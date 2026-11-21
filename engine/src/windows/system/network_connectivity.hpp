#pragma once

#include <qobject.h>
#include <qt_windows.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>

struct INetworkListManager;

namespace qs::windows::sys {

class Network;

class NetworkConnectivityBackend: public QObject {
	Q_OBJECT;

public:
	explicit NetworkConnectivityBackend(Network* owner);
	~NetworkConnectivityBackend() override;
	Q_DISABLE_COPY_MOVE(NetworkConnectivityBackend);

	void start();

	void refreshConnectivity();
	void refreshEthernet();

	void handleConnectivityChanged();
	void handleInterfaceChanged();

private:
	void performStart();

	Network* mOwner;
	INetworkListManager* mManager = nullptr;
	void* mConnectivitySink = nullptr;
	void* mConnectionPoint = nullptr;
	DWORD mAdviseCookie = 0;
	HANDLE mIpChangeHandle = nullptr;
};

} // namespace qs::windows::sys
