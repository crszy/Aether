#include "pw_node.hpp"
#include <utility>

#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qt_windows.h>
#include <qtypes.h>
#include <qvector.h>

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include "com_util.hpp"

namespace qs::windows::services::pipewire {

namespace {
Q_LOGGING_CATEGORY(logPwNode, "quickshell.windows.pipewire.node", QtWarningMsg);

class EndpointVolumeCallback final
    : public IAudioEndpointVolumeCallback
    , public ComCallbackTarget<PwNodeAudio> {
public:
	explicit EndpointVolumeCallback(PwNodeAudio* target): ComCallbackTarget(target) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
		if (ppvObject == nullptr) return E_POINTER;

		if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioEndpointVolumeCallback)) {
			*ppvObject = static_cast<IAudioEndpointVolumeCallback*>(this);
			this->AddRef();
			return S_OK;
		}

		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override {
		return static_cast<ULONG>(InterlockedIncrement(&this->mRefCount));
	}

	ULONG STDMETHODCALLTYPE Release() override {
		auto rc = InterlockedDecrement(&this->mRefCount);
		if (rc == 0) delete this;
		return static_cast<ULONG>(rc);
	}

	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
		if (data == nullptr) return S_OK;

		this->post([](PwNodeAudio* audio) { audio->refreshFromDevice(); });
		return S_OK;
	}

private:
	volatile LONG mRefCount = 1;
};

} // namespace

PwNodeAudio::~PwNodeAudio() {
	if (this->mEndpointVolume != nullptr) {
		auto* callback = static_cast<EndpointVolumeCallback*>(this->mEndpointCallback);
		if (callback != nullptr) {
			this->mEndpointVolume->UnregisterControlChangeNotify(callback);
			callback->detach();
			callback->Release();
		}

		this->mEndpointVolume->Release();
	}

	if (this->mSessionVolume != nullptr) {
		this->mSessionVolume->Release();
	}
}

void PwNodeAudio::bindEndpoint(IAudioEndpointVolume* endpointVolume) {
	this->mEndpointVolume = endpointVolume;

	auto* callback = new EndpointVolumeCallback(this);
	auto hr = endpointVolume->RegisterControlChangeNotify(callback);
	if (FAILED(hr)) {
		qCWarning(logPwNode) << "RegisterControlChangeNotify failed:" << Qt::hex << hr;
	}

	this->mEndpointCallback = callback;
	this->readState();
}

void PwNodeAudio::bindSession(ISimpleAudioVolume* sessionVolume) {
	this->mSessionVolume = sessionVolume;
	this->readState();
}

float PwNodeAudio::readVolume() const {
	auto level = 0.0F;
	if (this->mEndpointVolume != nullptr) this->mEndpointVolume->GetMasterVolumeLevelScalar(&level);
	else if (this->mSessionVolume != nullptr) this->mSessionVolume->GetMasterVolume(&level);
	return level;
}

void PwNodeAudio::readState() {
	BOOL muted = FALSE;
	if (this->mEndpointVolume != nullptr) this->mEndpointVolume->GetMute(&muted);
	else if (this->mSessionVolume != nullptr) this->mSessionVolume->GetMute(&muted);

	this->mMuted = muted != FALSE;
	this->mVolume = this->readVolume();
	this->mVolumes.clear();

	UINT count = 0;
	if (this->mEndpointVolume == nullptr || FAILED(this->mEndpointVolume->GetChannelCount(&count))) {
		return;
	}

	this->mVolumes.reserve(static_cast<qsizetype>(count));
	for (UINT i = 0; i < count; i++) {
		auto level = 0.0F;
		this->mEndpointVolume->GetChannelVolumeLevelScalar(i, &level);
		this->mVolumes.append(level);
	}
}

bool PwNodeAudio::isMuted() const { return this->mMuted; }

void PwNodeAudio::setMuted(bool muted) {
	HRESULT hr = S_OK;
	if (this->mEndpointVolume != nullptr) {
		hr = this->mEndpointVolume->SetMute(muted ? TRUE : FALSE, nullptr);
	} else if (this->mSessionVolume != nullptr) {
		hr = this->mSessionVolume->SetMute(muted ? TRUE : FALSE, nullptr);
	} else {
		return;
	}

	if (FAILED(hr)) {
		qCWarning(logPwNode) << "SetMute failed:" << Qt::hex << hr;
		return;
	}

	this->mMuted = muted;
	emit this->mutedChanged();
}

float PwNodeAudio::volume() const { return this->mVolume; }

void PwNodeAudio::setVolume(float volume) {
	if (volume < 0.0F) volume = 0.0F;
	if (volume > 1.0F) volume = 1.0F;

	HRESULT hr = S_OK;
	if (this->mEndpointVolume != nullptr) {
		hr = this->mEndpointVolume->SetMasterVolumeLevelScalar(volume, nullptr);
	} else if (this->mSessionVolume != nullptr) {
		hr = this->mSessionVolume->SetMasterVolume(volume, nullptr);
	} else {
		return;
	}

	if (FAILED(hr)) {
		qCWarning(logPwNode) << "SetMasterVolume failed:" << Qt::hex << hr;
		return;
	}

	this->mVolume = this->readVolume();
	emit this->volumeChanged();
}

QVector<PwAudioChannel::Enum> PwNodeAudio::channels() const {
	return QVector<PwAudioChannel::Enum>(this->mVolumes.size(), PwAudioChannel::Unknown);
}

QVector<float> PwNodeAudio::volumes() const { return this->mVolumes; }

void PwNodeAudio::setVolumes(const QVector<float>& volumes) {
	if (this->mEndpointVolume == nullptr) return;

	for (qsizetype i = 0; i < volumes.length(); i++) {
		auto level = volumes.at(i);
		if (level < 0.0F) level = 0.0F;
		if (level > 1.0F) level = 1.0F;
		this->mEndpointVolume->SetChannelVolumeLevelScalar(static_cast<UINT>(i), level, nullptr);
	}

	this->readState();
	emit this->volumesChanged();
	emit this->volumeChanged();
}

void PwNodeAudio::refreshFromDevice() {
	auto channelCount = this->mVolumes.size();
	this->readState();

	emit this->volumeChanged();
	emit this->mutedChanged();
	emit this->volumesChanged();
	if (this->mVolumes.size() != channelCount) emit this->channelsChanged();
}

PwNode::PwNode(QObject* parent): QObject(parent) {
	this->mAudio = new PwNodeAudio(this);
}

PwNode::~PwNode() {
	if (this->mMeter != nullptr) this->mMeter->Release();
	if (this->mMeterDevice != nullptr) this->mMeterDevice->Release();
}

void PwNode::setProperties(const QVariantMap& properties) {
	if (this->mProperties == properties) return;
	this->mProperties = properties;
	emit this->propertiesChanged();
}

void PwNode::setReady(bool ready) {
	if (this->mReady == ready) return;
	this->mReady = ready;
	emit this->readyChanged();
}

void PwNode::setMeterDevice(IMMDevice* device) {
	if (this->mMeterDevice != nullptr) this->mMeterDevice->Release();
	this->mMeterDevice = device;
	if (device != nullptr) device->AddRef();
}

IAudioMeterInformation* PwNode::meterInformation() {
	if (this->mMeter != nullptr || this->mMeterDevice == nullptr) return this->mMeter;

	auto* device = std::exchange(this->mMeterDevice, nullptr);
	IAudioMeterInformation* meter = nullptr;
	auto hr = device->Activate(
	    __uuidof(IAudioMeterInformation),
	    CLSCTX_ALL,
	    nullptr,
	    reinterpret_cast<void**>(&meter)
	);
	device->Release();

	if (FAILED(hr) || meter == nullptr) {
		qCDebug(logPwNode) << "Activate(IAudioMeterInformation) failed for" << this->mBackendKey;
		return nullptr;
	}

	this->mMeter = meter;
	return meter;
}

} // namespace qs::windows::services::pipewire
