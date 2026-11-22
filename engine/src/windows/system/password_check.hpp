#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

namespace qs::windows::sys {

///! Checks a password against the signed-in Windows account (LogonUser), off the GUI thread.
/// Used by the Pam stand-in so a QML lock screen can unlock with your Windows password.
/// The password is only held for the check and wiped afterwards; nothing is stored.
class PasswordCheck: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	/// A shell-only PIN/password, as PBKDF2-SHA256 (100 000 rounds) of the UTF-8 text with `pinSalt`,
	/// both hex. When set, verify() checks against it instead of the Windows account.
	Q_PROPERTY(QString pinHash READ pinHash WRITE setPinHash NOTIFY pinChanged);
	Q_PROPERTY(QString pinSalt READ pinSalt WRITE setPinSalt NOTIFY pinChanged);

public:
	explicit PasswordCheck(QObject* parent = nullptr): QObject(parent) {}

	/// Starts a check; `finished` reports the result. Without a PIN, a blank password counts as correct only
	/// when the account really has no password (Windows answers ERROR_ACCOUNT_RESTRICTION for that).
	Q_INVOKABLE void verify(const QString& password);
	/// "DOMAIN\\user" of the signed-in account.
	Q_INVOKABLE static QString userName();
	/// 16 random bytes as hex, for a new PIN.
	Q_INVOKABLE static QString newSalt();
	/// The stored form of a PIN (see pinHash).
	Q_INVOKABLE static QString hashPin(const QString& pin, const QString& saltHex);

	[[nodiscard]] QString pinHash() const { return this->mPinHash; }
	[[nodiscard]] QString pinSalt() const { return this->mPinSalt; }
	void setPinHash(const QString& v) { if (v != this->mPinHash) { this->mPinHash = v; emit this->pinChanged(); } }
	void setPinSalt(const QString& v) { if (v != this->mPinSalt) { this->mPinSalt = v; emit this->pinChanged(); } }

signals:
	void finished(bool ok, const QString& message);
	void pinChanged();

private:
	QString mPinHash;
	QString mPinSalt;
};

} // namespace qs::windows::sys
