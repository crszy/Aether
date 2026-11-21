#include "loopback_source.hpp"

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#include <qt_windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qscopeguard.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"

namespace qs::windows::visualizer {

namespace {
QS_LOGGING_CATEGORY(logLoopback, "quickshell.windows.visualizer.capture", QtWarningMsg);

constexpr REFERENCE_TIME BUFFER_DURATION = 5'000'000;

class DefaultDeviceNotifier final: public IMMNotificationClient {
public:
	explicit DefaultDeviceNotifier(HANDLE event) {
		auto* process = GetCurrentProcess();
		if (DuplicateHandle(process, event, process, &this->event, 0, FALSE, DUPLICATE_SAME_ACCESS)
		    == 0)
		{
			this->event = nullptr;
		}
	}

	~DefaultDeviceNotifier() {
		if (this->event != nullptr) CloseHandle(this->event);
	}

	Q_DISABLE_COPY_MOVE(DefaultDeviceNotifier);

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
		if (ppvObject == nullptr) return E_POINTER;

		if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
			*ppvObject = static_cast<IMMNotificationClient*>(this);
			this->AddRef();
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override {
		return static_cast<ULONG>(InterlockedIncrement(&this->refCount));
	}

	ULONG STDMETHODCALLTYPE Release() override {
		auto rc = InterlockedDecrement(&this->refCount);
		if (rc == 0) delete this;
		return static_cast<ULONG>(rc);
	}

	HRESULT STDMETHODCALLTYPE
	OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR /*defaultDeviceId*/) override {
		if (flow == eRender && role == eConsole && this->event != nullptr) SetEvent(this->event);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnDeviceStateChanged(LPCWSTR /*deviceId*/, DWORD /*newState*/) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR /*deviceId*/) override { return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR /*deviceId*/) override { return S_OK; }

	HRESULT STDMETHODCALLTYPE
	OnPropertyValueChanged(LPCWSTR /*deviceId*/, const PROPERTYKEY /*key*/) override {
		return S_OK;
	}

private:
	volatile LONG refCount = 1;
	HANDLE event = nullptr;
};

} // namespace

LoopbackSource::~LoopbackSource() { this->close(); }

bool LoopbackSource::open(IMMDeviceEnumerator* enumerator) {
	this->close();
	if (enumerator == nullptr) return false;

	winrt::com_ptr<IMMDevice> device;
	auto hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
	if (FAILED(hr)) {
		qCDebug(logLoopback) << "No default output device:" << Qt::hex << hr;
		return false;
	}

	winrt::com_ptr<IAudioClient> client;
	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void());
	if (FAILED(hr)) {
		qCWarning(logLoopback) << "Cannot open the output device:" << Qt::hex << hr;
		return false;
	}

	WAVEFORMATEX* mix = nullptr;
	hr = client->GetMixFormat(&mix);
	if (FAILED(hr) || mix == nullptr) {
		qCWarning(logLoopback) << "No mix format:" << Qt::hex << hr;
		return false;
	}
	auto freeMix = qScopeGuard([mix] { CoTaskMemFree(mix); });

	auto tag = mix->wFormatTag;
	auto extensible = mix->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	if (tag == WAVE_FORMAT_EXTENSIBLE && extensible) {
		const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix); // NOLINT
		if (ext->SubFormat.Data2 == 0x0000 && ext->SubFormat.Data3 == 0x0010) {
			tag = static_cast<WORD>(ext->SubFormat.Data1);
		}
	}

	auto bits = mix->wBitsPerSample;
	if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) this->type = SampleType::Float32;
	else if (tag == WAVE_FORMAT_PCM && bits == 16) this->type = SampleType::Int16;
	else if (tag == WAVE_FORMAT_PCM && bits == 24) this->type = SampleType::Int24;
	else if (tag == WAVE_FORMAT_PCM && bits == 32) this->type = SampleType::Int32;
	else {
		qCWarning(logLoopback) << "Unsupported mix format: tag" << tag << "bits" << bits;
		return false;
	}

	this->channels = mix->nChannels;
	this->rate = static_cast<int>(mix->nSamplesPerSec);
	this->bytesPerSample = bits / 8;
	if (this->channels < 1 || this->rate <= 0
	    || mix->nBlockAlign != this->channels * this->bytesPerSample)
	{
		qCWarning(logLoopback) << "Unsupported mix format layout";
		return false;
	}

	hr = client->Initialize(
	    AUDCLNT_SHAREMODE_SHARED,
	    AUDCLNT_STREAMFLAGS_LOOPBACK,
	    BUFFER_DURATION,
	    0,
	    mix,
	    nullptr
	);
	if (FAILED(hr)) {
		qCWarning(logLoopback) << "Cannot start loopback capture:" << Qt::hex << hr;
		return false;
	}

	winrt::com_ptr<IAudioCaptureClient> capture;
	hr = client->GetService(IID_PPV_ARGS(capture.put()));
	if (FAILED(hr)) {
		qCWarning(logLoopback) << "No capture service:" << Qt::hex << hr;
		return false;
	}

	hr = client->Start();
	if (FAILED(hr)) {
		qCWarning(logLoopback) << "IAudioClient::Start failed:" << Qt::hex << hr;
		return false;
	}

	this->client = std::move(client);
	this->capture = std::move(capture);

	qCInfo(logLoopback) << "Visualizer capture:" << this->channels << "channels at" << this->rate
	                    << "Hz, type" << static_cast<int>(this->type);
	return true;
}

void LoopbackSource::close() {
	if (this->client) this->client->Stop();
	this->capture = nullptr;
	this->client = nullptr;
}

bool LoopbackSource::read(std::vector<float>& mono, bool* gotPackets) {
	*gotPackets = false;
	if (!this->capture) return false;

	UINT32 packet = 0;
	HRESULT hr = S_OK;

	while (SUCCEEDED(hr = this->capture->GetNextPacketSize(&packet)) && packet > 0) {
		BYTE* data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;

		hr = this->capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
		if (FAILED(hr) || hr == AUDCLNT_S_BUFFER_EMPTY) break;

		this->append(data, frames, flags, mono);
		this->capture->ReleaseBuffer(frames);
		*gotPackets = true;
	}

	if (FAILED(hr)) {
		qCInfo(logLoopback) << "Visualizer capture lost:" << Qt::hex << hr;
		return false;
	}

	return true;
}

void LoopbackSource::append(const BYTE* data, UINT32 frames, DWORD flags, std::vector<float>& mono)
    const {
	auto offset = mono.size();
	mono.resize(offset + frames, 0.0F);
	if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) return;

	auto blockAlign = static_cast<size_t>(this->channels) * this->bytesPerSample;
	auto scale = 1.0F / static_cast<float>(this->channels);

	for (UINT32 f = 0; f < frames; ++f) {
		const auto* frame = data + f * blockAlign;
		auto sum = 0.0F;

		for (auto c = 0; c < this->channels; ++c) {
			const auto* p = frame + static_cast<ptrdiff_t>(c) * this->bytesPerSample;

			switch (this->type) {
			case SampleType::Float32: {
				float v = 0;
				std::memcpy(&v, p, sizeof(v));
				sum += v;
			} break;
			case SampleType::Int16: {
				qint16 v = 0;
				std::memcpy(&v, p, sizeof(v));
				sum += static_cast<float>(v) / 32768.0F;
			} break;
			case SampleType::Int24: {
				auto v = static_cast<qint32>(
				    static_cast<quint32>(p[0]) << 8 | static_cast<quint32>(p[1]) << 16
				    | static_cast<quint32>(p[2]) << 24
				);
				sum += static_cast<float>(v >> 8) / 8388608.0F;
			} break;
			case SampleType::Int32: {
				qint32 v = 0;
				std::memcpy(&v, p, sizeof(v));
				sum += static_cast<float>(v) / 2147483648.0F;
			} break;
			}
		}

		mono[offset + f] = sum * scale;
	}
}

DefaultDeviceWatcher::~DefaultDeviceWatcher() { this->stop(); }

bool DefaultDeviceWatcher::start(IMMDeviceEnumerator* enumerator, HANDLE event) {
	this->stop();
	if (enumerator == nullptr || event == nullptr) return false;

	auto* client = new DefaultDeviceNotifier(event);
	auto hr = enumerator->RegisterEndpointNotificationCallback(client);
	if (FAILED(hr)) {
		qCWarning(logLoopback) << "Cannot watch the default output device:" << Qt::hex << hr;
		client->Release();
		return false;
	}

	this->enumerator.copy_from(enumerator);
	this->client = client;
	return true;
}

void DefaultDeviceWatcher::stop() {
	if (this->client == nullptr) return;
	this->enumerator->UnregisterEndpointNotificationCallback(this->client);
	this->client->Release();
	this->client = nullptr;
	this->enumerator = nullptr;
}

} // namespace qs::windows::visualizer
