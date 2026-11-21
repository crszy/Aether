#include "capture.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>

#include <qt_windows.h>

#include <d3d11_1.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <inspectable.h>
#include <unknwn.h>

#include <qcoreapplication.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qthread.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using winrt::Windows::Foundation::TimeSpan;
using winrt::Windows::Foundation::Metadata::ApiInformation;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapabilityAccessStatus;
using DxgiInterfaceAccess = ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;

namespace qs::windows::capture {

namespace {
Q_LOGGING_CATEGORY(logCapture, "quickshell.windows.capture", QtWarningMsg);

constexpr DXGI_FORMAT FRAME_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DirectXPixelFormat FRAME_PIXEL_FORMAT = DirectXPixelFormat::R8G8B8A8UIntNormalized;
constexpr int POOL_BUFFERS = 2;

constexpr UINT64 KEY_PRODUCER = 0;
constexpr UINT64 KEY_CONSUMER = 1;

QString hresultString(const winrt::hresult_error& e) {
	return QString("0x%1 %2")
	    .arg(static_cast<quint32>(e.code().value), 8, 16, QChar('0'))
	    .arg(QString::fromWCharArray(e.message().c_str()));
}

template <typename T>
winrt::com_ptr<T> textureOf(const IDirect3DSurface& surface) {
	auto access = surface.as<DxgiInterfaceAccess>();
	winrt::com_ptr<T> result;
	winrt::check_hresult(access->GetInterface(winrt::guid_of<T>(), result.put_void()));
	return result;
}

bool sizeEquals(const SizeInt32& a, const SizeInt32& b) {
	return a.Width == b.Width && a.Height == b.Height;
}

std::shared_ptr<SharedFrame> createSharedFrame(ID3D11Device* device, const SizeInt32& size) {
	auto frame = std::make_shared<SharedFrame>();
	frame->size = QSize(size.Width, size.Height);

	D3D11_TEXTURE2D_DESC desc {};
	desc.Width = static_cast<UINT>(size.Width);
	desc.Height = static_cast<UINT>(size.Height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = FRAME_FORMAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	auto hr = device->CreateTexture2D(&desc, nullptr, &frame->texture);
	if (FAILED(hr)) {
		qCWarning(logCapture) << "CreateTexture2D (shared frame) failed:" << Qt::hex << hr;
		return nullptr;
	}

	IDXGIResource1* resource = nullptr;
	hr = frame->texture->QueryInterface(IID_PPV_ARGS(&resource));
	if (SUCCEEDED(hr)) {
		hr = resource->CreateSharedHandle(
		    nullptr,
		    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
		    nullptr,
		    &frame->handle
		);
		resource->Release();
	}

	if (SUCCEEDED(hr)) hr = frame->texture->QueryInterface(IID_PPV_ARGS(&frame->mutex));

	if (FAILED(hr)) {
		qCWarning(logCapture) << "Exporting the shared frame failed:" << Qt::hex << hr;
		return nullptr;
	}

	return frame;
}

} // namespace

SharedFrame::~SharedFrame() {
	if (this->mutex != nullptr) this->mutex->Release();
	if (this->texture != nullptr) this->texture->Release();
	if (this->handle != nullptr) CloseHandle(this->handle);
}

SharedFrameReader::~SharedFrameReader() { this->close(); }

void SharedFrameReader::close() {
	if (this->mutex != nullptr) this->mutex->Release();
	if (this->opened != nullptr) this->opened->Release();
	this->mutex = nullptr;
	this->opened = nullptr;
	this->mFrame = nullptr;
}

bool SharedFrameReader::open(ID3D11Device* device, const std::shared_ptr<SharedFrame>& frame) {
	this->close();
	if (!frame || frame->handle == nullptr) return false;

	ID3D11Device1* device1 = nullptr;
	auto hr = device->QueryInterface(IID_PPV_ARGS(&device1));
	if (FAILED(hr)) {
		qCWarning(logCapture) << "Consumer device is not a D3D11.1 device:" << Qt::hex << hr;
		return false;
	}

	hr = device1->OpenSharedResource1(frame->handle, IID_PPV_ARGS(&this->opened));
	device1->Release();

	if (SUCCEEDED(hr)) hr = this->opened->QueryInterface(IID_PPV_ARGS(&this->mutex));

	if (FAILED(hr)) {
		qCWarning(logCapture) << "Opening the shared frame on the consumer device failed:" << Qt::hex
		                      << hr;
		this->close();
		return false;
	}

	this->mFrame = frame;
	return true;
}

bool SharedFrameReader::copyTo(ID3D11DeviceContext* context, ID3D11Texture2D* dest) {
	if (this->mutex == nullptr) return false;

	auto hr = this->mutex->AcquireSync(KEY_CONSUMER, 0);
	if (hr != S_OK) hr = this->mutex->AcquireSync(KEY_PRODUCER, 0);
	if (hr != S_OK) return false;

	context->CopyResource(dest, this->opened);
	this->mutex->ReleaseSync(KEY_PRODUCER);
	context->Flush();
	return true;
}

class CaptureWorker: public QObject {
	Q_OBJECT;

public:
	CaptureWorker() = default;
	~CaptureWorker() override = default;
	Q_DISABLE_COPY_MOVE(CaptureWorker);

	[[nodiscard]] bool ready() const { return this->mReady; }
	[[nodiscard]] const winrt::com_ptr<ID3D11Device>& device() const { return this->mDevice; }
	[[nodiscard]] const winrt::com_ptr<ID3D11DeviceContext>& context() const { return this->mContext; }
	[[nodiscard]] const IDirect3DDevice& winrtDevice() const { return this->mWinrtDevice; }

	GraphicsCaptureItem createItem(const CaptureTarget& target);
	bool configure(const GraphicsCaptureSession& session, const CaptureOptions& options);

	QImage grabMonitor(HMONITOR monitor, int timeoutMs);

	void registerSession(CaptureSession* session) { this->sessions.append(session); }
	void unregisterSession(CaptureSession* session) { this->sessions.removeOne(session); }

public slots:
	void start();
	void shutdown();

private:
	bool mReady = false;
	bool apartment = false;
	bool borderless = false;
	bool hasBorderProperty = false;
	bool hasCursorProperty = false;
	bool hasMinUpdateInterval = false;
	winrt::com_ptr<ID3D11Device> mDevice;
	winrt::com_ptr<ID3D11DeviceContext> mContext;
	IDirect3DDevice mWinrtDevice {nullptr};
	QList<CaptureSession*> sessions;
};

void CaptureWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->apartment = true;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "init_apartment(multi_threaded) failed:" << hresultString(e);
		return;
	}

	try {
		if (!GraphicsCaptureSession::IsSupported()) {
			qCWarning(logCapture) << "Windows.Graphics.Capture is not supported on this system.";
			return;
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "GraphicsCaptureSession::IsSupported failed:" << hresultString(e);
		return;
	}

	auto hr = D3D11CreateDevice(
	    nullptr,
	    D3D_DRIVER_TYPE_HARDWARE,
	    nullptr,
	    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
	    nullptr,
	    0,
	    D3D11_SDK_VERSION,
	    this->mDevice.put(),
	    nullptr,
	    this->mContext.put()
	);

	if (FAILED(hr)) {
		qCWarning(logCapture) << "D3D11CreateDevice failed:" << Qt::hex << hr;
		return;
	}

	if (auto multithread = this->mContext.try_as<ID3D11Multithread>()) {
		multithread->SetMultithreadProtected(TRUE);
	}

	try {
		auto dxgiDevice = this->mDevice.as<IDXGIDevice>();
		winrt::com_ptr<::IInspectable> inspectable;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put()));
		this->mWinrtDevice = inspectable.as<IDirect3DDevice>();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "CreateDirect3D11DeviceFromDXGIDevice failed:" << hresultString(e);
		return;
	}

	const auto sessionClass = L"Windows.Graphics.Capture.GraphicsCaptureSession";
	try {
		this->hasBorderProperty = ApiInformation::IsPropertyPresent(sessionClass, L"IsBorderRequired");
		this->hasCursorProperty =
		    ApiInformation::IsPropertyPresent(sessionClass, L"IsCursorCaptureEnabled");
		this->hasMinUpdateInterval =
		    ApiInformation::IsPropertyPresent(sessionClass, L"MinUpdateInterval");
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "ApiInformation queries failed:" << hresultString(e);
	}

	if (this->hasBorderProperty
	    && ApiInformation::IsTypePresent(L"Windows.Graphics.Capture.GraphicsCaptureAccess"))
	{
		try {
			auto status =
			    GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless).get();
			this->borderless = status == AppCapabilityAccessStatus::Allowed;
			qCInfo(logCapture) << "Borderless capture access:" << static_cast<int>(status)
			                   << (this->borderless ? "(allowed)" : "(not allowed)");
		} catch (const winrt::hresult_error& e) {
			qCWarning(logCapture) << "RequestAccessAsync(Borderless) failed:" << hresultString(e);
		}
	}

	qCInfo(logCapture) << "Capture worker ready; MinUpdateInterval" << this->hasMinUpdateInterval
	                   << "cursor" << this->hasCursorProperty;
	this->mReady = true;
}

GraphicsCaptureItem CaptureWorker::createItem(const CaptureTarget& target) {
	auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
	GraphicsCaptureItem item {nullptr};

	if (target.window != nullptr) {
		winrt::check_hresult(
		    factory->CreateForWindow(target.window, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item))
		);
	} else {
		winrt::check_hresult(
		    factory->CreateForMonitor(target.monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item))
		);
	}

	return item;
}

bool CaptureWorker::configure(const GraphicsCaptureSession& session, const CaptureOptions& options) {
	if (this->hasBorderProperty) {
		try {
			session.IsBorderRequired(false);
		} catch (const winrt::hresult_error& e) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				qCWarning(logCapture) << "IsBorderRequired(false) refused:" << hresultString(e);
			}
		}
	}

	if (this->hasCursorProperty) {
		try {
			session.IsCursorCaptureEnabled(options.cursor);
		} catch (const winrt::hresult_error& e) {
			qCWarning(logCapture) << "IsCursorCaptureEnabled failed:" << hresultString(e);
		}
	}

	if (options.live && options.maxFps > 0 && this->hasMinUpdateInterval) {
		try {
			session.MinUpdateInterval(std::chrono::milliseconds(1000 / options.maxFps));
			return true;
		} catch (const winrt::hresult_error& e) {
			qCWarning(logCapture) << "MinUpdateInterval failed:" << hresultString(e);
		}
	}

	return false;
}

QImage gdiGrabMonitor(HMONITOR monitor);

QImage CaptureWorker::grabMonitor(HMONITOR monitor, int timeoutMs) {
	if (!this->mReady) return gdiGrabMonitor(monitor);

	struct Grab {
		std::mutex mutex;
		std::condition_variable cv;
		bool done = false;
		QImage image;
	};

	auto grab = std::make_shared<Grab>();

	try {
		auto item = this->createItem({.monitor = monitor});
		auto pool =
		    Direct3D11CaptureFramePool::CreateFreeThreaded(this->mWinrtDevice, FRAME_PIXEL_FORMAT, 1, item.Size());
		auto session = pool.CreateCaptureSession(item);
		this->configure(session, {.cursor = false, .live = false});

		auto device = this->mDevice;
		auto context = this->mContext;

		auto token = pool.FrameArrived([grab, device, context](auto const& sender, auto const&) {
			std::lock_guard lock(grab->mutex);
			if (grab->done) return;

			try {
				auto frame = sender.TryGetNextFrame();
				if (!frame) return;

				auto content = frame.ContentSize();
				auto source = textureOf<ID3D11Texture2D>(frame.Surface());

				D3D11_TEXTURE2D_DESC desc {};
				source->GetDesc(&desc);
				desc.Width = static_cast<UINT>(content.Width);
				desc.Height = static_cast<UINT>(content.Height);
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.MiscFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

				winrt::com_ptr<ID3D11Texture2D> staging;
				winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));

				D3D11_BOX box {0, 0, 0, desc.Width, desc.Height, 1};
				context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, source.get(), 0, &box);

				D3D11_MAPPED_SUBRESOURCE mapped {};
				winrt::check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));

				auto image = QImage(content.Width, content.Height, QImage::Format_RGBX8888);
				auto rowBytes = static_cast<size_t>(content.Width) * 4;
				for (int y = 0; y < content.Height; ++y) {
					std::memcpy(
					    image.scanLine(y),
					    static_cast<const char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
					    rowBytes
					);
				}

				context->Unmap(staging.get(), 0);
				frame.Close();
				grab->image = image;
			} catch (const winrt::hresult_error& e) {
				qCWarning(logCapture) << "Monitor readback failed:" << hresultString(e);
			}

			grab->done = true;
			grab->cv.notify_all();
		});

		session.StartCapture();

		{
			std::unique_lock lock(grab->mutex);
			grab->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return grab->done; });
			grab->done = true;
		}

		pool.FrameArrived(token);
		session.Close();
		pool.Close();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "Monitor capture failed:" << hresultString(e) << "- using a GDI copy instead";
	}

	// Windows Graphics Capture refused the monitor (seen as E_INVALIDARG on some setups) or gave no frame in time:
	// copy the monitor's pixels from the desktop DC instead (what the screen shows, without the cursor)
	if (grab->image.isNull()) grab->image = gdiGrabMonitor(monitor);
	return grab->image;
}

QImage gdiGrabMonitor(HMONITOR monitor) {
	MONITORINFO info {};
	info.cbSize = sizeof(info);
	if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return {};
	auto& r = info.rcMonitor;
	int w = r.right - r.left;
	int h = r.bottom - r.top;
	if (w <= 0 || h <= 0) return {};

	HDC screen = GetDC(nullptr);
	HDC mem = CreateCompatibleDC(screen);
	BITMAPINFO bmi {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = w;
	bmi.bmiHeader.biHeight = -h; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
	QImage image;
	if (bmp != nullptr && bits != nullptr) {
		auto* old = SelectObject(mem, bmp);
		if (BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT)) {
			image = QImage(static_cast<const uchar*>(bits), w, h, w * 4, QImage::Format_RGB32).copy();
		}
		SelectObject(mem, old);
	}
	if (bmp != nullptr) DeleteObject(bmp);
	DeleteDC(mem);
	ReleaseDC(nullptr, screen);
	return image;
}

struct SessionCore {
	std::mutex mutex;

	CaptureTarget target;
	CaptureOptions options;

	CaptureHandle* handle = nullptr;
	CaptureSession* session = nullptr;

	bool running = false;
	bool throttleFallback = false;
	bool hasLastTime = false;
	TimeSpan lastTime {};

	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	IDirect3DDevice winrtDevice {nullptr};

	GraphicsCaptureItem item {nullptr};
	Direct3D11CaptureFramePool pool {nullptr};
	GraphicsCaptureSession captureSession {nullptr};
	winrt::event_token frameToken {};
	winrt::event_token closedToken {};
	SizeInt32 poolSize {};

	std::shared_ptr<SharedFrame> slot;
	QSize slotFailedSize;
	std::shared_ptr<SharedFrame> latest;
	quint64 serial = 0;
};

class CaptureSession: public QObject {
	Q_OBJECT;

public:
	explicit CaptureSession(std::shared_ptr<SessionCore> core): core(std::move(core)) {
		this->core->session = this;
	}

	~CaptureSession() override {
		this->stop();
		CaptureThread::instance()->worker()->unregisterSession(this);
		std::lock_guard lock(this->core->mutex);
		this->core->session = nullptr;
	}

	Q_DISABLE_COPY_MOVE(CaptureSession);

public slots:
	void start();
	void stop();
	void setCursor(bool cursor);
	void setLive(bool live);

private:
	static void onFrameArrived(
	    const std::shared_ptr<SessionCore>& core,
	    const Direct3D11CaptureFramePool& sender
	);
	static void onClosed(const std::shared_ptr<SessionCore>& core);
	static void notifyStopped(SessionCore& core, bool error);

	std::shared_ptr<SessionCore> core;
	bool registered = false;
};

void CaptureSession::start() {
	auto& c = *this->core;
	auto* worker = CaptureThread::instance()->worker();

	{
		std::lock_guard lock(c.mutex);
		if (c.running) return;
	}

	if (!this->registered) {
		worker->registerSession(this);
		this->registered = true;
	}

	if (!worker->ready()) {
		qCWarning(logCapture) << "Capture requested but the capture worker is not ready.";
		CaptureSession::notifyStopped(c, true);
		return;
	}

	try {
		auto item = worker->createItem(c.target);
		auto size = item.Size();
		auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
		    worker->winrtDevice(),
		    FRAME_PIXEL_FORMAT,
		    POOL_BUFFERS,
		    size
		);
		auto session = pool.CreateCaptureSession(item);

		CaptureOptions options;
		{
			std::lock_guard lock(c.mutex);
			options = c.options;
		}

		auto throttled = worker->configure(session, options);
		auto core = this->core;

		{
			std::lock_guard lock(c.mutex);
			c.device = worker->device();
			c.context = worker->context();
			c.winrtDevice = worker->winrtDevice();
			c.item = item;
			c.pool = pool;
			c.captureSession = session;
			c.poolSize = size;
			c.throttleFallback = !throttled;
			c.hasLastTime = false;
			c.running = true;

			c.frameToken = pool.FrameArrived([core](auto const& sender, auto const&) {
				CaptureSession::onFrameArrived(core, sender);
			});

			c.closedToken = item.Closed([core](auto const&, auto const&) {
				CaptureSession::onClosed(core);
			});
		}

		session.StartCapture();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "Starting capture failed:" << hresultString(e);
		this->stop();
		CaptureSession::notifyStopped(c, true);
	}
}

void CaptureSession::stop() {
	auto& c = *this->core;

	GraphicsCaptureItem item {nullptr};
	Direct3D11CaptureFramePool pool {nullptr};
	GraphicsCaptureSession session {nullptr};
	winrt::event_token frameToken {};
	winrt::event_token closedToken {};

	{
		std::lock_guard lock(c.mutex);
		c.running = false;
		item = std::move(c.item);
		pool = std::move(c.pool);
		session = std::move(c.captureSession);
		frameToken = c.frameToken;
		closedToken = c.closedToken;
		c.item = nullptr;
		c.pool = nullptr;
		c.captureSession = nullptr;
		c.slot = nullptr;
		c.winrtDevice = nullptr;
	}

	try {
		if (pool) pool.FrameArrived(frameToken);
		if (item) item.Closed(closedToken);
		if (session) session.Close();
		if (pool) pool.Close();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "Stopping capture failed:" << hresultString(e);
	}
}

void CaptureSession::setCursor(bool cursor) {
	auto& c = *this->core;
	GraphicsCaptureSession session {nullptr};

	{
		std::lock_guard lock(c.mutex);
		c.options.cursor = cursor;
		session = c.captureSession;
	}

	if (!session) return;

	try {
		session.IsCursorCaptureEnabled(cursor);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logCapture) << "IsCursorCaptureEnabled failed:" << hresultString(e);
	}
}

void CaptureSession::setLive(bool live) {
	std::lock_guard lock(this->core->mutex);
	this->core->options.live = live;
	if (live && this->core->running) this->core->throttleFallback = true;
}

void CaptureSession::notifyStopped(SessionCore& core, bool error) {
	std::lock_guard lock(core.mutex);
	if (core.handle == nullptr) return;
	auto* handle = core.handle;
	QMetaObject::invokeMethod(handle, [handle, error] { handle->onSessionStopped(error); }, Qt::QueuedConnection);
}

void CaptureSession::onClosed(const std::shared_ptr<SessionCore>& core) {
	auto& c = *core;
	std::lock_guard lock(c.mutex);
	if (!c.running) return;
	c.running = false;

	if (c.session != nullptr) {
		QMetaObject::invokeMethod(c.session, &CaptureSession::stop, Qt::QueuedConnection);
	}

	if (c.handle != nullptr) {
		auto* handle = c.handle;
		QMetaObject::invokeMethod(handle, [handle] { handle->onSessionStopped(false); }, Qt::QueuedConnection);
	}
}

void CaptureSession::onFrameArrived(
    const std::shared_ptr<SessionCore>& core,
    const Direct3D11CaptureFramePool& sender
) {
	auto& c = *core;
	std::lock_guard lock(c.mutex);
	if (!c.running) return;

	try {
		auto frame = sender.TryGetNextFrame();
		if (!frame) return;

		auto content = frame.ContentSize();
		auto time = frame.SystemRelativeTime();
		auto published = false;

		auto throttle = c.options.live && c.throttleFallback && c.options.maxFps > 0 && c.hasLastTime
		             && (time - c.lastTime) < std::chrono::milliseconds(1000 / c.options.maxFps);

		if (!throttle && content.Width > 0 && content.Height > 0) {
			auto source = textureOf<ID3D11Texture2D>(frame.Surface());
			D3D11_TEXTURE2D_DESC desc {};
			source->GetDesc(&desc);

			auto fits = static_cast<UINT>(content.Width) <= desc.Width
			         && static_cast<UINT>(content.Height) <= desc.Height;

			if (fits) {
				auto size = QSize(content.Width, content.Height);
				if (!c.slot || c.slot->size != size) {
					c.slot = nullptr;
					if (c.slotFailedSize != size) {
						c.slot = createSharedFrame(c.device.get(), content);
						if (!c.slot) c.slotFailedSize = size;
					}
				}

				if (c.slot && c.slot->mutex->AcquireSync(KEY_PRODUCER, 0) == S_OK) {
					D3D11_BOX box {0, 0, 0, static_cast<UINT>(content.Width), static_cast<UINT>(content.Height), 1};
					c.context->CopySubresourceRegion(c.slot->texture, 0, 0, 0, 0, source.get(), 0, &box);
					c.slot->mutex->ReleaseSync(KEY_CONSUMER);
					c.context->Flush();

					c.latest = c.slot;
					c.serial++;
					c.lastTime = time;
					c.hasLastTime = true;
					published = true;
				}
			}
		}

		frame.Close();

		if (!sizeEquals(content, c.poolSize) && content.Width > 0 && content.Height > 0) {
			sender.Recreate(c.winrtDevice, FRAME_PIXEL_FORMAT, POOL_BUFFERS, content);
			c.poolSize = content;
		}

		if (!published) return;

		if (c.handle != nullptr) {
			auto* handle = c.handle;
			auto live = c.options.live;
			QMetaObject::invokeMethod(handle, [handle, live] { handle->onFrame(live); }, Qt::QueuedConnection);
		}

		if (!c.options.live) {
			c.running = false;
			if (c.session != nullptr) {
				QMetaObject::invokeMethod(c.session, &CaptureSession::stop, Qt::QueuedConnection);
			}
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logCapture) << "Frame callback failed:" << hresultString(e);
	}
}

void CaptureWorker::shutdown() {
	for (auto* session: this->sessions) {
		session->stop();
	}

	this->mReady = false;
	this->mWinrtDevice = nullptr;
	this->mContext = nullptr;
	this->mDevice = nullptr;

	if (this->apartment) {
		winrt::uninit_apartment();
		this->apartment = false;
	}
}

CaptureHandle::CaptureHandle(
    const CaptureTarget& target,
    const CaptureOptions& options,
    QObject* parent
)
    : QObject(parent)
    , core(std::make_shared<SessionCore>()) {
	this->core->target = target;
	this->core->options = options;
	this->core->handle = this;

	this->session = new CaptureSession(this->core);
	this->session->moveToThread(CaptureThread::instance()->thread());
}

CaptureHandle::~CaptureHandle() {
	{
		std::lock_guard lock(this->core->mutex);
		this->core->handle = nullptr;
	}

	this->session->deleteLater();
}

void CaptureHandle::start() {
	if (this->mRunning) return;
	this->mRunning = true;
	auto* session = this->session;
	QMetaObject::invokeMethod(session, &CaptureSession::start, Qt::QueuedConnection);
}

void CaptureHandle::stop() {
	if (!this->mRunning) return;
	this->mRunning = false;
	auto* session = this->session;
	QMetaObject::invokeMethod(session, &CaptureSession::stop, Qt::QueuedConnection);
}

void CaptureHandle::setCursor(bool cursor) {
	auto* session = this->session;
	QMetaObject::invokeMethod(session, [session, cursor] { session->setCursor(cursor); }, Qt::QueuedConnection);
}

void CaptureHandle::setLive(bool live) {
	auto* session = this->session;
	QMetaObject::invokeMethod(session, [session, live] { session->setLive(live); }, Qt::QueuedConnection);
}

std::shared_ptr<SharedFrame> CaptureHandle::latestFrame(quint64* serial) const {
	std::lock_guard lock(this->core->mutex);
	if (serial != nullptr) *serial = this->core->serial;
	return this->core->latest;
}

void CaptureHandle::onFrame(bool live) {
	if (!live) this->mRunning = false;
	emit this->frameReady();
}

void CaptureHandle::onSessionStopped(bool error) {
	this->mRunning = false;
	emit this->stopped(error);
}

CaptureThread* CaptureThread::instance() {
	static auto* instance = new CaptureThread(); // NOLINT
	return instance;
}

CaptureThread::CaptureThread() {
	this->mWorker = new CaptureWorker();
	this->mWorker->moveToThread(&this->mThread);
	this->mThread.setObjectName("quickshell-capture");

	QObject::connect(&this->mThread, &QThread::started, this->mWorker, &CaptureWorker::start);
	QObject::connect(&this->mThread, &QThread::finished, this->mWorker, &CaptureWorker::shutdown);

	if (auto* app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, this, [this] {
			this->mThread.quit();
			this->mThread.wait();
		});
	}

	this->mThread.start();
}

CaptureThread::~CaptureThread() {
	this->mThread.quit();
	this->mThread.wait();
	delete this->mWorker;
}

QImage CaptureThread::grabMonitor(HMONITOR monitor, int timeoutMs) {
	if (!this->mThread.isRunning()) return {};
	if (QThread::currentThread() == &this->mThread) return this->mWorker->grabMonitor(monitor, timeoutMs);

	struct Result {
		std::mutex mutex;
		std::condition_variable cv;
		bool done = false;
		QImage image;
	};
	auto result = std::make_shared<Result>();

	auto* worker = this->mWorker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, monitor, timeoutMs, result] {
		    auto image = worker->grabMonitor(monitor, timeoutMs);
		    std::lock_guard lock(result->mutex);
		    result->image = std::move(image);
		    result->done = true;
		    result->cv.notify_all();
	    },
	    Qt::QueuedConnection
	);

	std::unique_lock lock(result->mutex);
	result->cv.wait_for(
	    lock,
	    std::chrono::milliseconds(timeoutMs) + std::chrono::milliseconds(500),
	    [&] { return result->done; }
	);
	return result->done ? result->image : QImage();
}

} // namespace qs::windows::capture

#include "capture.moc"
