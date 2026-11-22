#include "screencopy_view.hpp"

#include <memory>

#include <qt_windows.h>

#include <d3d11.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qqmlinfo.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qsgrendererinterface.h>
#include <qsgsimpletexturenode.h>
#include <qsgtexture.h>
#include <qsgtexture_platform.h>
#include <qsize.h>
#include <qtmetamacros.h>

#include "../../core/qmlscreen.hpp"
#include "../capture.hpp"
#include "../util.hpp"
#include "toplevel.hpp"

using namespace qs::windows::capture;

namespace qs::wayland::screencopy {

namespace {
Q_LOGGING_CATEGORY(logScreencopy, "quickshell.windows.screencopy", QtWarningMsg);

constexpr int LIVE_FPS = 30;

class CaptureNode: public QSGSimpleTextureNode {
public:
	explicit CaptureNode(QQuickWindow* window): window(window) {
		this->setFiltering(QSGTexture::Linear);
		this->setOwnsTexture(false);
	}

	~CaptureNode() override {
		delete this->sgTexture;
		if (this->texture != nullptr) this->texture->Release();
	}

	Q_DISABLE_COPY_MOVE(CaptureNode);

	[[nodiscard]] bool hasTexture() const { return this->sgTexture != nullptr; }

	bool sync(const std::shared_ptr<SharedFrame>& frame, quint64 serial) {
		if (!frame) return true;

		auto* ri = this->window->rendererInterface();
		if (ri->graphicsApi() != QSGRendererInterface::Direct3D11) {
			if (!this->warnedApi) {
				this->warnedApi = true;
				qCWarning(logScreencopy) << "ScreencopyView needs the Direct3D 11 scene graph backend; got"
				                         << ri->graphicsApi();
			}
			return true;
		}

		auto* device = static_cast<ID3D11Device*>(ri->getResource(this->window, QSGRendererInterface::DeviceResource));
		auto* context = static_cast<ID3D11DeviceContext*>(
		    ri->getResource(this->window, QSGRendererInterface::DeviceContextResource)
		);
		if (device == nullptr || context == nullptr) return true;

		if (frame != this->reader.frame()) {
			if (!this->reader.open(device, frame)) return true;

			if (this->texture == nullptr || this->size != frame->size) {
				delete this->sgTexture;
				this->sgTexture = nullptr;
				if (this->texture != nullptr) this->texture->Release();
				this->texture = nullptr;

				D3D11_TEXTURE2D_DESC desc {};
				desc.Width = static_cast<UINT>(frame->size.width());
				desc.Height = static_cast<UINT>(frame->size.height());
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				auto hr = device->CreateTexture2D(&desc, nullptr, &this->texture);
				if (FAILED(hr)) {
					qCWarning(logScreencopy) << "CreateTexture2D on the scene graph device failed:" << Qt::hex << hr;
					return true;
				}

				this->size = frame->size;
				this->sgTexture = QNativeInterface::QSGD3D11Texture::fromNative(
				    this->texture,
				    this->window,
				    frame->size,
				    QQuickWindow::TextureHasAlphaChannel
				);
				this->setTexture(this->sgTexture);
			}

			this->serial = 0;
		}

		if (serial == this->serial || this->texture == nullptr) return true;
		if (!this->reader.copyTo(context, this->texture)) return false;

		this->serial = serial;
		this->markDirty(QSGNode::DirtyMaterial);
		return true;
	}

private:
	QQuickWindow* window;
	SharedFrameReader reader;
	ID3D11Texture2D* texture = nullptr;
	QSGTexture* sgTexture = nullptr;
	QSize size;
	quint64 serial = 0;
	bool warnedApi = false;
};

} // namespace

ScreencopyView::ScreencopyView(QQuickItem* parent): QQuickItem(parent) {
	this->bImplicitSize.setBinding([this] {
		auto constraint = this->bConstraintSize.value();
		auto size = this->bSourceSize.value().toSizeF();

		if (constraint.width() != 0 && constraint.height() != 0) {
			size.scale(constraint.width(), constraint.height(), Qt::KeepAspectRatio);
		} else if (constraint.width() != 0) {
			size = QSizeF(constraint.width(), size.height() / constraint.width());
		} else if (constraint.height() != 0) {
			size = QSizeF(size.width() / constraint.height(), constraint.height());
		}

		return size;
	});
}

ScreencopyView::~ScreencopyView() { delete this->mHandle; }

void ScreencopyView::setCaptureSource(QObject* captureSource) {
	if (captureSource == this->mCaptureSource) return;
	auto hadContext = this->mHandle != nullptr;
	this->destroyContext(false);

	if (this->mCaptureSource) {
		QObject::disconnect(this->mCaptureSource, nullptr, this, nullptr);
	}

	this->mCaptureSource = captureSource;

	if (captureSource) {
		QObject::connect(
		    captureSource,
		    &QObject::destroyed,
		    this,
		    &ScreencopyView::onCaptureSourceDestroyed
		);

		if (this->completed) this->createContext();
	}

	if (!this->mHandle && hadContext) this->update();
	emit this->captureSourceChanged();
}

void ScreencopyView::onCaptureSourceDestroyed() {
	this->mCaptureSource = nullptr;
	this->destroyContext();
}

void ScreencopyView::setPaintCursors(bool paintCursors) {
	if (paintCursors == this->mPaintCursors) return;
	this->mPaintCursors = paintCursors;
	if (this->mHandle) this->mHandle->setCursor(paintCursors);
	emit this->paintCursorsChanged();
}

void ScreencopyView::setLive(bool live) {
	if (live == this->mLive) return;
	this->mLive = live;

	if (this->mHandle) {
		this->mHandle->setLive(live);
		if (live) this->wantFrame = true;
		this->syncSession();
	}

	emit this->liveChanged();
}

void ScreencopyView::createContext() {
	this->destroyContext(false);

	CaptureTarget target;

	if (auto* screen = qobject_cast<QuickshellScreenInfo*>(this->mCaptureSource)) {
		if (screen->screen != nullptr) target.monitor = qs::windows::monitorForScreen(screen->screen);
	} else if (auto* toplevel = qobject_cast<toplevel::Toplevel*>(this->mCaptureSource)) {
		if (toplevel->window() != nullptr) target.window = toplevel->window()->hwnd();
		this->toplevel = toplevel;
	}

	if (!target.valid()) {
		this->toplevel = nullptr;
		qmlWarning(this) << "Capture source set to non captureable object.";
		return;
	}

	CaptureOptions options;
	options.cursor = this->mPaintCursors;
	options.live = this->mLive;
	options.maxFps = LIVE_FPS;

	this->mHandle = new CaptureHandle(target, options, this);

	QObject::connect(this->mHandle, &CaptureHandle::frameReady, this, &ScreencopyView::onFrameReady);
	QObject::connect(this->mHandle, &CaptureHandle::stopped, this, &ScreencopyView::onCaptureStopped);

	if (this->toplevel) {
		QObject::connect(
		    this->toplevel,
		    &toplevel::Toplevel::minimizedChanged,
		    this,
		    &ScreencopyView::syncSession
		);
	}

	this->wantFrame = true;
	this->syncSession();
}

void ScreencopyView::destroyContext(bool update) {
	auto hadContext = this->mHandle != nullptr;

	if (this->toplevel) {
		QObject::disconnect(this->toplevel, nullptr, this, nullptr);
		this->toplevel = nullptr;
	}

	if (this->mHandle) {
		this->mHandle->stop();
		QObject::disconnect(this->mHandle, nullptr, this, nullptr);
		this->mHandle->deleteLater();
		this->mHandle = nullptr;
	}

	this->wantFrame = false;
	this->mHasFrame = false;
	this->bHasContent = false;
	this->bSourceSize = QSize();
	if (hadContext && update) this->update();
}

void ScreencopyView::captureFrame() {
	if (!this->mHandle) {
		qmlWarning(this) << "Cannot capture frame, as no recording context is ready.";
		return;
	}

	if (this->mLive) return;
	this->wantFrame = true;
	this->syncSession();
}

void ScreencopyView::syncSession() {
	if (!this->mHandle) return;

	auto minimized = this->toplevel != nullptr && this->toplevel->minimized();
	auto* window = this->window();
	auto shown = this->isVisible() && window != nullptr && window->isVisible();
	auto active = this->completed && shown && !minimized && (this->mLive || this->wantFrame);

	if (active) this->mHandle->start();
	else this->mHandle->stop();

	this->bHasContent = this->mHasFrame && !minimized;
}

void ScreencopyView::onFrameReady() {
	if (!this->mHandle) return;

	auto frame = this->mHandle->latestFrame();
	if (!frame) return;

	this->wantFrame = false;
	this->mHasFrame = true;
	this->bSourceSize = frame->size;
	this->setFlag(QQuickItem::ItemHasContents);
	this->update();
	this->syncSession();
}

void ScreencopyView::onCaptureStopped(bool error) {
	if (error) qmlWarning(this) << "Capture failed; see the quickshell.windows.capture log.";
	this->destroyContext();
	emit this->stopped();
}

void ScreencopyView::componentComplete() {
	this->QQuickItem::componentComplete();
	this->completed = true;
	if (this->mCaptureSource) this->createContext();
}

void ScreencopyView::watchWindow(QQuickWindow* window) {
	if (window == this->watchedWindow) return;

	if (this->watchedWindow) {
		QObject::disconnect(this->watchedWindow, nullptr, this, nullptr);
	}

	this->watchedWindow = window;

	if (window) {
		QObject::connect(window, &QWindow::visibleChanged, this, &ScreencopyView::syncSession);
	}
}

void ScreencopyView::itemChange(ItemChange change, const ItemChangeData& value) {
	this->QQuickItem::itemChange(change, value);

	if (change == QQuickItem::ItemVisibleHasChanged) {
		this->syncSession();
	} else if (change == QQuickItem::ItemSceneChange) {
		this->watchWindow(value.window);
		this->syncSession();
	}
}

QSGNode* ScreencopyView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*unused*/) {
	if (!this->mHandle || !this->mHasFrame) {
		delete oldNode;
		this->setFlag(QQuickItem::ItemHasContents, false);
		return nullptr;
	}

	auto* node = static_cast<CaptureNode*>(oldNode); // NOLINT
	if (!node) node = new CaptureNode(this->window());

	quint64 serial = 0;
	auto frame = this->mHandle->latestFrame(&serial);

	if (!node->sync(frame, serial)) this->update();

	if (!node->hasTexture()) {
		delete node;
		return nullptr;
	}

	node->setRect(this->boundingRect());
	return node;
}

void ScreencopyView::updateImplicitSize() {
	auto size = this->bImplicitSize.value();
	this->setImplicitSize(size.width(), size.height());
}

} // namespace qs::wayland::screencopy
