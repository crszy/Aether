#pragma once

#include <atomic>
#include <memory>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

///! Fingerprint unlock through the Windows Biometric Framework (the fingerprints set up in Windows Hello).
/// verify() waits for a finger on the sensor, off the GUI thread, and checks it belongs to the signed-in account -
/// no Windows dialog, so it works under a lock screen. The process needs the foreground (Windows gives the sensor
/// to the focused app), which a lock surface has. Used by the Pam stand-in for "fprint" configs (any shell's lock).
class Fingerprint: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	/// A fingerprint sensor is present (checked once, then on refresh()).
	Q_PROPERTY(bool available READ available NOTIFY availableChanged);
	/// A verify() is waiting for a finger.
	Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged);

public:
	explicit Fingerprint(QObject* parent = nullptr);
	~Fingerprint() override;
	Q_DISABLE_COPY_MOVE(Fingerprint);

	[[nodiscard]] bool available() const { return this->mAvailable; }
	[[nodiscard]] bool scanning() const { return this->mScanning; }

	/// Waits for one touch; `finished(ok, message)` reports it. A second call while scanning is ignored.
	Q_INVOKABLE void verify();
	/// Stops a waiting verify() (finished(false, "cancelled") follows).
	Q_INVOKABLE void cancel();
	/// Looks for a sensor again (e.g. one plugged in later).
	Q_INVOKABLE void refresh();

signals:
	void finished(bool ok, const QString& message);
	void availableChanged();
	void scanningChanged();

private:
	void setScanning(bool scanning);

	bool mAvailable = false;
	bool mScanning = false;
	// the open WinBio session of a running verify, for cancel(); shared with the worker thread
	std::shared_ptr<std::atomic<unsigned long>> mSession;
};

} // namespace qs::windows::sys
