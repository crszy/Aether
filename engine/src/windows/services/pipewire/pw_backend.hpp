#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

struct IMMDevice;
struct IMMDeviceEnumerator;
struct IAudioSessionManager2;
struct IAudioSessionControl;
struct IAudioSessionControl2;
struct IAudioMeterInformation;

namespace qs::windows::services::pipewire {

class Pipewire;
class PwNode;
class PwLinkGroup;

class PwBackend: public QObject {
	Q_OBJECT;

public:
	explicit PwBackend(Pipewire* owner);
	~PwBackend() override;
	Q_DISABLE_COPY_MOVE(PwBackend);

	void start();

	void setPreferredDefault(PwNode* node, bool isSink);

	void handleDefaultDeviceChanged(int flow, int role, const QString& deviceId);
	void handleDeviceAdded(const QString& deviceId);
	void handleDeviceRemoved(const QString& deviceId);
	void handleDeviceStateChanged(const QString& deviceId, quint32 newState);
	void adoptNewSession(const QString& endpointId, IAudioSessionControl* control);
	void handleSessionVolumeChanged(const QString& sessionKey);
	void handleSessionStateChanged(const QString& sessionKey, int newState);
	void handleSessionDisconnected(const QString& sessionKey);

private:
	struct EndpointEntry {
		PwNode* node = nullptr;
		IMMDevice* device = nullptr;
		IAudioSessionManager2* sessionManager = nullptr;
		void* sessionNotification = nullptr;
		int flow = 0;
	};

	struct SessionEntry {
		PwNode* node = nullptr;
		QString endpointId;
		void* eventsCallback = nullptr;
		IAudioSessionControl2* control = nullptr;
		bool capturing = false;
	};

	void startDeferred();
	void addDefaultEndpoint(int flow);
	void enumerateExistingDevices(int flow);
	PwNode* createEndpointNode(IMMDevice* device, int flow);
	void attachSessions(const QString& deviceId);
	void removeEndpoint(const QString& deviceId);
	void enumerateSessionsFor(const QString& endpointId);
	void addSessionNode(const QString& endpointId, IAudioSessionControl* control);
	void removeSessionNode(const QString& sessionKey);
	void refreshDefault(int flow);
	void tryCreatePolicyConfig();

	Pipewire* owner;
	IMMDeviceEnumerator* enumerator = nullptr;
	void* notificationClient = nullptr;
	void* policyConfig = nullptr;
	bool policyConfigAttempted = false;
	bool sessionsEnabled = false;

	QHash<QString, EndpointEntry> endpoints;
	QHash<QString, SessionEntry> sessions;
	QHash<QString, PwLinkGroup*> captureLinkGroups;

	quint32 nextId = 1;
};

} // namespace qs::windows::services::pipewire
