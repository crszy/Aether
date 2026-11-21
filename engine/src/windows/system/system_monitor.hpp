#pragma once

#include <memory>

#include <qnumeric.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::windows::sys {

class SystemMonitorWorker;

struct SystemMonitorStaticInfo {
	QString hostName;
	QString osName;
	QString osVersion;
	QString osBuild;
	QString boardVendor;
	QString boardModel;
	QString cpuName;
	int cpuCores = 0;
	int cpuThreads = 0;
	qreal cpuBaseMhz = qQNaN();
	qreal cpuMaxMhz = qQNaN();
	qint64 cpuL3Bytes = 0;
};

struct SystemMonitorSample {
	qint64 uptimeSeconds = 0;
	qreal cpuUsage = qQNaN();
	qreal cpuMhz = qQNaN();
	qreal cpuTemperature = qQNaN();
	bool haveMemory = false;
	qint64 memoryTotal = 0;
	qint64 memoryUsed = 0;
	qint64 swapTotal = 0;
	qint64 swapUsed = 0;
	QVariantList gpus;
	bool haveDisks = false;
	QVariantList disks;
};

class SystemMonitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);
	Q_PROPERTY(int intervalMs READ intervalMs WRITE setIntervalMs NOTIFY intervalMsChanged);
	Q_PROPERTY(bool ready READ default NOTIFY readyChanged BINDABLE bindableReady);
	Q_PROPERTY(QString hostName READ default NOTIFY hostNameChanged BINDABLE bindableHostName);
	Q_PROPERTY(QString osName READ default NOTIFY osNameChanged BINDABLE bindableOsName);
	Q_PROPERTY(QString osVersion READ default NOTIFY osVersionChanged BINDABLE bindableOsVersion);
	Q_PROPERTY(QString osBuild READ default NOTIFY osBuildChanged BINDABLE bindableOsBuild);
	Q_PROPERTY(QString boardVendor READ default NOTIFY boardVendorChanged BINDABLE bindableBoardVendor);
	Q_PROPERTY(QString boardModel READ default NOTIFY boardModelChanged BINDABLE bindableBoardModel);
	Q_PROPERTY(qint64 uptimeSeconds READ default NOTIFY uptimeSecondsChanged BINDABLE bindableUptimeSeconds);
	Q_PROPERTY(QString cpuName READ default NOTIFY cpuNameChanged BINDABLE bindableCpuName);
	Q_PROPERTY(int cpuCores READ default NOTIFY cpuCoresChanged BINDABLE bindableCpuCores);
	Q_PROPERTY(int cpuThreads READ default NOTIFY cpuThreadsChanged BINDABLE bindableCpuThreads);
	Q_PROPERTY(qreal cpuBaseMhz READ default NOTIFY cpuBaseMhzChanged BINDABLE bindableCpuBaseMhz);
	Q_PROPERTY(qreal cpuMaxMhz READ default NOTIFY cpuMaxMhzChanged BINDABLE bindableCpuMaxMhz);
	Q_PROPERTY(qint64 cpuL3Bytes READ default NOTIFY cpuL3BytesChanged BINDABLE bindableCpuL3Bytes);
	Q_PROPERTY(qreal cpuUsage READ default NOTIFY cpuUsageChanged BINDABLE bindableCpuUsage);
	Q_PROPERTY(qreal cpuMhz READ default NOTIFY cpuMhzChanged BINDABLE bindableCpuMhz);
	Q_PROPERTY(qreal cpuTemperature READ default NOTIFY cpuTemperatureChanged BINDABLE bindableCpuTemperature);
	Q_PROPERTY(qint64 memoryTotal READ default NOTIFY memoryTotalChanged BINDABLE bindableMemoryTotal);
	Q_PROPERTY(qint64 memoryUsed READ default NOTIFY memoryUsedChanged BINDABLE bindableMemoryUsed);
	Q_PROPERTY(qint64 swapTotal READ default NOTIFY swapTotalChanged BINDABLE bindableSwapTotal);
	Q_PROPERTY(qint64 swapUsed READ default NOTIFY swapUsedChanged BINDABLE bindableSwapUsed);
	Q_PROPERTY(QVariantList gpus READ default NOTIFY gpusChanged BINDABLE bindableGpus);
	Q_PROPERTY(QVariantList disks READ default NOTIFY disksChanged BINDABLE bindableDisks);
	// clang-format on

public:
	explicit SystemMonitor(QObject* parent = nullptr);
	~SystemMonitor() override;
	Q_DISABLE_COPY_MOVE(SystemMonitor);

	[[nodiscard]] bool active() const { return this->mActive; }
	void setActive(bool active);

	[[nodiscard]] int intervalMs() const { return this->mIntervalMs; }
	void setIntervalMs(int intervalMs);

	Q_INVOKABLE QVariantMap volumeUsage(const QString& path) const;

	[[nodiscard]] QBindable<bool> bindableReady() const { return &this->bReady; }
	[[nodiscard]] QBindable<QString> bindableHostName() const { return &this->bHostName; }
	[[nodiscard]] QBindable<QString> bindableOsName() const { return &this->bOsName; }
	[[nodiscard]] QBindable<QString> bindableOsVersion() const { return &this->bOsVersion; }
	[[nodiscard]] QBindable<QString> bindableOsBuild() const { return &this->bOsBuild; }
	[[nodiscard]] QBindable<QString> bindableBoardVendor() const { return &this->bBoardVendor; }
	[[nodiscard]] QBindable<QString> bindableBoardModel() const { return &this->bBoardModel; }
	[[nodiscard]] QBindable<qint64> bindableUptimeSeconds() const {
		return &this->bUptimeSeconds;
	}
	[[nodiscard]] QBindable<QString> bindableCpuName() const { return &this->bCpuName; }
	[[nodiscard]] QBindable<int> bindableCpuCores() const { return &this->bCpuCores; }
	[[nodiscard]] QBindable<int> bindableCpuThreads() const { return &this->bCpuThreads; }
	[[nodiscard]] QBindable<qreal> bindableCpuBaseMhz() const { return &this->bCpuBaseMhz; }
	[[nodiscard]] QBindable<qreal> bindableCpuMaxMhz() const { return &this->bCpuMaxMhz; }
	[[nodiscard]] QBindable<qint64> bindableCpuL3Bytes() const { return &this->bCpuL3Bytes; }
	[[nodiscard]] QBindable<qreal> bindableCpuUsage() const { return &this->bCpuUsage; }
	[[nodiscard]] QBindable<qreal> bindableCpuMhz() const { return &this->bCpuMhz; }
	[[nodiscard]] QBindable<qreal> bindableCpuTemperature() const {
		return &this->bCpuTemperature;
	}
	[[nodiscard]] QBindable<qint64> bindableMemoryTotal() const { return &this->bMemoryTotal; }
	[[nodiscard]] QBindable<qint64> bindableMemoryUsed() const { return &this->bMemoryUsed; }
	[[nodiscard]] QBindable<qint64> bindableSwapTotal() const { return &this->bSwapTotal; }
	[[nodiscard]] QBindable<qint64> bindableSwapUsed() const { return &this->bSwapUsed; }
	[[nodiscard]] QBindable<QVariantList> bindableGpus() const { return &this->bGpus; }
	[[nodiscard]] QBindable<QVariantList> bindableDisks() const { return &this->bDisks; }

signals:
	void activeChanged();
	void intervalMsChanged();
	void readyChanged();
	void hostNameChanged();
	void osNameChanged();
	void osVersionChanged();
	void osBuildChanged();
	void boardVendorChanged();
	void boardModelChanged();
	void uptimeSecondsChanged();
	void cpuNameChanged();
	void cpuCoresChanged();
	void cpuThreadsChanged();
	void cpuBaseMhzChanged();
	void cpuMaxMhzChanged();
	void cpuL3BytesChanged();
	void cpuUsageChanged();
	void cpuMhzChanged();
	void cpuTemperatureChanged();
	void memoryTotalChanged();
	void memoryUsedChanged();
	void swapTotalChanged();
	void swapUsedChanged();
	void gpusChanged();
	void disksChanged();
	void updated();

private:
	friend class SystemMonitorWorker;

	void applyStatic(const SystemMonitorStaticInfo& info);
	void applySample(const SystemMonitorSample& sample);

	bool mActive = false;
	int mIntervalMs = 1000;
	std::unique_ptr<SystemMonitorWorker> worker;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, bool, bReady, &SystemMonitor::readyChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bHostName, &SystemMonitor::hostNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bOsName, &SystemMonitor::osNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bOsVersion, &SystemMonitor::osVersionChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bOsBuild, &SystemMonitor::osBuildChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bBoardVendor, &SystemMonitor::boardVendorChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bBoardModel, &SystemMonitor::boardModelChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bUptimeSeconds, &SystemMonitor::uptimeSecondsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QString, bCpuName, &SystemMonitor::cpuNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, int, bCpuCores, &SystemMonitor::cpuCoresChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, int, bCpuThreads, &SystemMonitor::cpuThreadsChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemMonitor, qreal, bCpuBaseMhz, qQNaN(), &SystemMonitor::cpuBaseMhzChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemMonitor, qreal, bCpuMaxMhz, qQNaN(), &SystemMonitor::cpuMaxMhzChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bCpuL3Bytes, &SystemMonitor::cpuL3BytesChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qreal, bCpuUsage, &SystemMonitor::cpuUsageChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemMonitor, qreal, bCpuMhz, qQNaN(), &SystemMonitor::cpuMhzChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(SystemMonitor, qreal, bCpuTemperature, qQNaN(), &SystemMonitor::cpuTemperatureChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bMemoryTotal, &SystemMonitor::memoryTotalChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bMemoryUsed, &SystemMonitor::memoryUsedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bSwapTotal, &SystemMonitor::swapTotalChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, qint64, bSwapUsed, &SystemMonitor::swapUsedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QVariantList, bGpus, &SystemMonitor::gpusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(SystemMonitor, QVariantList, bDisks, &SystemMonitor::disksChanged);
	// clang-format on
};

} // namespace qs::windows::sys
