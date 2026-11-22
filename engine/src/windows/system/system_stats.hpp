#pragma once

#include <vector>

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qt_windows.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include <pdh.h>

namespace qs::windows::sys {

class SystemStats: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);
	Q_PROPERTY(int updateIntervalMs READ updateIntervalMs WRITE setUpdateIntervalMs NOTIFY updateIntervalMsChanged);
	Q_PROPERTY(qreal cpuUsage READ default NOTIFY cpuUsageChanged BINDABLE bindableCpuUsage);
	Q_PROPERTY(quint64 memoryTotalKb READ default NOTIFY memoryTotalKbChanged BINDABLE bindableMemoryTotalKb);
	Q_PROPERTY(quint64 memoryAvailableKb READ default NOTIFY memoryAvailableKbChanged BINDABLE bindableMemoryAvailableKb);
	Q_PROPERTY(quint64 swapTotalKb READ default NOTIFY swapTotalKbChanged BINDABLE bindableSwapTotalKb);
	Q_PROPERTY(quint64 swapAvailableKb READ default NOTIFY swapAvailableKbChanged BINDABLE bindableSwapAvailableKb);
	Q_PROPERTY(qint64 uptimeSeconds READ default NOTIFY uptimeSecondsChanged BINDABLE bindableUptimeSeconds);
	Q_PROPERTY(QString cpuName READ cpuName CONSTANT);
	Q_PROPERTY(bool gpuEnabled READ gpuEnabled WRITE setGpuEnabled NOTIFY gpuEnabledChanged);
	Q_PROPERTY(qreal gpuUsage READ default NOTIFY gpuUsageChanged BINDABLE bindableGpuUsage);
	Q_PROPERTY(bool networkEnabled READ networkEnabled WRITE setNetworkEnabled NOTIFY networkEnabledChanged);
	Q_PROPERTY(qreal networkDownloadBytesPerSec READ default NOTIFY networkDownloadBytesPerSecChanged BINDABLE bindableNetworkDownloadBytesPerSec);
	Q_PROPERTY(qreal networkUploadBytesPerSec READ default NOTIFY networkUploadBytesPerSecChanged BINDABLE bindableNetworkUploadBytesPerSec);
	Q_PROPERTY(qreal networkDownloadedBytes READ default NOTIFY networkDownloadedBytesChanged BINDABLE bindableNetworkDownloadedBytes);
	Q_PROPERTY(qreal networkUploadedBytes READ default NOTIFY networkUploadedBytesChanged BINDABLE bindableNetworkUploadedBytes);
	// clang-format on

public:
	explicit SystemStats(QObject* parent = nullptr);
	~SystemStats() override;
	Q_DISABLE_COPY_MOVE(SystemStats);

	[[nodiscard]] bool active() const { return this->mActive; }
	void setActive(bool active);

	[[nodiscard]] bool gpuEnabled() const { return this->mGpuEnabled; }
	void setGpuEnabled(bool enabled);

	[[nodiscard]] int updateIntervalMs() const { return this->mUpdateIntervalMs; }
	void setUpdateIntervalMs(int interval);

	[[nodiscard]] QString cpuName() const { return this->mCpuName; }

	[[nodiscard]] QBindable<qreal> bindableCpuUsage() const { return &this->bCpuUsage; }
	[[nodiscard]] QBindable<quint64> bindableMemoryTotalKb() const {
		return &this->bMemoryTotalKb;
	}
	[[nodiscard]] QBindable<quint64> bindableMemoryAvailableKb() const {
		return &this->bMemoryAvailableKb;
	}
	[[nodiscard]] QBindable<quint64> bindableSwapTotalKb() const { return &this->bSwapTotalKb; }
	[[nodiscard]] QBindable<quint64> bindableSwapAvailableKb() const {
		return &this->bSwapAvailableKb;
	}
	[[nodiscard]] QBindable<qint64> bindableUptimeSeconds() const {
		return &this->bUptimeSeconds;
	}
	[[nodiscard]] QBindable<qreal> bindableGpuUsage() const { return &this->bGpuUsage; }

	[[nodiscard]] bool networkEnabled() const { return this->mNetworkEnabled; }
	void setNetworkEnabled(bool enabled);

	[[nodiscard]] QBindable<qreal> bindableNetworkDownloadBytesPerSec() const {
		return &this->bNetworkDownloadBytesPerSec;
	}
	[[nodiscard]] QBindable<qreal> bindableNetworkUploadBytesPerSec() const {
		return &this->bNetworkUploadBytesPerSec;
	}
	[[nodiscard]] QBindable<qreal> bindableNetworkDownloadedBytes() const {
		return &this->bNetworkDownloadedBytes;
	}
	[[nodiscard]] QBindable<qreal> bindableNetworkUploadedBytes() const {
		return &this->bNetworkUploadedBytes;
	}

signals:
	void activeChanged();
	void gpuEnabledChanged();
	void updateIntervalMsChanged();
	void cpuUsageChanged();
	void memoryTotalKbChanged();
	void memoryAvailableKbChanged();
	void swapTotalKbChanged();
	void swapAvailableKbChanged();
	void uptimeSecondsChanged();
	void gpuUsageChanged();
	void networkEnabledChanged();
	void networkDownloadBytesPerSecChanged();
	void networkUploadBytesPerSecChanged();
	void networkDownloadedBytesChanged();
	void networkUploadedBytesChanged();

private:
	void sample();
	void setupGpuCounters();
	void teardownGpuCounters();
	void sampleGpu();
	void sampleNetwork();

	bool mActive = true;
	bool mGpuEnabled = false;
	int mUpdateIntervalMs = 2000;
	QString mCpuName;
	QTimer timer;

	bool havePrevCpuTimes = false;
	ULARGE_INTEGER prevIdle {};
	ULARGE_INTEGER prevKernel {};
	ULARGE_INTEGER prevUser {};

	PDH_HQUERY gpuQuery = nullptr;
	std::vector<PDH_HCOUNTER> gpuCounters;
	bool gpuAvailable = false;

	bool mNetworkEnabled = false;
	bool havePrevNetworkSample = false;
	qint64 prevNetworkTimestampMs = 0;
	quint64 prevNetworkInBytes = 0;
	quint64 prevNetworkOutBytes = 0;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, qreal, bCpuUsage, &SystemStats::cpuUsageChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, quint64, bMemoryTotalKb, &SystemStats::memoryTotalKbChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, quint64, bMemoryAvailableKb, &SystemStats::memoryAvailableKbChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, quint64, bSwapTotalKb, &SystemStats::swapTotalKbChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, quint64, bSwapAvailableKb, &SystemStats::swapAvailableKbChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, qint64, bUptimeSeconds, &SystemStats::uptimeSecondsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, qreal, bGpuUsage, &SystemStats::gpuUsageChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemStats, qreal, bNetworkDownloadBytesPerSec, -1.0, &SystemStats::networkDownloadBytesPerSecChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemStats, qreal, bNetworkUploadBytesPerSec, -1.0, &SystemStats::networkUploadBytesPerSecChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, qreal, bNetworkDownloadedBytes, &SystemStats::networkDownloadedBytesChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemStats, qreal, bNetworkUploadedBytes, &SystemStats::networkUploadedBytesChanged);
	// clang-format on
};

} // namespace qs::windows::sys
