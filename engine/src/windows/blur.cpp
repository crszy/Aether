#include "blur.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <qcoreapplication.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qjsonvalue.h>
#include <qjsvalue.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlengine.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>
#include <qvariant.h>
#include <qwineventnotifier.h>

#include "../core/generation.hpp"
#include "blur_shapes.hpp"
#include "panel_window.hpp"
#include "startup.hpp"
#include "util.hpp"

#include <unknwn.h>
#include <inspectable.h>

#include <DispatcherQueue.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.h>

#include <dwmapi.h>
#include <windows.ui.composition.interop.h>

namespace wuc = winrt::Windows::UI::Composition;
namespace wucd = winrt::Windows::UI::Composition::Desktop;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
using AbiDesktopInterop = ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop;
using AbiDesktopWindowTarget = ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget;
using AbiDispatcherQueueController = ABI::Windows::System::IDispatcherQueueController;

namespace qs::windows {

namespace {

Q_LOGGING_CATEGORY(logBlur, "quickshell.windows.blur", QtInfoMsg);

constexpr auto DEFAULT_FILE = "defaults/windows/layerrules.json";
constexpr auto PERSONALIZE_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr auto BACKDROP_CLASS = L"QuickshellBlurBackdrop";
constexpr DWORD WINDOWS_11_BUILD = 22000;

bool gManagerDestroyed = false; // NOLINT

QString hresultString(const winrt::hresult_error& error) {
	return QString("0x%1 %2")
	    .arg(static_cast<quint32>(error.code().value), 8, 16, QChar('0'))
	    .arg(QString::fromWCharArray(error.message().c_str()));
}

constexpr DWORD WCA_ACCENT_POLICY = 19;
constexpr DWORD ACCENT_ENABLE_BLURBEHIND = 3;
constexpr DWORD ACCENT_FLAG_GRADIENT_COLOR = 2;

struct AccentPolicy {
	DWORD accentState;
	DWORD accentFlags;
	DWORD gradientColor;
	DWORD animationId;
};

struct WindowCompositionAttribData {
	DWORD attribute;
	PVOID data;
	SIZE_T dataSize;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttribData*);

SetWindowCompositionAttributeFn setWindowCompositionAttribute() {
	static const auto function = []() -> SetWindowCompositionAttributeFn {
		auto* user32 = GetModuleHandleW(L"user32.dll");
		if (user32 == nullptr) return nullptr;

		return reinterpret_cast<SetWindowCompositionAttributeFn>( // NOLINT
		    GetProcAddress(user32, "SetWindowCompositionAttribute")
		);
	}();

	return function;
}

QRect nearestRect(const QRectF& rect) {
	auto left = static_cast<int>(std::lround(rect.left()));
	auto top = static_cast<int>(std::lround(rect.top()));
	auto right = static_cast<int>(std::lround(rect.right()));
	auto bottom = static_cast<int>(std::lround(rect.bottom()));
	return QRect(left, top, right - left, bottom - top);
}

void hideWindow(HWND hwnd) {
	if (!IsWindowVisible(hwnd)) return;

	SetWindowPos(
	    hwnd,
	    nullptr,
	    0,
	    0,
	    0,
	    0,
	    SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER
	);
}

} // namespace

class BackdropWindow {
public:
	BackdropWindow() = default;
	~BackdropWindow();
	Q_DISABLE_COPY_MOVE(BackdropWindow);

	static std::unique_ptr<BackdropWindow> create(
	    const wuc::Compositor& compositor,
	    bool layered,
	    bool tint,
	    QString& error,
	    bool& unsupported
	);

	static std::unique_ptr<BackdropWindow>
	createAccent(bool layered, bool tint, QString& error, bool& unsupported);

	[[nodiscard]] HWND hwnd() const { return this->mHwnd; }
	void setPanel(HWND panel) { this->mPanel = panel; }

	bool setShapes(const QList<BlurShape>& shapes);

	void show();
	void hide();

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static bool registerClass();
	static HWND createWindow(bool layered, BackdropWindow* owner, QString& error);
	void updateRegion(const QList<BlurShape>& shapes);

	HWND createPiece(QString& error, bool& unsupported);
	bool setPieces(const QList<BlurShape>& shapes);
	void showPieces();
	void hidePieces();
	[[nodiscard]] bool piecesInPlace(bool panelTopmost) const;

	struct Slot {
		wuc::ContainerVisual holder {nullptr};
		wuc::SpriteVisual sprite {nullptr};
		wuc::CompositionRoundedRectangleGeometry geometry {nullptr};
	};

	struct Piece {
		HWND hwnd = nullptr;
		QRect rect;
		QRect shape;
		int diameter = 0;
		bool hasRegion = false;
		RECT placed {};
	};

	HWND mHwnd = nullptr;
	HWND mPanel = nullptr;
	bool layered = false;
	bool syncing = false;
	bool shown = false;
	RECT lastRect {};
	QRegion region;
	bool hasRegion = false;

	bool accent = false;
	bool accentTint = false;
	std::vector<Piece> pieces;
	size_t pieceCount = 0;
	bool piecesMoved = false;

	wuc::Compositor compositor {nullptr};
	wucd::DesktopWindowTarget target {nullptr};
	wuc::ContainerVisual root {nullptr};
	wuc::CompositionBrush brush {nullptr};
	wuc::CompositionBrush tint {nullptr};
	std::vector<Slot> visuals;
	QList<BlurShape> applied;
};

bool BackdropWindow::registerClass() {
	static auto registered = false; // NOLINT
	if (registered) return true;

	WNDCLASSEXW wndClass {};
	wndClass.cbSize = sizeof(wndClass);
	wndClass.lpfnWndProc = &BackdropWindow::wndProc;
	wndClass.hInstance = GetModuleHandleW(nullptr);
	wndClass.lpszClassName = BACKDROP_CLASS;

	registered = RegisterClassExW(&wndClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	return registered;
}

HWND BackdropWindow::createWindow(bool layered, BackdropWindow* owner, QString& error) {
	if (!registerClass()) {
		error = QString("RegisterClassEx failed (%1)").arg(GetLastError());
		return nullptr;
	}

	DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP;
	if (layered) exStyle |= WS_EX_LAYERED;

	auto* hwnd = CreateWindowExW(
	    exStyle,
	    BACKDROP_CLASS,
	    L"",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    GetModuleHandleW(nullptr),
	    nullptr
	);

	if (hwnd != nullptr) markNonRude(hwnd);

	if (hwnd == nullptr) {
		error = QString("CreateWindowEx failed (%1)").arg(GetLastError());
		return nullptr;
	}

	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner));

	if (layered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

	return hwnd;
}

std::unique_ptr<BackdropWindow> BackdropWindow::create(
    const wuc::Compositor& compositor,
    bool layered,
    bool tint,
    QString& error,
    bool& unsupported
) {
	unsupported = false;

	auto backdrop = std::make_unique<BackdropWindow>();
	backdrop->layered = layered;
	backdrop->mHwnd = createWindow(layered, backdrop.get(), error);
	if (backdrop->mHwnd == nullptr) return nullptr;
	auto* hwnd = backdrop->mHwnd;

	BOOL enable = TRUE;
	auto hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &enable, sizeof(enable));
	if (FAILED(hr)) {
		error = QString("DWMWA_USE_HOSTBACKDROPBRUSH was refused (0x%1), Windows 11 is required")
		            .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
		unsupported = true;
		return nullptr;
	}

	auto corner = static_cast<DWM_WINDOW_CORNER_PREFERENCE>(DWMWCP_DONOTROUND);
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
	COLORREF border = DWMWA_COLOR_NONE;
	DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
	DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &enable, sizeof(enable));
	DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &enable, sizeof(enable));

	try {
		backdrop->compositor = compositor;

		auto interop = compositor.as<AbiDesktopInterop>();
		winrt::check_hresult(interop->CreateDesktopWindowTarget(
		    hwnd,
		    TRUE,
		    reinterpret_cast<AbiDesktopWindowTarget**>(winrt::put_abi(backdrop->target)) // NOLINT
		));

		backdrop->root = compositor.CreateContainerVisual();
		backdrop->root.RelativeSizeAdjustment(float2(1.0f, 1.0f));
		backdrop->target.Root(backdrop->root);

		backdrop->brush = compositor.CreateHostBackdropBrush();

		if (tint) {
			backdrop->tint = compositor.CreateColorBrush(
			    winrt::Windows::UI::Color {.A = 0x60, .R = 0xff, .G = 0x20, .B = 0x20}
			);
		}
	} catch (const winrt::hresult_error& e) {
		error = "Composition setup failed: " + hresultString(e);
		return nullptr;
	}

	return backdrop;
}

std::unique_ptr<BackdropWindow>
BackdropWindow::createAccent(bool layered, bool tint, QString& error, bool& unsupported) {
	auto backdrop = std::make_unique<BackdropWindow>();
	backdrop->layered = layered;
	backdrop->accent = true;
	backdrop->accentTint = tint;

	auto* hwnd = backdrop->createPiece(error, unsupported);
	if (hwnd == nullptr) return nullptr;

	backdrop->pieces.emplace_back().hwnd = hwnd;
	return backdrop;
}

HWND BackdropWindow::createPiece(QString& error, bool& unsupported) {
	unsupported = false;

	auto* setAttribute = setWindowCompositionAttribute();
	if (setAttribute == nullptr) {
		error = "SetWindowCompositionAttribute is missing from user32";
		unsupported = true;
		return nullptr;
	}

	auto* hwnd = createWindow(this->layered, this, error);
	if (hwnd == nullptr) return nullptr;

	auto policy = AccentPolicy {
	    .accentState = ACCENT_ENABLE_BLURBEHIND,
	    .accentFlags = this->accentTint ? ACCENT_FLAG_GRADIENT_COLOR : 0,
	    .gradientColor = this->accentTint ? 0x602020ffu : 0u,
	    .animationId = 0,
	};

	auto data = WindowCompositionAttribData {
	    .attribute = WCA_ACCENT_POLICY,
	    .data = &policy,
	    .dataSize = sizeof(policy),
	};

	if (!setAttribute(hwnd, &data)) {
		error = QString("SetWindowCompositionAttribute refused the accent policy (%1)")
		            .arg(GetLastError());
		unsupported = true;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
		return nullptr;
	}

	BOOL enable = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &enable, sizeof(enable));
	DwmSetWindowAttribute(hwnd, DWMWA_EXCLUDED_FROM_PEEK, &enable, sizeof(enable));

	return hwnd;
}

BackdropWindow::~BackdropWindow() {
	try {
		this->visuals.clear();
		if (this->root != nullptr) this->root.Children().RemoveAll();

		if (this->target != nullptr) {
			this->target.Root(nullptr);
			this->target.Close();
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logBlur) << "Releasing a backdrop:" << hresultString(e);
	}

	this->tint = nullptr;
	this->brush = nullptr;
	this->root = nullptr;
	this->target = nullptr;
	this->compositor = nullptr;

	if (this->mHwnd != nullptr) {
		SetWindowLongPtrW(this->mHwnd, GWLP_USERDATA, 0);
		DestroyWindow(this->mHwnd);
		this->mHwnd = nullptr;
	}

	for (auto& piece: this->pieces) {
		SetWindowLongPtrW(piece.hwnd, GWLP_USERDATA, 0);
		DestroyWindow(piece.hwnd);
	}

	this->pieces.clear();
}

bool BackdropWindow::setShapes(const QList<BlurShape>& shapes) {
	if (this->accent) return this->setPieces(shapes);

	try {
		auto children = this->root.Children();
		auto count = static_cast<size_t>(shapes.size());

		while (this->visuals.size() < count) {
			Slot slot;
			slot.holder = this->compositor.CreateContainerVisual();
			slot.holder.Clip(this->compositor.CreateInsetClip());

			slot.sprite = this->compositor.CreateSpriteVisual();
			slot.sprite.Brush(this->brush);
			slot.geometry = this->compositor.CreateRoundedRectangleGeometry();
			slot.sprite.Clip(this->compositor.CreateGeometricClip(slot.geometry));

			if (this->tint != nullptr) {
				auto overlay = this->compositor.CreateSpriteVisual();
				overlay.Brush(this->tint);
				overlay.RelativeSizeAdjustment(float2(1.0f, 1.0f));
				slot.sprite.Children().InsertAtTop(overlay);
			}

			slot.holder.Children().InsertAtTop(slot.sprite);
			children.InsertAtTop(slot.holder);
			this->visuals.push_back(std::move(slot));
		}

		while (this->visuals.size() > count) {
			children.Remove(this->visuals.back().holder);
			this->visuals.pop_back();
		}

		for (size_t i = 0; i < count; i++) {
			auto index = static_cast<qsizetype>(i);
			const auto& shape = shapes.at(index);
			if (index < this->applied.size() && this->applied.at(index).fuzzyEquals(shape)) continue;

			auto& slot = this->visuals.at(i);

			auto size = float2(static_cast<float>(shape.rect.width()), static_cast<float>(shape.rect.height()));
			auto radius = static_cast<float>(shape.radius);

			slot.holder.Offset(float3(static_cast<float>(shape.clip.x()), static_cast<float>(shape.clip.y()), 0));
			slot.holder.Size(float2(static_cast<float>(shape.clip.width()), static_cast<float>(shape.clip.height())));

			slot.sprite.Offset(float3(
			    static_cast<float>(shape.rect.x() - shape.clip.x()),
			    static_cast<float>(shape.rect.y() - shape.clip.y()),
			    0
			));
			slot.sprite.Size(size);
			slot.geometry.Size(size);
			slot.geometry.CornerRadius(float2(radius, radius));
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logBlur) << "Updating the blur shapes failed:" << hresultString(e);
		this->applied.clear();
		return false;
	}

	this->applied = shapes;
	if (!this->layered) this->updateRegion(shapes);
	return true;
}

void BackdropWindow::updateRegion(const QList<BlurShape>& shapes) {
	QRegion next;
	for (const auto& shape: shapes) next += shape.rect.intersected(shape.clip).toAlignedRect();

	if (this->hasRegion && next == this->region) return;

	auto* hrgn = CreateRectRgn(0, 0, 0, 0);
	for (const auto& rect: next) {
		auto* part = CreateRectRgn(rect.x(), rect.y(), rect.x() + rect.width(), rect.y() + rect.height());
		CombineRgn(hrgn, hrgn, part, RGN_OR);
		DeleteObject(part);
	}

	this->syncing = true;
	if (SetWindowRgn(this->mHwnd, hrgn, FALSE) == 0) DeleteObject(hrgn);
	this->syncing = false;

	this->region = next;
	this->hasRegion = true;
}

bool BackdropWindow::setPieces(const QList<BlurShape>& shapes) {
	struct Part {
		QRect rect;
		QRect shape;
		int diameter = 0;
	};

	struct Whole {
		QRect visible;
		QRect full;
		QRect clip;
		int radius = 0;
	};

	std::vector<Whole> wholes;
	wholes.reserve(static_cast<size_t>(shapes.size()));

	for (const auto& shape: shapes) {
		auto rect = nearestRect(shape.rect);
		auto clip = nearestRect(shape.clip);
		auto visible = rect.intersected(clip);
		if (visible.isEmpty()) continue;

		wholes.push_back({
		    .visible = visible,
		    .full = rect,
		    .clip = clip,
		    .radius = static_cast<int>(std::lround(shape.radius)),
		});
	}

	std::vector<Part> kept;

	for (size_t i = 0; i < wholes.size(); i++) {
		const auto& rect = wholes.at(i).visible;
		auto inside = false;

		for (size_t j = 0; j < wholes.size() && !inside; j++) {
			const auto& other = wholes.at(j).visible;
			inside = j != i && other.contains(rect) && (other != rect || j < i);
		}

		if (inside) continue;

		const auto& whole = wholes.at(i);
		auto add = [&](const QRect& band) {
			auto part = band.intersected(whole.clip);
			if (!part.isEmpty()) kept.push_back({.rect = part, .shape = QRect()});
		};

		auto full = whole.full;
		auto radius = std::min(whole.radius, std::min(full.width(), full.height()) / 2);
		if (radius < 2) {
			add(full);
			continue;
		}

		constexpr int STEPS = 3;
		add(QRect(full.left(), full.top() + radius, full.width(), full.height() - 2 * radius));

		for (auto step = 0; step < STEPS; step++) {
			auto from = radius * step / STEPS;
			auto to = radius * (step + 1) / STEPS;
			if (to <= from) continue;

			auto dy = static_cast<double>(radius - from);
			auto inset = static_cast<int>(
			    std::ceil(radius - std::sqrt(static_cast<double>(radius) * radius - dy * dy))
			);
			auto width = full.width() - 2 * inset;
			if (width <= 0) continue;

			add(QRect(full.left() + inset, full.top() + from, width, to - from));
			add(QRect(full.left() + inset, full.bottom() + 1 - to, width, to - from));
		}
	}

	while (this->pieces.size() < kept.size()) {
		QString error;
		auto unsupported = false;
		auto* hwnd = this->createPiece(error, unsupported);

		if (hwnd == nullptr) {
			qCWarning(logBlur).noquote() << "Adding an accent blur window failed:" << error;
			return false;
		}

		this->pieces.emplace_back().hwnd = hwnd;
	}

	this->syncing = true;

	for (size_t i = 0; i < kept.size(); i++) {
		auto& piece = this->pieces.at(i);
		const auto& part = kept.at(i);

		if (piece.rect != part.rect) {
			piece.rect = part.rect;
			this->piecesMoved = true;
		}

		if (piece.hasRegion && piece.shape == part.shape && piece.diameter == part.diameter) continue;

		HRGN hrgn = nullptr;
		if (part.diameter >= 2) {
			hrgn = CreateRoundRectRgn(
			    part.shape.left(),
			    part.shape.top(),
			    part.shape.left() + part.shape.width() + 1,
			    part.shape.top() + part.shape.height() + 1,
			    part.diameter,
			    part.diameter
			);
		}

		if (SetWindowRgn(piece.hwnd, hrgn, FALSE) == 0 && hrgn != nullptr) DeleteObject(hrgn);

		piece.shape = part.shape;
		piece.diameter = part.diameter;
		piece.hasRegion = true;
	}

	for (auto i = kept.size(); i < this->pieces.size(); i++) hideWindow(this->pieces.at(i).hwnd);

	this->syncing = false;

	if (kept.size() != this->pieceCount) this->piecesMoved = true;
	this->pieceCount = kept.size();
	return true;
}

void BackdropWindow::showPieces() {
	if (this->mPanel == nullptr) return;

	RECT panel {};
	if (!GetWindowRect(this->mPanel, &panel)) {
		this->hide();
		return;
	}

	auto panelTopmost = (GetWindowLongPtrW(this->mPanel, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	auto inPlace = this->shown && this->piecesInPlace(panelTopmost);

	if (inPlace && !this->piecesMoved && EqualRect(&panel, &this->lastRect)) return;

	struct Move {
		HWND hwnd = nullptr;
		HWND after = nullptr;
		RECT rect {};
		UINT flags = 0;
	};

	std::vector<Move> moves;
	moves.reserve(this->pieceCount);

	this->syncing = true;
	constexpr UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;

	HWND above = this->mPanel;

	for (size_t i = 0; i < this->pieceCount; i++) {
		auto& piece = this->pieces.at(i);
		auto* after = std::exchange(above, piece.hwnd);

		auto rect = RECT {
		    .left = panel.left + piece.rect.x(),
		    .top = panel.top + piece.rect.y(),
		    .right = panel.left + piece.rect.x() + piece.rect.width(),
		    .bottom = panel.top + piece.rect.y() + piece.rect.height(),
		};

		if (inPlace && EqualRect(&rect, &piece.placed)) continue;

		auto ownTopmost = (GetWindowLongPtrW(piece.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

		if (panelTopmost != ownTopmost) {
			SetWindowPos(
			    piece.hwnd,
			    panelTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
			    0,
			    0,
			    0,
			    0,
			    flags | SWP_NOMOVE | SWP_NOSIZE
			);
		}

		moves.push_back({
		    .hwnd = piece.hwnd,
		    .after = after,
		    .rect = rect,
		    .flags = flags | SWP_SHOWWINDOW | (inPlace ? SWP_NOZORDER : 0),
		});

		piece.placed = rect;
	}

	if (!moves.empty()) {
		auto* batch = BeginDeferWindowPos(static_cast<int>(moves.size()));

		for (const auto& move: moves) {
			if (batch == nullptr) break;

			batch = DeferWindowPos(
			    batch,
			    move.hwnd,
			    move.after,
			    move.rect.left,
			    move.rect.top,
			    move.rect.right - move.rect.left,
			    move.rect.bottom - move.rect.top,
			    move.flags
			);
		}

		if (batch == nullptr || !EndDeferWindowPos(batch)) {
			for (const auto& move: moves) {
				SetWindowPos(
				    move.hwnd,
				    move.after,
				    move.rect.left,
				    move.rect.top,
				    move.rect.right - move.rect.left,
				    move.rect.bottom - move.rect.top,
				    move.flags
				);
			}
		}
	}

	this->syncing = false;
	this->lastRect = panel;
	this->piecesMoved = false;
	this->shown = true;
}

bool BackdropWindow::piecesInPlace(bool panelTopmost) const {
	HWND above = this->mPanel;

	for (size_t i = 0; i < this->pieceCount; i++) {
		auto* hwnd = this->pieces.at(i).hwnd;
		auto topmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

		if (!IsWindowVisible(hwnd) || topmost != panelTopmost
		    || GetWindow(above, GW_HWNDNEXT) != hwnd)
		{
			return false;
		}

		above = hwnd;
	}

	return true;
}

void BackdropWindow::hidePieces() {
	this->syncing = true;
	for (const auto& piece: this->pieces) hideWindow(piece.hwnd);
	this->syncing = false;
}

void BackdropWindow::show() {
	if (this->accent) {
		this->showPieces();
		return;
	}

	if (this->mHwnd == nullptr || this->mPanel == nullptr) return;

	RECT rect {};
	if (!GetWindowRect(this->mPanel, &rect)) {
		this->hide();
		return;
	}

	auto panelTopmost = (GetWindowLongPtrW(this->mPanel, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	auto ownTopmost = (GetWindowLongPtrW(this->mHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;

	if (this->shown && IsWindowVisible(this->mHwnd) && EqualRect(&rect, &this->lastRect)
	    && panelTopmost == ownTopmost && GetWindow(this->mPanel, GW_HWNDNEXT) == this->mHwnd)
	{
		return;
	}

	this->syncing = true;
	constexpr UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;

	if (panelTopmost != ownTopmost) {
		SetWindowPos(
		    this->mHwnd,
		    panelTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		    0,
		    0,
		    0,
		    0,
		    flags | SWP_NOMOVE | SWP_NOSIZE
		);
	}

	SetWindowPos(
	    this->mHwnd,
	    this->mPanel,
	    rect.left,
	    rect.top,
	    rect.right - rect.left,
	    rect.bottom - rect.top,
	    flags | SWP_SHOWWINDOW
	);

	this->syncing = false;
	this->lastRect = rect;
	this->shown = true;
}

void BackdropWindow::hide() {
	this->shown = false;

	if (this->accent) {
		this->hidePieces();
		return;
	}

	if (this->mHwnd == nullptr || !IsWindowVisible(this->mHwnd)) return;

	this->syncing = true;
	SetWindowPos(
	    this->mHwnd,
	    nullptr,
	    0,
	    0,
	    0,
	    0,
	    SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER
	);
	this->syncing = false;
}

LRESULT CALLBACK BackdropWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// NOLINTNEXTLINE(performance-no-int-to-ptr)
	auto* self = reinterpret_cast<BackdropWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	switch (msg) {
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT: ValidateRect(hwnd, nullptr); return 0;
	case WM_DPICHANGED: return 0;
	case WM_WINDOWPOSCHANGING:
		if (self != nullptr && !self->syncing && self->mPanel != nullptr && IsWindow(self->mPanel)) {
			auto* pos = reinterpret_cast<WINDOWPOS*>(lParam); // NOLINT(performance-no-int-to-ptr)
			if ((pos->flags & SWP_NOZORDER) == 0) pos->hwndInsertAfter = self->mPanel;
		}
		break;
	default: break;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct BlurManager::Composition {
	winrt::Windows::System::DispatcherQueueController controller {nullptr};
	wuc::Compositor compositor {nullptr};
};

BlurManager* BlurManager::instance() {
	static QPointer<BlurManager> manager; // NOLINT

	if (gManagerDestroyed) return nullptr;

	if (manager.isNull()) {
		manager = new BlurManager(QCoreApplication::instance());
	}

	return manager.data();
}

BlurManager::BlurManager(QObject* parent): QObject(parent) {
	auto env = qEnvironmentVariable("QS_WINDOWS_BLUR").trimmed().toLower();
	this->envEnabled = !(env == "0" || env == "false" || env == "off" || env == "no");
	this->envLayered = qEnvironmentVariableIntValue("QS_WINDOWS_BLUR_LAYERED") != 0;
	this->mDebugTint = qEnvironmentVariableIntValue("QS_WINDOWS_BLUR_DEBUG") != 0;

	if (!this->envEnabled) qCInfo(logBlur) << "Blur behind panels is turned off by QS_WINDOWS_BLUR.";
	else this->detectBackend();

	this->reloadTimer.setSingleShot(true);
	this->reloadTimer.setInterval(200);
	QObject::connect(&this->reloadTimer, &QTimer::timeout, this, &BlurManager::loadFile);

	// clang-format off
	QObject::connect(&this->watcher, &QFileSystemWatcher::fileChanged, this, &BlurManager::scheduleReload);
	QObject::connect(&this->watcher, &QFileSystemWatcher::directoryChanged, this, &BlurManager::scheduleReload);
	// clang-format on

	if (auto* app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, this, &BlurManager::shutdown);
	}

	this->checkSystem();
	this->watchRegistry();
}

BlurManager::~BlurManager() {
	gManagerDestroyed = true;

	if (this->composition != nullptr) {
		winrt::detach_abi(this->composition->compositor);
		winrt::detach_abi(this->composition->controller);
	}

	delete this->registryNotifier;
	this->registryNotifier = nullptr;
	if (this->registryEvent != nullptr) CloseHandle(this->registryEvent);
	if (this->personalizeKey != nullptr) RegCloseKey(this->personalizeKey);
}

void BlurManager::detectBackend() {
	auto build = windowsBuild();

	if (build == 0 || build >= WINDOWS_11_BUILD) {
		this->mBackend = Backend::HostBackdrop;
		qCInfo(logBlur).nospace() << "Blur behind panels uses the host backdrop brush (Windows build "
		                          << build << ").";
		return;
	}

	this->mBackend = Backend::Accent;

	if (setWindowCompositionAttribute() == nullptr) {
		this->markUnsupported(
		    QString("SetWindowCompositionAttribute is missing (Windows build %1)").arg(build)
		);
		return;
	}

	qCInfo(logBlur).nospace() << "Blur behind panels uses SetWindowCompositionAttribute blur behind "
	                          << "(Windows build " << build << ").";
}

void BlurManager::shutdown() {
	if (this->mShutDown) return;
	this->mShutDown = true;

	for (auto* panel: QList(this->panels)) panel->updateActive();

	if (this->composition != nullptr) {
		this->closeCompositor();
		winrt::detach_abi(this->composition->controller);
		this->composition.reset();
	}
}

void BlurManager::closeCompositor() {
	if (this->composition == nullptr || this->composition->compositor == nullptr) return;

	try {
		this->composition->compositor.Close();
	} catch (const winrt::hresult_error& e) {
		qCDebug(logBlur) << "Closing the compositor:" << hresultString(e);
	}

	this->composition->compositor = nullptr;
}

BlurManager::Composition* BlurManager::ensureComposition() {
	if (this->unsupported || this->mShutDown) return nullptr;

	if (this->composition == nullptr) this->composition = std::make_unique<Composition>();
	auto* state = this->composition.get();
	if (state->compositor != nullptr) return state;

	try {
		if (state->controller == nullptr
		    && winrt::Windows::System::DispatcherQueue::GetForCurrentThread() == nullptr)
		{
			DispatcherQueueOptions options {
			    .dwSize = sizeof(DispatcherQueueOptions),
			    .threadType = DQTYPE_THREAD_CURRENT,
			    .apartmentType = DQTAT_COM_NONE,
			};

			winrt::check_hresult(CreateDispatcherQueueController(
			    options,
			    reinterpret_cast<AbiDispatcherQueueController**>(winrt::put_abi(state->controller)) // NOLINT
			));
		}

		state->compositor = wuc::Compositor();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logBlur).noquote() << "Could not create a compositor for blur:" << hresultString(e);
		state->compositor = nullptr;
		return nullptr;
	}

	qCDebug(logBlur) << "Compositor created.";
	return state;
}

bool BlurManager::deferComposition() {
	if (this->unsupported || this->mShutDown || this->mBackend != Backend::HostBackdrop) return false;
	if (startup::settled()) return false;
	if (this->composition != nullptr && this->composition->compositor != nullptr) return false;

	if (!this->compositionWaiting) {
		this->compositionWaiting = true;
		qCDebug(logBlur) << "Creating the compositor after the first panel frame.";

		startup::afterFirstFrame(this, [this]() {
			this->compositionWaiting = false;
			this->scheduleNotifyPanels();
		});
	}

	return true;
}

void BlurManager::markUnsupported(const QString& reason) {
	if (this->unsupported) return;
	this->unsupported = true;

	qCWarning(logBlur).noquote() << "Blur behind panels is unavailable:" << reason;
	emit this->stateChanged();
	this->scheduleNotifyPanels();
}

void BlurManager::registerPanel(PanelBlur* panel) {
	if (!this->panels.contains(panel)) this->panels.append(panel);
}

void BlurManager::unregisterPanel(PanelBlur* panel) { this->panels.removeOne(panel); }

void BlurManager::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	this->mEnabled = enabled;

	emit this->stateChanged();
	this->scheduleNotifyPanels();
}

void BlurManager::setPanelAnimations(bool enabled) {
	if (enabled == this->mPanelAnimations) return;
	this->mPanelAnimations = enabled;

	qCInfo(logBlur) << "Panel open and close animations" << (enabled ? "on" : "off");
	emit this->panelAnimationsChanged();
}

void BlurManager::setExpressiveAnimations(bool expressive) {
	if (expressive == this->mExpressiveAnimations) return;
	this->mExpressiveAnimations = expressive;

	qCInfo(logBlur) << "Panel animation style" << (expressive ? "expressive" : "standard");
	emit this->panelAnimationsChanged();
}

bool BlurManager::available() const {
	return !this->unsupported && !this->mShutDown && this->envEnabled && this->rules.enabled()
	    && this->transparencyEffects && !this->highContrast && !this->energySaver;
}

bool BlurManager::layeredBackdrops() const { return this->envLayered || this->rules.layered(); }

void BlurManager::scheduleNotifyPanels() {
	if (this->notifyPending) return;
	this->notifyPending = true;

	QMetaObject::invokeMethod(
	    this,
	    [this]() {
		    this->notifyPending = false;
		    this->notifyPanels();
	    },
	    Qt::QueuedConnection
	);
}

void BlurManager::notifyPanels() {
	for (auto* panel: QList(this->panels)) panel->updateActive();
}

void BlurManager::scheduleSystemCheck(bool recreate) {
	if (recreate) this->recreatePending = true;
	if (this->systemCheckPending) return;
	this->systemCheckPending = true;

	QTimer::singleShot(0, this, [this]() {
		this->systemCheckPending = false;
		this->checkSystem();

		if (this->recreatePending) {
			this->recreatePending = false;
			if (this->mShutDown) return;

			qCInfo(logBlur) << "Desktop composition changed, recreating the blur backdrops.";

			for (auto* panel: QList(this->panels)) panel->recreate();

			this->closeCompositor();
			this->notifyPanels();
		}
	});
}

void BlurManager::checkSystem() {
	DWORD value = 1;
	DWORD size = sizeof(value);
	auto status = RegGetValueW(
	    HKEY_CURRENT_USER,
	    PERSONALIZE_KEY,
	    L"EnableTransparency",
	    RRF_RT_REG_DWORD,
	    nullptr,
	    &value,
	    &size
	);
	auto transparency = status != ERROR_SUCCESS || value != 0;

	HIGHCONTRASTW contrast {};
	contrast.cbSize = sizeof(contrast);
	auto highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)
	                 && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;

	SYSTEM_POWER_STATUS power {};
	auto energySaver = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;

	if (transparency == this->transparencyEffects && highContrast == this->highContrast
	    && energySaver == this->energySaver)
	{
		return;
	}

	this->transparencyEffects = transparency;
	this->highContrast = highContrast;
	this->energySaver = energySaver;

	qCInfo(logBlur) << "System state: transparency effects" << transparency << "high contrast"
	                << highContrast << "energy saver" << energySaver;

	emit this->stateChanged();
	this->scheduleNotifyPanels();
}

void BlurManager::watchRegistry() {
	auto status = RegOpenKeyExW(
	    HKEY_CURRENT_USER,
	    PERSONALIZE_KEY,
	    0,
	    KEY_NOTIFY | KEY_QUERY_VALUE,
	    &this->personalizeKey
	);

	if (status != ERROR_SUCCESS) {
		this->personalizeKey = nullptr;
		qCDebug(logBlur) << "Can't watch the Personalize key:" << status;
		return;
	}

	this->registryEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (this->registryEvent == nullptr) return;

	this->registryNotifier = new QWinEventNotifier(this->registryEvent, this);
	QObject::connect(this->registryNotifier, &QWinEventNotifier::activated, this, [this]() {
		this->armRegistryNotification();
		this->scheduleSystemCheck();
	});

	this->armRegistryNotification();
}

void BlurManager::armRegistryNotification() {
	if (this->personalizeKey == nullptr || this->registryEvent == nullptr) return;

	RegNotifyChangeKeyValue(
	    this->personalizeKey,
	    FALSE,
	    REG_NOTIFY_CHANGE_LAST_SET,
	    this->registryEvent,
	    TRUE
	);
}

void BlurManager::setShellDir(const QString& shellDir) {
	if (shellDir.isEmpty() || QDir::isRelativePath(shellDir)) return;
	if (this->loaded && shellDir == this->shellDir) return;

	this->shellDir = shellDir;
	this->loaded = true;
	this->parsed = false;
	this->loadFile();
}

void BlurManager::reload() {
	this->parsed = false;
	this->loadFile();
}

QString BlurManager::userFilePath() const {
	if (this->userFile.isEmpty()) return QString();
	if (!QDir::isRelativePath(this->userFile)) return this->userFile;
	auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	if (base.isEmpty()) return QString();
	return base + '/' + this->userFile;
}

QString BlurManager::readUserFile() const {
	auto file = QFile(this->defaultFilePath());
	if (this->defaultFilePath().isEmpty() || !file.open(QFile::ReadOnly)) return QString();
	return QJsonDocument::fromJson(file.readAll()).object().value("userFile").toString().trimmed();
}

QString BlurManager::defaultFilePath() const {
	if (this->shellDir.isEmpty()) return QString();
	return this->shellDir + '/' + DEFAULT_FILE;
}

void BlurManager::scheduleReload() { this->reloadTimer.start(); }

void BlurManager::loadFile() {
	this->userFile = this->readUserFile();
	auto path = this->userFilePath();
	if (path.isEmpty() || !QFileInfo::exists(path)) path = this->defaultFilePath();

	QByteArray data;
	auto file = QFile(path);
	if (path.isEmpty() || !file.open(QFile::ReadOnly)) path.clear();
	else data = file.readAll();

	if (!this->parsed || path != this->mConfigPath || data != this->loadedData) {
		if (path.isEmpty()) {
			auto looked = QStringList {this->userFilePath(), this->defaultFilePath()};
			looked.removeAll(QString());
			qCInfo(logBlur).noquote() << "No layer rules file found (looked for" << looked.join(" and ")
			                          << "), blur behind panels is off.";
		}

		auto wasLayered = this->layeredBackdrops();
		auto wasAvailable = this->available();

		this->parsed = true;
		this->mConfigPath = path;
		this->loadedData = data;
		this->parse(data, path);

		if (this->layeredBackdrops() != wasLayered) {
			for (auto* panel: QList(this->panels)) panel->recreate();
		}

		emit this->rulesChanged();
		if (this->available() != wasAvailable) emit this->stateChanged();
		this->scheduleNotifyPanels();
	}

	this->updateWatches();
}

void BlurManager::updateWatches() {
	auto userPath = this->userFilePath();

	QStringList wanted;
	if (!this->mConfigPath.isEmpty()) wanted.append(this->mConfigPath);
	auto defaultPath = this->defaultFilePath();
	if (!defaultPath.isEmpty() && !wanted.contains(defaultPath)) wanted.append(defaultPath);
	if (!userPath.isEmpty()) {
		auto userDir = QFileInfo(userPath).absolutePath();
		if (QFileInfo::exists(userDir)) wanted.append(userDir);
		else wanted.append(QFileInfo(userDir).absolutePath());
	}

	auto current = this->watcher.files() + this->watcher.directories();
	for (const auto& path: current) {
		if (!wanted.contains(path)) this->watcher.removePath(path);
	}

	for (const auto& path: wanted) {
		if (!current.contains(path) && QFileInfo::exists(path)) this->watcher.addPath(path);
	}
}

void BlurManager::parse(const QByteArray& data, const QString& path) {
	QStringList warnings;
	this->rules = LayerRules::parse(data, path, &warnings);

	for (const auto& warning: warnings) qCWarning(logBlur).noquote() << warning;

	if (this->rules.valid()) {
		qCInfo(logBlur).noquote() << "Loaded" << this->rules.size() << "layer rules from" << path;
	}
}

LayerRule BlurManager::ruleFor(const QString& ns) const { return this->rules.ruleFor(ns); }

QVariantList BlurManager::rulesInfo() const { return this->rules.info(); }

PanelBlur::PanelBlur(WinPanelWindow* panel): QObject(panel), panel(panel) {
	if (auto* manager = BlurManager::instance()) manager->registerPanel(this);
	QObject::connect(panel, &WinPanelWindow::namespaceChanged, this, &PanelBlur::updateActive);
}

PanelBlur::~PanelBlur() {
	this->disconnectFrames();
	this->backdrop.reset();
	if (auto* manager = BlurManager::instance()) manager->unregisterPanel(this);
}

void PanelBlur::adopt(PanelBlur* other) {
	if (other == nullptr || other == this) return;

	other->disconnectFrames();
	this->destroyBackdrop();

	this->backdrop = std::move(other->backdrop);
	this->shapes = std::move(other->shapes);
	this->shapesForced = true;
	this->active = this->backdrop != nullptr && other->active;
	this->stale = other->stale;
	this->panelWasShown = other->panelWasShown;
	this->mask = other->mask;
	this->hasMask = other->hasMask;

	other->shapes.clear();
	other->active = false;
}

void PanelBlur::attach() {
	this->mWindow = this->panel->backingWindow();
	if (this->backdrop != nullptr) this->backdrop->setPanel(this->panel->hwnd());

	this->updateActive();
	this->syncPlacement();
}

void PanelBlur::release() {
	this->disconnectFrames();
	this->destroyBackdrop();
	this->active = false;
	this->mWindow = nullptr;
	this->panelWasShown = false;
}

void PanelBlur::recreate() {
	this->disconnectFrames();
	this->destroyBackdrop();
	this->active = false;
}

void PanelBlur::setSuspended(bool suspended) {
	if (suspended == this->suspended) return;
	this->suspended = suspended;

	if (suspended) {
		if (this->backdrop != nullptr) this->backdrop->hide();
		return;
	}

	this->stale = true;
	if (this->active && this->panelShown()) this->scheduleShapes();
}

void PanelBlur::destroyBackdrop() {
	this->backdrop.reset();
	this->shapes.clear();
	this->shapesForced = true;
	this->stale = true;
}

void PanelBlur::connectFrames() {
	auto* window = this->mWindow.data();
	if (window == nullptr) return;
	if (this->frameConnection && this->framesWindow == window) return;

	this->disconnectFrames();

	this->frameConnection =
	    QObject::connect(window, &QQuickWindow::afterAnimating, this, &PanelBlur::onFrame);
	this->framesWindow = window;
}

void PanelBlur::disconnectFrames() {
	if (this->frameConnection) QObject::disconnect(this->frameConnection);
	this->frameConnection = {};
	this->framesWindow = nullptr;
}

void PanelBlur::updateActive() {
	auto* manager = BlurManager::instance();
	auto* hwnd = this->panel->hwnd();

	if (manager != nullptr && !manager->shutDown()) {
		if (auto* generation = EngineGeneration::findObjectGeneration(this->panel)) {
			manager->setShellDir(generation->rootPath.path());
		}

		this->rule = manager->ruleFor(this->panel->ns());
	} else {
		this->rule = LayerRule();
	}

	auto wanted = manager != nullptr && manager->enabled() && manager->available() && this->rule.blur
	           && (!this->rule.ignoreAlpha.has_value() || *this->rule.ignoreAlpha < 1.0)
	           && hwnd != nullptr && this->mWindow != nullptr
	           && !this->panel->isEmbedded();

	if (!wanted) {
		if (this->active) qCDebug(logBlur) << "Blur off for" << this->panel->ns();
		this->active = false;
		this->disconnectFrames();
		this->destroyBackdrop();
		return;
	}

	if (!this->active) qCDebug(logBlur) << "Blur on for" << this->panel->ns();
	this->active = true;
	if (this->backdrop != nullptr) this->backdrop->setPanel(hwnd);
	this->connectFrames();
	this->scheduleShapes();
}

bool PanelBlur::ensureBackdrop() {
	if (this->backdrop != nullptr) return true;

	auto* manager = BlurManager::instance();
	if (manager == nullptr) return false;

	QString error;
	auto unsupported = false;

	if (manager->backend() == BlurManager::Backend::Accent) {
		if (!manager->available()) return false;

		this->backdrop = BackdropWindow::createAccent(
		    manager->layeredBackdrops(),
		    manager->debugTint(),
		    error,
		    unsupported
		);
	} else {
		auto* composition = manager->ensureComposition();
		if (composition == nullptr) return false;

		this->backdrop = BackdropWindow::create(
		    composition->compositor,
		    manager->layeredBackdrops(),
		    manager->debugTint(),
		    error,
		    unsupported
		);
	}

	if (this->backdrop == nullptr) {
		if (unsupported) manager->markUnsupported(error);
		else qCWarning(logBlur).noquote() << "No blur for" << this->panel->ns() << ":" << error;
		return false;
	}

	this->backdrop->setPanel(this->panel->hwnd());
	this->shapes.clear();
	return true;
}

void PanelBlur::setInputMask(const QRegion& region, bool hasMask) {
	if (hasMask == this->hasMask && region == this->mask) return;

	this->mask = region;
	this->hasMask = hasMask;
	if (this->active) this->scheduleShapes();
}

void PanelBlur::onFrame() { this->updateShapes(); }

void PanelBlur::scheduleShapes() {
	this->shapesForced = true;
	if (this->shapesPending) return;
	this->shapesPending = true;

	QTimer::singleShot(0, this, [this]() {
		this->shapesPending = false;
		if (this->frameConnection && framePending(this->mWindow.data())) return;
		this->updateShapes();
	});
}

bool PanelBlur::panelShown() const {
	auto* hwnd = this->panel->hwnd();
	return hwnd != nullptr && IsWindowVisible(hwnd) && !IsIconic(hwnd);
}

PanelBlur::ShapeInputs PanelBlur::shapeInputs(bool shown) const {
	auto* window = this->mWindow.data();
	if (window == nullptr) return {.shown = shown, .size = QSize(), .alpha = -1, .dpr = 0};

	return {
	    .shown = shown,
	    .size = window->size(),
	    .alpha = static_cast<qreal>(window->color().alphaF()),
	    .dpr = window->devicePixelRatio(),
	};
}

void PanelBlur::updateShapes() {
	if (!this->active || this->suspended) return;

	auto shown = this->panelShown();
	auto inputs = this->shapeInputs(shown);
	auto walks = shown && this->rule.ignoreAlpha.has_value();

	if (!this->shapesForced && inputs == this->lastInputs
	    && !(walks && itemTreeDirty(this->mWindow.data())))
	{
		this->syncPlacement();
		return;
	}

	this->shapesForced = false;
	this->lastInputs = inputs;

	QList<BlurShape> next;
	if (shown) this->collectShapes(next);

	if (this->backdrop == nullptr) {
		if (next.isEmpty()) return;

		auto* manager = BlurManager::instance();
		if (manager != nullptr && manager->deferComposition()) return;

		if (!this->ensureBackdrop()) {
			this->active = false;
			this->disconnectFrames();
			return;
		}
	}

	auto same = next.size() == this->shapes.size()
	         && std::equal(next.begin(), next.end(), this->shapes.begin(), [](const auto& a, const auto& b) {
		            return a.fuzzyEquals(b);
	            });

	if (!same) {
		if (!this->backdrop->setShapes(next)) {
			this->active = false;
			this->disconnectFrames();
			this->destroyBackdrop();
			return;
		}

		if (logBlur().isDebugEnabled()) {
			QStringList list;
			for (const auto& shape: next) {
				list.append(QString("%1x%2+%3+%4 r%5")
				                .arg(shape.rect.width())
				                .arg(shape.rect.height())
				                .arg(shape.rect.x())
				                .arg(shape.rect.y())
				                .arg(shape.radius));
			}

			qCDebug(logBlur).noquote() << "Blur shapes of" << this->panel->ns() << ":"
			                           << (list.isEmpty() ? "none" : list.join(", "));
		}

		this->shapes = std::move(next);
	}

	this->stale = false;
	this->syncPlacement();
}

void PanelBlur::syncPlacement() {
	auto shown = this->panelShown();

	if (shown && !this->panelWasShown) {
		this->stale = true;
		if (this->active) this->scheduleShapes();
	}

	this->panelWasShown = shown;
	if (this->backdrop == nullptr) return;

	if (!shown || !this->active || this->suspended || this->stale || this->shapes.isEmpty()) {
		this->backdrop->hide();
	} else {
		this->backdrop->show();
	}
}

void PanelBlur::collectShapes(QList<BlurShape>& shapes) {
	auto* window = this->mWindow.data();
	if (window == nullptr) return;

	auto query = BlurShapeQuery {
	    .ignoreAlpha = this->rule.ignoreAlpha,
	    .mask = this->mask,
	    .hasMask = this->hasMask,
	    .dpr = window->devicePixelRatio(),
	};

	auto result = collectBlurShapes(window, query);
	shapes = std::move(result.shapes);

	if (result.truncated && !this->truncatedWarned) {
		this->truncatedWarned = true;
		qCWarning(logBlur) << "Item tree of" << this->panel->ns()
		                   << "is too large to walk every frame, blur only covers part of it.";
	}
}

BackdropBlur::BackdropBlur(QObject* parent): QObject(parent) {
	if (auto* manager = BlurManager::instance()) {
		// clang-format off
		QObject::connect(manager, &BlurManager::stateChanged, this, &BackdropBlur::stateChanged);
		QObject::connect(manager, &BlurManager::rulesChanged, this, &BackdropBlur::rulesChanged);
		// clang-format on
		QObject::connect(
		    manager,
		    &BlurManager::panelAnimationsChanged,
		    this,
		    &BackdropBlur::panelAnimationsChanged
		);
	}
}

BackdropBlur* BackdropBlur::create(QQmlEngine* engine, QJSEngine* /*jsEngine*/) {
	auto* blur = new BackdropBlur(nullptr);

	auto* manager = BlurManager::instance();
	auto* generation = EngineGeneration::findEngineGeneration(engine);
	if (manager != nullptr && generation != nullptr) manager->setShellDir(generation->rootPath.path());

	return blur;
}

bool BackdropBlur::enabled() {
	auto* manager = BlurManager::instance();
	return manager != nullptr && manager->enabled();
}

void BackdropBlur::setEnabled(bool enabled) {
	if (auto* manager = BlurManager::instance()) manager->setEnabled(enabled);
}

bool BackdropBlur::available() {
	auto* manager = BlurManager::instance();
	return manager != nullptr && manager->available();
}

bool BackdropBlur::panelAnimations() {
	auto* manager = BlurManager::instance();
	return manager == nullptr || manager->panelAnimations();
}

void BackdropBlur::setPanelAnimations(bool enabled) {
	if (auto* manager = BlurManager::instance()) manager->setPanelAnimations(enabled);
}

QString BackdropBlur::panelAnimationStyle() {
	auto* manager = BlurManager::instance();
	return manager != nullptr && manager->expressiveAnimations() ? "expressive" : "standard";
}

void BackdropBlur::setPanelAnimationStyle(const QString& style) {
	if (auto* manager = BlurManager::instance()) {
		auto expressive = style.trimmed().compare("expressive", Qt::CaseInsensitive) == 0;
		manager->setExpressiveAnimations(expressive);
	}
}

QString BackdropBlur::configPath() {
	auto* manager = BlurManager::instance();
	return manager != nullptr ? manager->configPath() : QString();
}

QVariantList BackdropBlur::rules() {
	auto* manager = BlurManager::instance();
	return manager != nullptr ? manager->rulesInfo() : QVariantList();
}

void BackdropBlur::reload() {
	if (auto* manager = BlurManager::instance()) manager->reload();
}

} // namespace qs::windows
