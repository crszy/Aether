#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows::sys {

class IdlePower: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	// clang-format off
	Q_PROPERTY(bool hasBattery READ hasBattery NOTIFY hasBatteryChanged);
	Q_PROPERTY(bool lastWriteOk READ lastWriteOk NOTIFY lastWriteOkChanged);
	Q_PROPERTY(int screenOffAcMinutes READ screenOffAcMinutes WRITE setScreenOffAcMinutes NOTIFY screenOffAcMinutesChanged);
	Q_PROPERTY(int screenOffDcMinutes READ screenOffDcMinutes WRITE setScreenOffDcMinutes NOTIFY screenOffDcMinutesChanged);
	Q_PROPERTY(int sleepAcMinutes READ sleepAcMinutes WRITE setSleepAcMinutes NOTIFY sleepAcMinutesChanged);
	Q_PROPERTY(int sleepDcMinutes READ sleepDcMinutes WRITE setSleepDcMinutes NOTIFY sleepDcMinutesChanged);
	// clang-format on

public:
	explicit IdlePower(QObject* parent = nullptr);
	~IdlePower() override;
	Q_DISABLE_COPY_MOVE(IdlePower);

	[[nodiscard]] bool hasBattery() const { return this->mHasBattery; }
	[[nodiscard]] bool lastWriteOk() const { return this->mLastWriteOk; }

	[[nodiscard]] int screenOffAcMinutes() const { return this->mScreenOffAc; }
	void setScreenOffAcMinutes(int minutes);

	[[nodiscard]] int screenOffDcMinutes() const { return this->mScreenOffDc; }
	void setScreenOffDcMinutes(int minutes);

	[[nodiscard]] int sleepAcMinutes() const { return this->mSleepAc; }
	void setSleepAcMinutes(int minutes);

	[[nodiscard]] int sleepDcMinutes() const { return this->mSleepDc; }
	void setSleepDcMinutes(int minutes);

	Q_INVOKABLE void refresh();

	// Milliseconds since the last keyboard/mouse input anywhere in the session (GetLastInputInfo),
	// or -1 if it could not be read.
	Q_INVOKABLE static qint64 idleSeconds();

signals:
	void hasBatteryChanged();
	void lastWriteOkChanged();
	void screenOffAcMinutesChanged();
	void screenOffDcMinutesChanged();
	void sleepAcMinutesChanged();
	void sleepDcMinutesChanged();

private:
	void refreshBatteryPresence();
	void refreshTimeouts();

	bool mHasBattery = false;
	bool mLastWriteOk = true;

	int mScreenOffAc = 10;
	int mScreenOffDc = 5;
	int mSleepAc = 30;
	int mSleepDc = 15;

	void* mVideoNotify = nullptr;
	void* mStandbyNotify = nullptr;
};

} // namespace qs::windows::sys
