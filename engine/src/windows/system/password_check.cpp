#include "password_check.hpp"

#include <string>
#include <thread>

#include <algorithm>

#include <qcryptographichash.h>
#include <qmetaobject.h>
#include <qpassworddigestor.h>
#include <qpointer.h>
#include <qrandom.h>
#include <qt_windows.h>
#define SECURITY_WIN32
#include <security.h>

namespace qs::windows::sys {

namespace {

std::wstring samName() {
	wchar_t buf[512];
	ULONG len = 512;
	if (GetUserNameExW(NameSamCompatible, buf, &len)) return std::wstring(buf, len);
	DWORD n = 512;
	if (GetUserNameW(buf, &n)) return std::wstring(L".\\") + buf;
	return {};
}

} // namespace

QString PasswordCheck::userName() { return QString::fromStdWString(samName()); }

QString PasswordCheck::newSalt() {
	QByteArray salt(16, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(salt.data()), salt.size() / 4);
	return QString::fromLatin1(salt.toHex());
}

QString PasswordCheck::hashPin(const QString& pin, const QString& saltHex) {
	auto key = QPasswordDigestor::deriveKeyPbkdf2(
	    QCryptographicHash::Sha256, pin.toUtf8(), QByteArray::fromHex(saltHex.toLatin1()), 100000, 32
	);
	return QString::fromLatin1(key.toHex());
}

void PasswordCheck::verify(const QString& password) {
	if (!this->mPinHash.isEmpty()) {
		// shell PIN: PBKDF2 is deliberately slow, so it runs off the GUI thread too
		QPointer<PasswordCheck> self(this);
		std::thread([self, pin = password, salt = this->mPinSalt, want = this->mPinHash]() mutable {
			auto got = hashPin(pin, salt).toLatin1();
			auto expect = want.toLower().toLatin1();
			pin.fill(QChar(0));
			// constant-time compare
			unsigned diff = got.size() == expect.size() ? 0u : 1u;
			for (qsizetype i = 0; i < std::min(got.size(), expect.size()); ++i) diff |= unsigned(got[i] ^ expect[i]);
			const bool ok = diff == 0;
			if (self) {
				QMetaObject::invokeMethod(self.data(), [self, ok] {
					if (self) emit self->finished(ok, ok ? QString() : QStringLiteral("Wrong PIN"));
				}, Qt::QueuedConnection);
			}
		}).detach();
		return;
	}

	auto sam = samName();
	auto slash = sam.find(L'\\');
	std::wstring domain = slash == std::wstring::npos ? L"." : sam.substr(0, slash);
	std::wstring user = slash == std::wstring::npos ? sam : sam.substr(slash + 1);
	auto pw = password.toStdWString();
	const bool blank = pw.empty();

	QPointer<PasswordCheck> self(this);
	std::thread([self, user = std::move(user), domain = std::move(domain), pw = std::move(pw), blank]() mutable {
		HANDLE token = nullptr;
		bool ok = LogonUserW(user.c_str(), domain.c_str(), pw.c_str(), LOGON32_LOGON_INTERACTIVE, LOGON32_PROVIDER_DEFAULT, &token);
		DWORD err = ok ? 0 : GetLastError();
		if (token != nullptr) CloseHandle(token);
		SecureZeroMemory(pw.data(), pw.size() * sizeof(wchar_t));

		// an account without a password: Windows refuses blank-password logons here with ERROR_ACCOUNT_RESTRICTION
		if (!ok && blank && err == ERROR_ACCOUNT_RESTRICTION) ok = true;

		QString message;
		if (!ok) {
			switch (err) {
			case ERROR_LOGON_FAILURE: message = QStringLiteral("Wrong password"); break;
			case ERROR_ACCOUNT_LOCKED_OUT: message = QStringLiteral("The account is locked out"); break;
			default: message = QStringLiteral("Windows could not check the password (error %1)").arg(err);
			}
		}
		if (self) {
			QMetaObject::invokeMethod(self.data(), [self, ok, message] {
				if (self) emit self->finished(ok, message);
			}, Qt::QueuedConnection);
		}
	}).detach();
}

} // namespace qs::windows::sys
