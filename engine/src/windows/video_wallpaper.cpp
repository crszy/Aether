#include "video_wallpaper.hpp"

#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include <qt_windows.h>

#include <d3d10.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <oleauto.h>

#include <qfileinfo.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qobject.h>
#include <qquickitem.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qsgrendererinterface.h>
#include <qsgsimpletexturenode.h>
#include <qsgtexture.h>
#include <qtimer.h>
#include <private/qsgplaintexture_p.h>
#include <qurl.h>
#include <rhi/qrhi.h>

#include <winrt/base.h>

#include "capture.hpp"

namespace qs::windows::video {

namespace {
Q_LOGGING_CATEGORY(logVideo, "quickshell.windows.video", QtWarningMsg);

constexpr UINT64 KEY_PRODUCER = 0;
constexpr UINT64 KEY_CONSUMER = 1;
constexpr int TICK_MS = 15;
constexpr int SOFTWARE_TICK_MS = 66;
constexpr int MAX_WIDTH = 3840;
constexpr int MAX_HEIGHT = 2160;
constexpr int SOFTWARE_MAX_WIDTH = 1280;
constexpr int SOFTWARE_MAX_HEIGHT = 720;
constexpr UINT MICROSOFT_VENDOR_ID = 0x1414;
constexpr unsigned long STOP_TIMEOUT_MS = 3000;

bool isSoftwareDevice(ID3D11Device* device) {
	winrt::com_ptr<IDXGIDevice> dxgi;
	winrt::com_ptr<IDXGIAdapter> adapter;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi.put()))) || FAILED(dxgi->GetAdapter(adapter.put()))) {
		return false;
	}

	auto adapter1 = adapter.try_as<IDXGIAdapter1>();
	DXGI_ADAPTER_DESC1 desc {};
	if (!adapter1 || FAILED(adapter1->GetDesc1(&desc))) return false;
	return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == MICROSOFT_VENDOR_ID;
}
constexpr DXGI_FORMAT FRAME_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;

QString hresultText(HRESULT hr) {
	return QStringLiteral("0x%1").arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
}

QString normalizedPath(const QString& source) {
	auto isUrl = source.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive);
	auto path = isUrl ? QUrl(source).toLocalFile() : source;
	return QFileInfo(path).absoluteFilePath();
}

std::shared_ptr<capture::SharedFrame> createFrame(ID3D11Device* device, const QSize& size) {
	auto frame = std::make_shared<capture::SharedFrame>();
	frame->size = size;

	D3D11_TEXTURE2D_DESC desc {};
	desc.Width = static_cast<UINT>(size.width());
	desc.Height = static_cast<UINT>(size.height());
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = FRAME_FORMAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	auto hr = device->CreateTexture2D(&desc, nullptr, &frame->texture);
	if (FAILED(hr)) {
		qCWarning(logVideo) << "CreateTexture2D (video frame) failed:" << hresultText(hr);
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
		qCWarning(logVideo) << "Exporting the video frame failed:" << hresultText(hr);
		return nullptr;
	}

	return frame;
}

QRectF coverRect(const QSize& frame, const QSizeF& target) {
	if (frame.isEmpty() || target.isEmpty()) return QRectF(QPointF(0, 0), frame);

	auto scale = std::max(target.width() / frame.width(), target.height() / frame.height());
	auto width = target.width() / scale;
	auto height = target.height() / scale;
	return QRectF((frame.width() - width) / 2, (frame.height() - height) / 2, width, height);
}

class EngineNotify;

} // namespace

class VideoWorker: public QObject {
	Q_OBJECT;

public:
	VideoWorker(QString path, std::shared_ptr<FrameSlot> slot)
	    : path(std::move(path))
	    , slot(std::move(slot)) {}
	~VideoWorker() override = default;
	Q_DISABLE_COPY_MOVE(VideoWorker);

	void onEngineEvent(DWORD event);

public slots:
	void start();
	void setPlaying(bool playing);
	void shutdown();

signals:
	void ready();
	void failed(const QString& error);
	void frameReady();

private:
	bool createDevice();
	void resizeFrame();
	void tick();
	void fail(const QString& what, HRESULT hr);

	QString path;
	std::shared_ptr<FrameSlot> slot;
	bool comReady = false;
	bool mfReady = false;
	bool loaded = false;
	bool wantPlaying = false;
	bool warnedTransfer = false;
	bool software = false;
	winrt::com_ptr<ID3D11Device> device;
	winrt::com_ptr<ID3D11DeviceContext> context;
	winrt::com_ptr<IMFDXGIDeviceManager> manager;
	winrt::com_ptr<IMFMediaEngine> engine;
	EngineNotify* notify = nullptr;
	std::shared_ptr<capture::SharedFrame> frame;
	QTimer* timer = nullptr;
	LONGLONG lastPts = -1;
};

namespace {

class EngineNotify: public IMFMediaEngineNotify {
public:
	explicit EngineNotify(VideoWorker* target): target(target) {}

	STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
		if (object == nullptr) return E_POINTER;
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
			*object = static_cast<IMFMediaEngineNotify*>(this);
			this->AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override { return ++this->refs; }

	STDMETHODIMP_(ULONG) Release() override {
		auto refs = --this->refs;
		if (refs == 0) delete this;
		return refs;
	}

	STDMETHODIMP EventNotify(DWORD event, DWORD_PTR /*param1*/, DWORD /*param2*/) override {
		std::lock_guard lock(this->mutex);
		if (this->target == nullptr) return S_OK;

		QMetaObject::invokeMethod(
		    this->target,
		    [target = this->target, event] { target->onEngineEvent(event); },
		    Qt::QueuedConnection
		);
		return S_OK;
	}

	void detach() {
		std::lock_guard lock(this->mutex);
		this->target = nullptr;
	}

private:
	virtual ~EngineNotify() = default;

	std::atomic<ULONG> refs = 1;
	std::mutex mutex;
	VideoWorker* target;
};

} // namespace

bool VideoWorker::createDevice() {
	const D3D_FEATURE_LEVEL levels[] = {
	    D3D_FEATURE_LEVEL_11_1,
	    D3D_FEATURE_LEVEL_11_0,
	    D3D_FEATURE_LEVEL_10_1,
	    D3D_FEATURE_LEVEL_10_0,
	};

	for (auto type: {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
		auto hr = D3D11CreateDevice(
		    nullptr,
		    type,
		    nullptr,
		    D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		    levels,
		    static_cast<UINT>(std::size(levels)),
		    D3D11_SDK_VERSION,
		    this->device.put(),
		    nullptr,
		    this->context.put()
		);

		if (SUCCEEDED(hr)) {
			this->software = type == D3D_DRIVER_TYPE_WARP || isSoftwareDevice(this->device.get());
			if (this->software) {
				qCInfo(logVideo) << "No GPU for video; playing at up to 720p and 15 fps";
			}
			if (auto multithread = this->device.try_as<ID3D10Multithread>()) {
				multithread->SetMultithreadProtected(TRUE);
			}
			return true;
		}

		qCInfo(logVideo) << "D3D11CreateDevice failed for driver type" << type << ":" << hresultText(hr);
	}

	return false;
}

void VideoWorker::start() {
	this->comReady = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

	auto hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return this->fail(QStringLiteral("MFStartup"), hr);
	this->mfReady = true;

	if (!this->createDevice()) return this->fail(QStringLiteral("D3D11CreateDevice"), E_FAIL);

	UINT resetToken = 0;
	hr = MFCreateDXGIDeviceManager(&resetToken, this->manager.put());
	if (SUCCEEDED(hr)) hr = this->manager->ResetDevice(this->device.get(), resetToken);
	if (FAILED(hr)) return this->fail(QStringLiteral("MFCreateDXGIDeviceManager"), hr);

	winrt::com_ptr<IMFMediaEngineClassFactory> factory;
	hr = CoCreateInstance(
	    CLSID_MFMediaEngineClassFactory,
	    nullptr,
	    CLSCTX_INPROC_SERVER,
	    IID_PPV_ARGS(factory.put())
	);
	if (FAILED(hr)) return this->fail(QStringLiteral("CoCreateInstance(MediaEngineClassFactory)"), hr);

	winrt::com_ptr<IMFAttributes> attributes;
	hr = MFCreateAttributes(attributes.put(), 3);
	if (FAILED(hr)) return this->fail(QStringLiteral("MFCreateAttributes"), hr);

	this->notify = new EngineNotify(this);
	attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, this->manager.get());
	attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, this->notify);
	attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, FRAME_FORMAT);

	hr = factory->CreateInstance(0, attributes.get(), this->engine.put());
	if (FAILED(hr)) return this->fail(QStringLiteral("MediaEngine CreateInstance"), hr);

	this->engine->SetMuted(TRUE);
	this->engine->SetLoop(TRUE);
	this->engine->SetAutoPlay(FALSE);

	this->timer = new QTimer(this);
	this->timer->setTimerType(Qt::PreciseTimer);
	this->timer->setInterval(this->software ? SOFTWARE_TICK_MS : TICK_MS);
	QObject::connect(this->timer, &QTimer::timeout, this, &VideoWorker::tick);

	auto url = QUrl::fromLocalFile(this->path).toString().toStdWString();
	auto* source = SysAllocString(url.c_str());
	hr = this->engine->SetSource(source);
	SysFreeString(source);
	if (FAILED(hr)) return this->fail(QStringLiteral("SetSource"), hr);
}

void VideoWorker::onEngineEvent(DWORD event) {
	if (!this->engine) return;

	switch (event) {
	case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
	case MF_MEDIA_ENGINE_EVENT_FORMATCHANGE:
		this->resizeFrame();
		if (!this->loaded) {
			this->loaded = true;
			emit this->ready();
		}
		this->setPlaying(this->wantPlaying);
		break;
	case MF_MEDIA_ENGINE_EVENT_ERROR: {
		winrt::com_ptr<IMFMediaError> error;
		this->engine->GetError(error.put());
		auto code = error ? error->GetErrorCode() : 0;
		auto extended = error ? error->GetExtendedErrorCode() : E_FAIL;
		auto message = QStringLiteral("Can't play %1 (media error %2, %3)")
		                   .arg(this->path)
		                   .arg(code)
		                   .arg(hresultText(extended));
		qCWarning(logVideo).noquote() << message;
		emit this->failed(message);
		break;
	}
	default: break;
	}
}

void VideoWorker::resizeFrame() {
	DWORD width = 0;
	DWORD height = 0;
	if (FAILED(this->engine->GetNativeVideoSize(&width, &height)) || width == 0 || height == 0) return;

	auto size = QSize(static_cast<int>(width), static_cast<int>(height));
	auto maxWidth = this->software ? SOFTWARE_MAX_WIDTH : MAX_WIDTH;
	auto maxHeight = this->software ? SOFTWARE_MAX_HEIGHT : MAX_HEIGHT;
	if (size.width() > maxWidth || size.height() > maxHeight) {
		size = size.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio);
	}

	if (this->frame && this->frame->size == size) return;

	this->frame = createFrame(this->device.get(), size);
	this->lastPts = -1;
}

void VideoWorker::setPlaying(bool playing) {
	this->wantPlaying = playing;
	if (!this->engine || !this->loaded) return;

	if (playing) {
		this->engine->Play();
		this->timer->start();
	} else {
		this->engine->Pause();
		this->timer->stop();
	}
}

void VideoWorker::tick() {
	if (!this->engine || !this->frame) return;

	LONGLONG pts = 0;
	if (this->engine->OnVideoStreamTick(&pts) != S_OK || pts == this->lastPts) return;
	if (this->frame->mutex->AcquireSync(KEY_PRODUCER, 0) != S_OK) return;

	auto size = this->frame->size;
	MFVideoNormalizedRect source {0.0F, 0.0F, 1.0F, 1.0F};
	RECT target {0, 0, size.width(), size.height()};
	MFARGB border {0, 0, 0, 255};
	auto hr = this->engine->TransferVideoFrame(this->frame->texture, &source, &target, &border);
	this->frame->mutex->ReleaseSync(KEY_CONSUMER);
	this->context->Flush();

	if (FAILED(hr)) {
		if (!this->warnedTransfer) {
			this->warnedTransfer = true;
			qCWarning(logVideo) << "TransferVideoFrame failed:" << hresultText(hr);
		}
		return;
	}

	this->lastPts = pts;

	{
		std::lock_guard lock(this->slot->mutex);
		this->slot->frame = this->frame;
		this->slot->serial++;
	}

	emit this->frameReady();
}

void VideoWorker::fail(const QString& what, HRESULT hr) {
	auto message = QStringLiteral("%1 failed: %2").arg(what, hresultText(hr));
	qCWarning(logVideo).noquote() << "Video wallpaper" << this->path << ":" << message;
	emit this->failed(message);
}

void VideoWorker::shutdown() {
	if (this->timer != nullptr) this->timer->stop();
	if (this->notify != nullptr) this->notify->detach();
	if (this->engine) this->engine->Shutdown();
	this->engine = nullptr;
	this->frame = nullptr;

	{
		std::lock_guard lock(this->slot->mutex);
		this->slot->frame = nullptr;
	}

	this->manager = nullptr;
	this->context = nullptr;
	this->device = nullptr;

	if (this->notify != nullptr) {
		this->notify->Release();
		this->notify = nullptr;
	}

	if (this->mfReady) MFShutdown();
	this->mfReady = false;
	if (this->comReady) CoUninitialize();
	this->comReady = false;
}

VideoSource::VideoSource(QString path)
    : mPath(std::move(path))
    , thread(std::make_unique<QThread>())
    , slot(std::make_shared<FrameSlot>()) {
	this->worker = new VideoWorker(this->mPath, this->slot);
	this->worker->moveToThread(this->thread.get());

	QObject::connect(this->thread.get(), &QThread::started, this->worker, &VideoWorker::start);
	QObject::connect(this->thread.get(), &QThread::finished, this->worker, &VideoWorker::shutdown);
	QObject::connect(this->worker, &VideoWorker::ready, this, &VideoSource::onReady);
	QObject::connect(this->worker, &VideoWorker::failed, this, &VideoSource::onFailed);
	QObject::connect(this->worker, &VideoWorker::frameReady, this, &VideoSource::frameReady);

	this->thread->setObjectName(QStringLiteral("qs-video"));
	this->thread->start();
}

VideoSource::~VideoSource() {
	QObject::disconnect(this->worker, nullptr, this, nullptr);

	this->thread->quit();
	if (!this->thread->wait(STOP_TIMEOUT_MS)) {
		qCWarning(logVideo) << "Video worker for" << this->mPath << "didn't stop in time";
		(void) this->thread.release();
		return;
	}

	delete this->worker;
}

std::shared_ptr<VideoSource> VideoSource::acquire(const QString& path) {
	static QHash<QString, std::weak_ptr<VideoSource>> sources; // NOLINT

	auto key = path.toLower();
	if (auto existing = sources.value(key).lock()) return existing;

	auto source = std::make_shared<VideoSource>(path);
	sources.insert(key, source);
	return source;
}

std::shared_ptr<capture::SharedFrame> VideoSource::latestFrame(quint64* serial) const {
	std::lock_guard lock(this->slot->mutex);
	if (serial != nullptr) *serial = this->slot->serial;
	return this->slot->frame;
}

void VideoSource::setWanted(const void* viewer, bool wanted) {
	if (wanted) this->viewers.insert(viewer, true);
	else this->viewers.remove(viewer);
	this->updatePlaying();
}

void VideoSource::updatePlaying() {
	auto playing = !this->viewers.isEmpty();
	if (playing == this->mPlaying) return;
	this->mPlaying = playing;

	QMetaObject::invokeMethod(
	    this->worker,
	    [worker = this->worker, playing] { worker->setPlaying(playing); },
	    Qt::QueuedConnection
	);
}

void VideoSource::onReady() {
	this->mReady = true;
	this->mError.clear();
	emit this->stateChanged();
}

void VideoSource::onFailed(const QString& error) {
	this->mError = error;
	emit this->stateChanged();
}

namespace {

class VideoNode: public QSGSimpleTextureNode {
public:
	explicit VideoNode(QQuickWindow* window): window(window) {
		this->setFiltering(QSGTexture::Linear);
		this->setOwnsTexture(false);
	}

	~VideoNode() override { this->releaseTexture(); }

	Q_DISABLE_COPY_MOVE(VideoNode);

	[[nodiscard]] bool hasTexture() const { return this->sgTexture != nullptr; }
	[[nodiscard]] QSize frameSize() const { return this->size; }

	bool sync(const std::shared_ptr<capture::SharedFrame>& frame, quint64 serial) {
		if (!frame) return true;

		auto* ri = this->window->rendererInterface();
		if (ri->graphicsApi() != QSGRendererInterface::Direct3D11) return true;

		auto* device = static_cast<ID3D11Device*>(
		    ri->getResource(this->window, QSGRendererInterface::DeviceResource)
		);
		auto* context = static_cast<ID3D11DeviceContext*>(
		    ri->getResource(this->window, QSGRendererInterface::DeviceContextResource)
		);
		if (device == nullptr || context == nullptr) return true;

		if (frame != this->reader.frame()) {
			if (!this->reader.open(device, frame)) return true;

			if (this->texture == nullptr || this->size != frame->size) {
				D3D11_TEXTURE2D_DESC desc {};
				desc.Width = static_cast<UINT>(frame->size.width());
				desc.Height = static_cast<UINT>(frame->size.height());
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = FRAME_FORMAT;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_DEFAULT;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				ID3D11Texture2D* texture = nullptr;
				auto hr = device->CreateTexture2D(&desc, nullptr, &texture);
				if (FAILED(hr)) {
					qCWarning(logVideo) << "CreateTexture2D on the scene graph device failed:" << hresultText(hr);
					return true;
				}

				auto* rhi = this->window->rhi();
				auto* rhiTexture = rhi != nullptr ? rhi->newTexture(QRhiTexture::BGRA8, frame->size) : nullptr;
				if (rhiTexture == nullptr || !rhiTexture->createFrom({reinterpret_cast<quint64>(texture), 0})) {
					qCWarning(logVideo) << "Wrapping the video texture for the scene graph failed";
					delete rhiTexture;
					texture->Release();
					return true;
				}

				auto* sgTexture = new QSGPlainTexture();
				sgTexture->setOwnsTexture(false);
				sgTexture->setTexture(rhiTexture);
				sgTexture->setTextureSize(frame->size);
				sgTexture->setHasAlphaChannel(false);
				this->setTexture(sgTexture);

				this->releaseTexture();
				this->texture = texture;
				this->rhiTexture = rhiTexture;
				this->sgTexture = sgTexture;
				this->size = frame->size;
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
	void releaseTexture() {
		delete this->sgTexture;
		this->sgTexture = nullptr;
		if (this->rhiTexture != nullptr) this->rhiTexture->deleteLater();
		this->rhiTexture = nullptr;
		if (this->texture != nullptr) this->texture->Release();
		this->texture = nullptr;
	}

	QQuickWindow* window;
	capture::SharedFrameReader reader;
	ID3D11Texture2D* texture = nullptr;
	QRhiTexture* rhiTexture = nullptr;
	QSGTexture* sgTexture = nullptr;
	QSize size;
	quint64 serial = 0;
};

} // namespace

VideoWallpaper::VideoWallpaper(QQuickItem* parent): QQuickItem(parent) {
	this->setFlag(QQuickItem::ItemHasContents, true);
}

VideoWallpaper::~VideoWallpaper() { this->detach(); }

void VideoWallpaper::setSource(const QString& source) {
	auto path = source.isEmpty() ? QString() : normalizedPath(source);
	if (path == this->mSource) return;

	this->detach();
	this->mSource = path;
	this->attach();
	emit this->sourceChanged();
	emit this->stateChanged();
	this->update();
}

void VideoWallpaper::setPlaying(bool playing) {
	if (playing == this->mPlaying) return;
	this->mPlaying = playing;
	this->updateWanted();
	emit this->playingChanged();
}

void VideoWallpaper::attach() {
	if (this->mSource.isEmpty()) return;

	this->mVideo = VideoSource::acquire(this->mSource);
	QObject::connect(this->mVideo.get(), &VideoSource::frameReady, this, [this] { this->update(); });
	QObject::connect(this->mVideo.get(), &VideoSource::stateChanged, this, [this] {
		emit this->stateChanged();
		this->update();
	});
	this->updateWanted();
}

void VideoWallpaper::detach() {
	if (!this->mVideo) return;
	this->mVideo->setWanted(this, false);
	QObject::disconnect(this->mVideo.get(), nullptr, this, nullptr);
	this->mVideo.reset();
}

void VideoWallpaper::updateWanted() {
	if (this->mVideo) this->mVideo->setWanted(this, this->mPlaying && this->isVisible());
}

void VideoWallpaper::itemChange(ItemChange change, const ItemChangeData& value) {
	if (change == ItemVisibleHasChanged) this->updateWanted();
	QQuickItem::itemChange(change, value);
}

QSGNode* VideoWallpaper::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* /*data*/) {
	if (!this->mVideo || this->window() == nullptr) {
		delete oldNode;
		return nullptr;
	}

	auto* node = static_cast<VideoNode*>(oldNode); // NOLINT
	if (!node) node = new VideoNode(this->window());

	quint64 serial = 0;
	auto frame = this->mVideo->latestFrame(&serial);
	if (!node->sync(frame, serial)) this->update();

	if (!node->hasTexture()) {
		delete node;
		return nullptr;
	}

	node->setRect(this->boundingRect());
	node->setSourceRect(coverRect(node->frameSize(), this->size()));
	return node;
}

} // namespace qs::windows::video

#include "video_wallpaper.moc"
