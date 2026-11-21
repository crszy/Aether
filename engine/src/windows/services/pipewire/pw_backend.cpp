#include "pw_backend.hpp"

#include <qcontainerfwd.h>
#include <qhash.h>
#include <qlatin1stringview.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qt_windows.h>
#include <qtclasshelpermacros.h>
#include <qvariant.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <processthreadsapi.h>
#include <propidl.h>
#include <propsys.h>

#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

#include "../../startup.hpp"
#include "com_util.hpp"
#include "pipewire.hpp"
#include "policy_config.hpp"
#include "pw_link.hpp"
#include "pw_node.hpp"

namespace qs::windows::services::pipewire {

namespace {
Q_LOGGING_CATEGORY(logPwBackend, "quickshell.windows.pipewire.backend", QtWarningMsg);

bool ensureComInitialized() {
	auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (hr == S_OK || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) return true;
	qCWarning(logPwBackend) << "CoInitializeEx failed:" << Qt::hex << hr;
	return false;
}

QString takeComString(LPWSTR str) {
	if (str == nullptr) return {};
	auto result = QString::fromWCharArray(str);
	CoTaskMemFree(str);
	return result;
}

QString propertyAsString(IPropertyStore* store, const PROPERTYKEY& key) {
	if (store == nullptr) return {};

	PROPVARIANT pv;
	PropVariantInit(&pv);

	QString result;
	if (SUCCEEDED(store->GetValue(key, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
		result = QString::fromWCharArray(pv.pwszVal);
	}

	PropVariantClear(&pv);
	return result;
}

class MMNotificationClient final
    : public IMMNotificationClient
    , public ComCallbackTarget<PwBackend> {
public:
	explicit MMNotificationClient(PwBackend* target): ComCallbackTarget(target) {}

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
		return static_cast<ULONG>(InterlockedIncrement(&this->mRefCount));
	}

	ULONG STDMETHODCALLTYPE Release() override {
		auto rc = InterlockedDecrement(&this->mRefCount);
		if (rc == 0) delete this;
		return static_cast<ULONG>(rc);
	}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override {
		auto id = QString::fromWCharArray(deviceId);
		this->post([id, newState](PwBackend* backend) {
			backend->handleDeviceStateChanged(id, newState);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override {
		auto id = QString::fromWCharArray(deviceId);
		this->post([id](PwBackend* backend) { backend->handleDeviceAdded(id); });
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override {
		auto id = QString::fromWCharArray(deviceId);
		this->post([id](PwBackend* backend) { backend->handleDeviceRemoved(id); });
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) override {
		auto id = defaultDeviceId != nullptr ? QString::fromWCharArray(defaultDeviceId) : QString();
		auto flowInt = static_cast<int>(flow);
		auto roleInt = static_cast<int>(role);
		this->post([flowInt, roleInt, id](PwBackend* backend) {
			backend->handleDefaultDeviceChanged(flowInt, roleInt, id);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR /*deviceId*/, const PROPERTYKEY /*key*/)
	    override {
		return S_OK;
	}

private:
	volatile LONG mRefCount = 1;
};

class SessionNotificationClient final
    : public IAudioSessionNotification
    , public ComCallbackTarget<PwBackend> {
public:
	SessionNotificationClient(PwBackend* target, QString endpointId)
	    : ComCallbackTarget(target)
	    , mEndpointId(std::move(endpointId)) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
		if (ppvObject == nullptr) return E_POINTER;

		if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
			*ppvObject = static_cast<IAudioSessionNotification*>(this);
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

	HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* newSession) override {
		if (newSession == nullptr) return S_OK;
		newSession->AddRef();

		auto endpointId = this->mEndpointId;
		auto posted = this->post([newSession, endpointId](PwBackend* backend) {
			backend->adoptNewSession(endpointId, newSession);
		});
		if (!posted) newSession->Release();

		return S_OK;
	}

private:
	volatile LONG mRefCount = 1;
	const QString mEndpointId;
};

class SessionEventsCallback final
    : public IAudioSessionEvents
    , public ComCallbackTarget<PwBackend> {
public:
	SessionEventsCallback(PwBackend* target, QString sessionKey)
	    : ComCallbackTarget(target)
	    , mSessionKey(std::move(sessionKey)) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
		if (ppvObject == nullptr) return E_POINTER;

		if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionEvents)) {
			*ppvObject = static_cast<IAudioSessionEvents*>(this);
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

	HRESULT STDMETHODCALLTYPE
	OnSimpleVolumeChanged(float /*newVolume*/, BOOL /*newMute*/, LPCGUID /*eventContext*/) override {
		auto key = this->mSessionKey;
		this->post([key](PwBackend* backend) { backend->handleSessionVolumeChanged(key); });
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState newState) override {
		auto key = this->mSessionKey;
		auto state = static_cast<int>(newState);
		this->post([key, state](PwBackend* backend) {
			backend->handleSessionStateChanged(key, state);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnSessionDisconnected(AudioSessionDisconnectReason /*reason*/) override {
		auto key = this->mSessionKey;
		this->post([key](PwBackend* backend) { backend->handleSessionDisconnected(key); });
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnDisplayNameChanged(LPCWSTR /*newName*/, LPCGUID /*eventContext*/) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnIconPathChanged(LPCWSTR /*newPath*/, LPCGUID /*eventContext*/) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(
	    DWORD /*channelCount*/,
	    float* /*newVolumes*/,
	    DWORD /*changedChannel*/,
	    LPCGUID /*eventContext*/
	) override {
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnGroupingParamChanged(LPCGUID /*newGroupingParam*/, LPCGUID /*eventContext*/) override {
		return S_OK;
	}

private:
	volatile LONG mRefCount = 1;
	const QString mSessionKey;
};

} // namespace

PwBackend::PwBackend(Pipewire* owner): owner(owner) {}

PwBackend::~PwBackend() {
	if (this->notificationClient != nullptr && this->enumerator != nullptr) {
		auto* client = static_cast<MMNotificationClient*>(this->notificationClient);
		this->enumerator->UnregisterEndpointNotificationCallback(client);
		client->detach();
		client->Release();
	}

	const auto sessionKeys = this->sessions.keys();
	for (const auto& key: sessionKeys) {
		this->removeSessionNode(key);
	}

	const auto endpointIds = this->endpoints.keys();
	for (const auto& id: endpointIds) {
		this->removeEndpoint(id);
	}

	if (this->policyConfig != nullptr) {
		static_cast<IPolicyConfig*>(this->policyConfig)->Release();
	}

	if (this->enumerator != nullptr) this->enumerator->Release();
}

void PwBackend::start() {
	if (!ensureComInitialized()) return;

	auto hr = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_ALL,
	    __uuidof(IMMDeviceEnumerator),
	    reinterpret_cast<void**>(&this->enumerator)
	);

	if (FAILED(hr) || this->enumerator == nullptr) {
		qCWarning(logPwBackend) << "Could not create IMMDeviceEnumerator:" << Qt::hex << hr;
		return;
	}

	auto* notify = new MMNotificationClient(this);
	this->enumerator->RegisterEndpointNotificationCallback(notify);
	this->notificationClient = notify;

	this->addDefaultEndpoint(eRender);
	this->addDefaultEndpoint(eCapture);

	this->owner->backendSetReady(true);

	startup::afterFirstFrame(this, [this]() { this->startDeferred(); });
}

void PwBackend::startDeferred() {
	this->sessionsEnabled = true;
	const auto knownEndpoints = this->endpoints.keys();
	for (const auto& id: knownEndpoints) {
		this->attachSessions(id);
	}

	this->enumerateExistingDevices(eRender);
	this->enumerateExistingDevices(eCapture);

	this->refreshDefault(eRender);
	this->refreshDefault(eCapture);
}

void PwBackend::addDefaultEndpoint(int flow) {
	IMMDevice* device = nullptr;
	auto hr =
	    this->enumerator->GetDefaultAudioEndpoint(static_cast<EDataFlow>(flow), eConsole, &device);
	if (FAILED(hr) || device == nullptr) return;

	auto* node = this->createEndpointNode(device, flow);
	device->Release();

	if (flow == eRender) this->owner->backendSetDefaultSink(node);
	else this->owner->backendSetDefaultSource(node);
}

void PwBackend::enumerateExistingDevices(int flow) {
	IMMDeviceCollection* collection = nullptr;
	auto hr = this->enumerator->EnumAudioEndpoints(
	    static_cast<EDataFlow>(flow),
	    DEVICE_STATE_ACTIVE,
	    &collection
	);

	if (FAILED(hr) || collection == nullptr) {
		qCWarning(logPwBackend) << "EnumAudioEndpoints failed:" << Qt::hex << hr;
		return;
	}

	UINT count = 0;
	collection->GetCount(&count);

	for (UINT i = 0; i < count; i++) {
		IMMDevice* device = nullptr;
		if (SUCCEEDED(collection->Item(i, &device)) && device != nullptr) {
			this->createEndpointNode(device, flow);
			device->Release();
		}
	}

	collection->Release();
}

PwNode* PwBackend::createEndpointNode(IMMDevice* device, int flow) {
	LPWSTR rawId = nullptr;
	if (FAILED(device->GetId(&rawId)) || rawId == nullptr) return nullptr;
	auto deviceId = takeComString(rawId);

	if (this->endpoints.contains(deviceId)) return this->endpoints.value(deviceId).node;

	IPropertyStore* store = nullptr;
	QString description = deviceId;
	QString nickname;
	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
		auto friendlyName = propertyAsString(store, PKEY_Device_FriendlyName);
		if (!friendlyName.isEmpty()) description = friendlyName;
		nickname = propertyAsString(store, PKEY_DeviceInterface_FriendlyName);
		store->Release();
	}

	auto* node = new PwNode(this->owner);
	node->setId(this->nextId++);
	node->setBackendKey(deviceId);
	node->setName(deviceId);
	node->setDescription(description);
	node->setNickname(nickname.isEmpty() ? description : nickname);
	node->setType(flow == eRender ? PwNodeType::AudioSink : PwNodeType::AudioSource);

	QVariantMap props;
	props[QStringLiteral("device.description")] = description;
	props[QStringLiteral("node.name")] = deviceId;
	props[QStringLiteral("media.class")] =
	    flow == eRender ? QStringLiteral("Audio/Sink") : QStringLiteral("Audio/Source");
	node->setProperties(props);

	IAudioEndpointVolume* endpointVolume = nullptr;
	if (SUCCEEDED(device->Activate(
	        __uuidof(IAudioEndpointVolume),
	        CLSCTX_ALL,
	        nullptr,
	        reinterpret_cast<void**>(&endpointVolume)
	    ))
	    && endpointVolume != nullptr)
	{
		node->audio()->bindEndpoint(endpointVolume);
	} else {
		qCWarning(logPwBackend) << "Activate(IAudioEndpointVolume) failed for" << deviceId;
	}

	node->setMeterDevice(device);
	node->setReady(true);

	EndpointEntry entry;
	entry.node = node;
	entry.device = device;
	entry.flow = flow;
	device->AddRef();

	this->endpoints.insert(deviceId, entry);
	this->owner->backendAddNode(node);

	if (this->sessionsEnabled) this->attachSessions(deviceId);

	return node;
}

void PwBackend::attachSessions(const QString& deviceId) {
	auto it = this->endpoints.find(deviceId);
	if (it == this->endpoints.end()) return;
	auto& entry = it.value();
	if (entry.sessionManager != nullptr || entry.device == nullptr) return;

	IAudioSessionManager2* sessionManager = nullptr;
	if (FAILED(entry.device->Activate(
	        __uuidof(IAudioSessionManager2),
	        CLSCTX_ALL,
	        nullptr,
	        reinterpret_cast<void**>(&sessionManager)
	    ))
	    || sessionManager == nullptr)
	{
		qCWarning(logPwBackend) << "Activate(IAudioSessionManager2) failed for" << deviceId;
		return;
	}

	auto* notify = new SessionNotificationClient(this, deviceId);
	sessionManager->RegisterSessionNotification(notify);
	entry.sessionManager = sessionManager;
	entry.sessionNotification = notify;

	this->enumerateSessionsFor(deviceId);
}

void PwBackend::removeEndpoint(const QString& deviceId) {
	auto it = this->endpoints.find(deviceId);
	if (it == this->endpoints.end()) return;
	auto entry = it.value();
	this->endpoints.erase(it);

	QStringList affectedSessions;
	for (auto sessionIt = this->sessions.cbegin(); sessionIt != this->sessions.cend(); ++sessionIt) {
		if (sessionIt.value().endpointId == deviceId) affectedSessions.append(sessionIt.key());
	}
	for (const auto& key: affectedSessions) this->removeSessionNode(key);

	if (entry.sessionManager != nullptr) {
		if (entry.sessionNotification != nullptr) {
			auto* notify = static_cast<SessionNotificationClient*>(entry.sessionNotification);
			entry.sessionManager->UnregisterSessionNotification(notify);
			notify->detach();
			notify->Release();
		}
		entry.sessionManager->Release();
	}

	if (entry.device != nullptr) entry.device->Release();
	if (entry.node != nullptr) this->owner->backendRemoveNode(entry.node);
}

void PwBackend::enumerateSessionsFor(const QString& endpointId) {
	auto it = this->endpoints.find(endpointId);
	if (it == this->endpoints.end() || it.value().sessionManager == nullptr) return;

	IAudioSessionEnumerator* sessionEnumerator = nullptr;
	if (FAILED(it.value().sessionManager->GetSessionEnumerator(&sessionEnumerator))
	    || sessionEnumerator == nullptr)
	{
		return;
	}

	int count = 0;
	sessionEnumerator->GetCount(&count);

	for (int i = 0; i < count; i++) {
		IAudioSessionControl* control = nullptr;
		if (SUCCEEDED(sessionEnumerator->GetSession(i, &control)) && control != nullptr) {
			this->addSessionNode(endpointId, control);
		}
	}

	sessionEnumerator->Release();
}

void PwBackend::adoptNewSession(const QString& endpointId, IAudioSessionControl* control) {
	this->addSessionNode(endpointId, control);
}

void PwBackend::addSessionNode(const QString& endpointId, IAudioSessionControl* control) {
	class ReleaseGuard {
	public:
		explicit ReleaseGuard(IAudioSessionControl* ptr): ptr(ptr) {}
		~ReleaseGuard() {
			if (this->ptr != nullptr) this->ptr->Release();
		}
		Q_DISABLE_COPY_MOVE(ReleaseGuard);

	private:
		IAudioSessionControl* ptr;
	} releaseGuard(control);

	auto endpointIt = this->endpoints.find(endpointId);
	if (endpointIt == this->endpoints.end()) return;
	auto& endpoint = endpointIt.value();

	IAudioSessionControl2* ctrl2 = nullptr;
	if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&ctrl2)))
	    || ctrl2 == nullptr)
	{
		return;
	}

	if (ctrl2->IsSystemSoundsSession() == S_OK) {
		ctrl2->Release();
		return;
	}

	auto state = AudioSessionStateInactive;
	ctrl2->GetState(&state);
	if (state == AudioSessionStateExpired) {
		ctrl2->Release();
		return;
	}

	DWORD pid = 0;
	ctrl2->GetProcessId(&pid);
	if (pid == 0) {
		ctrl2->Release();
		return;
	}

	LPWSTR rawInstanceId = nullptr;
	QString instance;
	if (SUCCEEDED(ctrl2->GetSessionInstanceIdentifier(&rawInstanceId))) {
		instance = takeComString(rawInstanceId);
	}

	auto sessionKey = QString::number(pid) + QLatin1Char('-') + instance;
	if (this->sessions.contains(sessionKey)) {
		ctrl2->Release();
		return;
	}

	ISimpleAudioVolume* simpleVolume = nullptr;
	if (FAILED(ctrl2->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&simpleVolume)))
	    || simpleVolume == nullptr)
	{
		ctrl2->Release();
		return;
	}

	QString processBase = QStringLiteral("unknown");
	QString processBaseNoExt = processBase;
	if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
		wchar_t buffer[MAX_PATH];
		DWORD len = MAX_PATH;
		if (QueryFullProcessImageNameW(process, 0, buffer, &len) != 0) {
			auto full = QString::fromWCharArray(buffer, static_cast<int>(len));
			auto slash = full.lastIndexOf(QLatin1Char('\\'));
			processBase = slash >= 0 ? full.mid(slash + 1) : full;
			processBaseNoExt = processBase;
			auto dot = processBaseNoExt.lastIndexOf(QLatin1Char('.'));
			if (dot > 0) processBaseNoExt.truncate(dot);
		}
		CloseHandle(process);
	}

	QString displayName;
	LPWSTR rawDisplayName = nullptr;
	if (SUCCEEDED(ctrl2->GetDisplayName(&rawDisplayName))) {
		displayName = takeComString(rawDisplayName);
	}

	auto* node = new PwNode(this->owner);
	node->setId(this->nextId++);
	node->setBackendKey(sessionKey);
	node->setName(processBaseNoExt);
	node->setDescription(displayName.isEmpty() ? processBaseNoExt : displayName);
	node->setNickname(displayName);
	node->setType(endpoint.flow == eRender ? PwNodeType::AudioOutStream : PwNodeType::AudioInStream);

	QVariantMap props;
	props[QStringLiteral("application.name")] = displayName.isEmpty() ? processBaseNoExt : displayName;
	props[QStringLiteral("application.process.binary")] = processBase;
	props[QStringLiteral("application.icon-name")] = processBaseNoExt;
	props[QStringLiteral("node.name")] = processBaseNoExt;
	node->setProperties(props);

	auto* events = new SessionEventsCallback(this, sessionKey);
	ctrl2->RegisterAudioSessionNotification(events);

	node->audio()->bindSession(simpleVolume);
	node->setReady(true);

	SessionEntry entry;
	entry.node = node;
	entry.endpointId = endpointId;
	entry.eventsCallback = events;
	entry.control = ctrl2;
	entry.capturing = false;
	this->sessions.insert(sessionKey, entry);

	this->owner->backendAddNode(node);

	if (endpoint.flow == eCapture && state == AudioSessionStateActive) {
		this->handleSessionStateChanged(sessionKey, static_cast<int>(AudioSessionStateActive));
	}
}

void PwBackend::removeSessionNode(const QString& sessionKey) {
	auto it = this->sessions.find(sessionKey);
	if (it == this->sessions.end()) return;
	auto entry = it.value();
	this->sessions.erase(it);

	auto linkIt = this->captureLinkGroups.find(sessionKey);
	if (linkIt != this->captureLinkGroups.end()) {
		auto* group = linkIt.value();
		this->captureLinkGroups.erase(linkIt);
		this->owner->backendRemoveLinkGroup(group);
	}

	if (entry.eventsCallback != nullptr) {
		auto* events = static_cast<SessionEventsCallback*>(entry.eventsCallback);
		if (entry.control != nullptr) entry.control->UnregisterAudioSessionNotification(events);
		events->detach();
		events->Release();
	}

	if (entry.control != nullptr) entry.control->Release();
	if (entry.node != nullptr) this->owner->backendRemoveNode(entry.node);
}

void PwBackend::refreshDefault(int flow) {
	IMMDevice* device = nullptr;
	auto hr =
	    this->enumerator->GetDefaultAudioEndpoint(static_cast<EDataFlow>(flow), eConsole, &device);

	if (FAILED(hr) || device == nullptr) {
		if (flow == eRender) this->owner->backendSetDefaultSink(nullptr);
		else this->owner->backendSetDefaultSource(nullptr);
		return;
	}

	LPWSTR rawId = nullptr;
	PwNode* node = nullptr;
	if (SUCCEEDED(device->GetId(&rawId)) && rawId != nullptr) {
		auto deviceId = takeComString(rawId);
		auto endpointIt = this->endpoints.find(deviceId);
		if (endpointIt != this->endpoints.end()) node = endpointIt.value().node;
	}
	device->Release();

	if (flow == eRender) this->owner->backendSetDefaultSink(node);
	else this->owner->backendSetDefaultSource(node);
}

void PwBackend::tryCreatePolicyConfig() {
	if (this->policyConfigAttempted) return;
	this->policyConfigAttempted = true;

	IPolicyConfig* config = nullptr;
	auto hr = CoCreateInstance(
	    CLSID_PolicyConfigClient,
	    nullptr,
	    CLSCTX_ALL,
	    IID_IPolicyConfig,
	    reinterpret_cast<void**>(&config)
	);

	if (FAILED(hr) || config == nullptr) {
		qCWarning(logPwBackend
		) << "Could not create IPolicyConfig; changing the default device will not work:"
		  << Qt::hex << hr;
		return;
	}

	this->policyConfig = config;
}

void PwBackend::setPreferredDefault(PwNode* node, bool isSink) {
	if (node == nullptr) return;

	this->tryCreatePolicyConfig();
	if (this->policyConfig == nullptr) {
		qCWarning(logPwBackend) << "No IPolicyConfig available, cannot change the default"
		                        << (isSink ? "sink" : "source");
		return;
	}

	auto deviceId = node->backendKey();
	auto* wideId = reinterpret_cast<LPCWSTR>(deviceId.utf16());
	auto* config = static_cast<IPolicyConfig*>(this->policyConfig);

	config->SetDefaultEndpoint(wideId, eConsole);
	config->SetDefaultEndpoint(wideId, eMultimedia);
	config->SetDefaultEndpoint(wideId, eCommunications);

	if (isSink) this->owner->backendSetDefaultSink(node);
	else this->owner->backendSetDefaultSource(node);
}

void PwBackend::handleDefaultDeviceChanged(int flow, int role, const QString& deviceId) {
	if (role != eConsole) return;

	if (!deviceId.isEmpty() && !this->endpoints.contains(deviceId)) this->handleDeviceAdded(deviceId);

	PwNode* node = nullptr;
	if (!deviceId.isEmpty()) {
		auto it = this->endpoints.find(deviceId);
		if (it != this->endpoints.end()) node = it.value().node;
	}

	if (flow == eRender) this->owner->backendSetDefaultSink(node);
	else if (flow == eCapture) this->owner->backendSetDefaultSource(node);
}

void PwBackend::handleDeviceAdded(const QString& deviceId) {
	if (this->endpoints.contains(deviceId) || this->enumerator == nullptr) return;

	IMMDevice* device = nullptr;
	if (FAILED(this->enumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device))
	    || device == nullptr)
	{
		return;
	}

	DWORD state = 0;
	device->GetState(&state);

	if (state == DEVICE_STATE_ACTIVE) {
		IMMEndpoint* endpoint = nullptr;
		if (SUCCEEDED(device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(&endpoint)))
		    && endpoint != nullptr)
		{
			EDataFlow flow = eRender;
			endpoint->GetDataFlow(&flow);
			endpoint->Release();

			this->createEndpointNode(device, static_cast<int>(flow));
			this->refreshDefault(flow);
		}
	}

	device->Release();
}

void PwBackend::handleDeviceRemoved(const QString& deviceId) { this->removeEndpoint(deviceId); }

void PwBackend::handleDeviceStateChanged(const QString& deviceId, quint32 newState) {
	auto active = newState == DEVICE_STATE_ACTIVE;
	auto tracked = this->endpoints.contains(deviceId);

	if (active && !tracked) {
		this->handleDeviceAdded(deviceId);
	} else if (!active && tracked) {
		auto flow = this->endpoints.value(deviceId).flow;
		this->removeEndpoint(deviceId);
		this->refreshDefault(flow);
	}
}

void PwBackend::handleSessionVolumeChanged(const QString& sessionKey) {
	auto it = this->sessions.find(sessionKey);
	if (it == this->sessions.end() || it.value().node == nullptr) return;
	it.value().node->audio()->refreshFromDevice();
}

void PwBackend::handleSessionStateChanged(const QString& sessionKey, int newState) {
	auto it = this->sessions.find(sessionKey);
	if (it == this->sessions.end()) return;
	auto& entry = it.value();

	if (newState == AudioSessionStateExpired) {
		this->removeSessionNode(sessionKey);
		return;
	}

	auto endpointIt = this->endpoints.find(entry.endpointId);
	auto isCapture = endpointIt != this->endpoints.end() && endpointIt.value().flow == eCapture;
	if (!isCapture) return;

	auto shouldCapture = newState == AudioSessionStateActive;
	if (shouldCapture == entry.capturing) return;
	entry.capturing = shouldCapture;

	if (shouldCapture) {
		auto* sourceNode = endpointIt.value().node;
		if (sourceNode == nullptr || entry.node == nullptr) return;
		auto* group = new PwLinkGroup(sourceNode, entry.node, this->owner);
		this->captureLinkGroups.insert(sessionKey, group);
		this->owner->backendAddLinkGroup(group);
	} else {
		auto linkIt = this->captureLinkGroups.find(sessionKey);
		if (linkIt != this->captureLinkGroups.end()) {
			auto* group = linkIt.value();
			this->captureLinkGroups.erase(linkIt);
			this->owner->backendRemoveLinkGroup(group);
		}
	}
}

void PwBackend::handleSessionDisconnected(const QString& sessionKey) {
	this->removeSessionNode(sessionKey);
}

} // namespace qs::windows::services::pipewire
