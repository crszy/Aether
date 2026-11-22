#include "audio_capture.hpp"

#include <cstdint>
#include <optional>

#include <qt_windows.h>

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qscopeguard.h>
#include <qstring.h>
#include <qtypes.h>

#include "../system/registry_util.hpp"
#include "audio_dsp.hpp"
#include "clock.hpp"

namespace qs::windows::recorder {

namespace {
Q_LOGGING_CATEGORY(logAudioCapture, "quickshell.windows.recorder", QtWarningMsg);

constexpr REFERENCE_TIME BUFFER_DURATION = 10'000'000;
constexpr DWORD POLL_MS = 10;
constexpr DWORD EVENT_TIMEOUT_MS = 20;
constexpr qint64 SYSTEM_AUDIO_MARGIN = 1'000'000;
constexpr qint64 MICROPHONE_MARGIN = 2'000'000;

constexpr const wchar_t* CONSENT_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";
constexpr const wchar_t* CONSENT_DESKTOP_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                                               L"CapabilityAccessManager\\ConsentStore\\microphone\\"
                                               L"NonPackaged";

QString hrMessage(const QString& what, HRESULT hr) {
	return QString("%1 (0x%2)").arg(what).arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
}

bool consentDenied(HKEY root, const wchar_t* key) {
	return sys::readRegistryString(root, key, L"Value").compare("Deny", Qt::CaseInsensitive) == 0;
}

} // namespace

AudioCapture::~AudioCapture() {
	this->stop();
	if (this->stopEvent != nullptr) CloseHandle(this->stopEvent);
	if (this->packetEvent != nullptr) CloseHandle(this->packetEvent);
}

QString AudioCapture::microphoneBlockedReason() {
	if (consentDenied(HKEY_LOCAL_MACHINE, CONSENT_KEY)) {
		return "microphone access is turned off for this device in Windows' privacy settings";
	}

	if (consentDenied(HKEY_CURRENT_USER, CONSENT_KEY)) {
		return "microphone access is turned off in Windows' privacy settings";
	}

	if (consentDenied(HKEY_CURRENT_USER, CONSENT_DESKTOP_KEY)) {
		return "Windows' privacy settings don't let desktop apps use the microphone";
	}

	return {};
}

bool AudioCapture::open(QString* error, bool* blocked) {
	auto microphone = this->source == Source::Microphone;
	if (blocked != nullptr) *blocked = false;

	auto fail = [error, microphone](HRESULT hr, const QString& what) {
		*error = hrMessage(what, hr);
		qCWarning(logAudioCapture) << (microphone ? "Microphone capture:" : "Loopback capture:")
		                           << *error;
		return false;
	};

	auto deny = [error, blocked](const QString& reason) {
		*error = reason;
		if (blocked != nullptr) *blocked = true;
		qCInfo(logAudioCapture) << "Microphone blocked:" << reason;
		return false;
	};

	auto deniedReason = [] {
		auto reason = microphoneBlockedReason();
		return reason.isEmpty() ? QString("Windows denied access to the microphone") : reason;
	};

	if (microphone) {
		auto reason = microphoneBlockedReason();
		if (!reason.isEmpty()) return deny(reason);
	}

	winrt::com_ptr<IMMDeviceEnumerator> enumerator;
	auto hr = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_ALL,
	    IID_PPV_ARGS(enumerator.put())
	);
	if (FAILED(hr)) return fail(hr, "no audio device enumerator");

	winrt::com_ptr<IMMDevice> device;
	hr = enumerator->GetDefaultAudioEndpoint(microphone ? eCapture : eRender, eConsole, device.put());
	if (FAILED(hr)) return fail(hr, microphone ? "no microphone found" : "no audio output device");

	hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, this->client.put_void());
	if (microphone && hr == E_ACCESSDENIED) return deny(deniedReason());
	if (FAILED(hr)) {
		return fail(hr, microphone ? "cannot open the microphone" : "cannot open the audio output device");
	}

	WAVEFORMATEX* mix = nullptr;
	hr = this->client->GetMixFormat(&mix);
	if (FAILED(hr)) return fail(hr, "no mix format");
	auto freeMix = qScopeGuard([mix] { CoTaskMemFree(mix); });

	auto tag = mix->wFormatTag;
	DWORD mask = 0;
	auto extensible = mix->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	if (tag == WAVE_FORMAT_EXTENSIBLE && extensible) {
		const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix); // NOLINT
		if (ext->SubFormat.Data2 == 0x0000 && ext->SubFormat.Data3 == 0x0010) {
			tag = static_cast<WORD>(ext->SubFormat.Data1);
		}
		mask = ext->dwChannelMask;
	}

	auto bits = mix->wBitsPerSample;
	if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) this->type = SampleType::Float32;
	else if (tag == WAVE_FORMAT_PCM && bits == 16) this->type = SampleType::Int16;
	else if (tag == WAVE_FORMAT_PCM && bits == 24) this->type = SampleType::Int24;
	else if (tag == WAVE_FORMAT_PCM && bits == 32) this->type = SampleType::Int32;
	else {
		return fail(E_NOTIMPL, QString("unsupported mix format (tag %1, %2 bits)").arg(tag).arg(bits));
	}

	auto channels = static_cast<int>(mix->nChannels);
	this->inRate = static_cast<int>(mix->nSamplesPerSec);
	this->bytesPerSample = mix->wBitsPerSample / 8;
	if (channels < 1 || this->inRate <= 0 || mix->nBlockAlign != channels * this->bytesPerSample) {
		return fail(E_NOTIMPL, "unsupported mix format layout");
	}

	this->weights = stereoWeights(channels, mask);

	DWORD flags = microphone ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST
	                         : AUDCLNT_STREAMFLAGS_LOOPBACK;
	hr = this->client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, BUFFER_DURATION, 0, mix, nullptr);
	if (microphone && hr == E_ACCESSDENIED) return deny(deniedReason());
	if (hr == AUDCLNT_E_DEVICE_IN_USE) {
		return fail(hr, microphone ? "another app holds the microphone exclusively"
		                           : "another app holds the audio output exclusively");
	}
	if (FAILED(hr)) {
		return fail(hr, microphone ? "cannot start microphone capture" : "cannot start loopback capture");
	}

	if (microphone) {
		this->packetEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (this->packetEvent == nullptr) {
			return fail(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed");
		}

		hr = this->client->SetEventHandle(this->packetEvent);
		if (FAILED(hr)) return fail(hr, "cannot set the microphone's capture event");

		winrt::com_ptr<IAudioSessionControl> session;
		if (SUCCEEDED(this->client->GetService(IID_PPV_ARGS(session.put())))) {
			if (auto session2 = session.try_as<IAudioSessionControl2>()) {
				session2->SetDuckingPreference(TRUE);
			}
		}
	}

	hr = this->client->GetService(IID_PPV_ARGS(this->capture.put()));
	if (FAILED(hr)) return fail(hr, "no capture service");

	this->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (this->stopEvent == nullptr) {
		return fail(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed");
	}

	qCInfo(logAudioCapture) << (microphone ? "Microphone:" : "Loopback:") << channels
	                        << "channels at" << this->inRate << "Hz, type"
	                        << static_cast<int>(this->type);
	return true;
}

void AudioCapture::start(qint64 origin, int outputRate) {
	if (this->thread.joinable() || !this->client || outputRate <= 0) return;
	this->origin = origin;
	this->outRate = outputRate;
	this->stream.configure(this->inRate, outputRate);
	this->thread = std::thread([this] { this->run(); });
}

void AudioCapture::stop() {
	if (!this->thread.joinable()) return;
	SetEvent(this->stopEvent);
	this->thread.join();
}

void AudioCapture::finish(qint64 until) {
	if (this->thread.joinable() || this->outRate <= 0) return;

	this->produced.clear();
	this->stream.padTo(static_cast<std::int64_t>(this->framesAt(until)), this->produced);
	this->mQueue.append(this->produced);
}

double AudioCapture::framesAt(qint64 time) const {
	return static_cast<double>(time - this->origin) * this->outRate / 10'000'000.0;
}

void AudioCapture::run() {
	auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	auto hr = this->client->Start();
	if (FAILED(hr)) {
		qCWarning(logAudioCapture) << "IAudioClient::Start failed:" << Qt::hex << hr;
		this->mLost.store(true, std::memory_order_relaxed);
	}

	auto margin = this->source == Source::Microphone ? MICROPHONE_MARGIN : SYSTEM_AUDIO_MARGIN;
	HANDLE handles[] = {this->stopEvent, this->packetEvent};

	while (true) {
		auto wait = this->packetEvent != nullptr
		              ? WaitForMultipleObjects(2, handles, FALSE, EVENT_TIMEOUT_MS)
		              : WaitForSingleObject(this->stopEvent, POLL_MS);
		if (wait == WAIT_OBJECT_0) break;
		if (wait == WAIT_FAILED) Sleep(POLL_MS);

		this->produced.clear();
		if (!this->mLost.load(std::memory_order_relaxed)) this->drain();

		auto target = static_cast<std::int64_t>(this->framesAt(qpc100ns() - margin));
		this->stream.padTo(target, this->produced);
		this->mQueue.append(this->produced);
	}

	this->client->Stop();
	if (SUCCEEDED(com)) CoUninitialize();
}

void AudioCapture::drain() {
	UINT32 packet = 0;
	HRESULT hr = S_OK;

	while (SUCCEEDED(hr = this->capture->GetNextPacketSize(&packet)) && packet > 0) {
		BYTE* data = nullptr;
		UINT32 frames = 0;
		DWORD flags = 0;
		UINT64 qpcPosition = 0;

		hr = this->capture->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition);
		if (FAILED(hr)) break;
		if (hr == AUDCLNT_S_BUFFER_EMPTY) break;

		auto silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
		convertToStereo(
		    silent ? nullptr : data,
		    frames,
		    this->type,
		    this->bytesPerSample,
		    this->weights,
		    this->converted
		);
		this->capture->ReleaseBuffer(frames);

		std::optional<double> position;
		if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) == 0) {
			position = this->framesAt(static_cast<qint64>(qpcPosition));
		}

		this->stream.push(this->converted.data(), frames, position, this->produced);
	}

	if (FAILED(hr)) {
		qCWarning(logAudioCapture) << (this->source == Source::Microphone ? "Microphone" : "Loopback")
		                           << "capture stopped:" << Qt::hex << hr;
		this->mLost.store(true, std::memory_order_relaxed);
	}
}

} // namespace qs::windows::recorder
