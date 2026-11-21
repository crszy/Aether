#include "credentials.hpp"

#include <qt_windows.h>

#include <wincred.h>

#include <qlogging.h>
#include <qloggingcategory.h>

#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logCredentials, "quickshell.windows.credentials", QtWarningMsg);
}

bool Credentials::write(const QString& target, const QString& secret) {
	auto utf16 = secret;
	auto bytes = utf16.utf16();

	CREDENTIALW cred {};
	cred.Type = CRED_TYPE_GENERIC;
	cred.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16())); // NOLINT
	cred.CredentialBlobSize = static_cast<DWORD>((utf16.size()) * sizeof(wchar_t));
	cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ushort*>(bytes)); // NOLINT
	cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

	if (!CredWriteW(&cred, 0)) {
		qCWarning(logCredentials) << "CredWriteW failed for" << target << ":" << GetLastError();
		return false;
	}
	return true;
}

QString Credentials::read(const QString& target) {
	PCREDENTIALW cred = nullptr;
	auto* name = reinterpret_cast<LPCWSTR>(target.utf16()); // NOLINT

	if (!CredReadW(name, CRED_TYPE_GENERIC, 0, &cred)) {
		return QString();
	}

	QString result;
	if (cred != nullptr) {
		result = QString::fromWCharArray(
		    reinterpret_cast<const wchar_t*>(cred->CredentialBlob), // NOLINT
		    static_cast<int>(cred->CredentialBlobSize / sizeof(wchar_t))
		);
		CredFree(cred);
	}

	return result;
}

bool Credentials::remove(const QString& target) {
	auto* name = reinterpret_cast<LPCWSTR>(target.utf16()); // NOLINT
	if (!CredDeleteW(name, CRED_TYPE_GENERIC, 0)) {
		auto err = GetLastError();
		if (err != ERROR_NOT_FOUND) {
			qCWarning(logCredentials) << "CredDeleteW failed for" << target << ":" << err;
		}
		return false;
	}
	return true;
}

bool Credentials::exists(const QString& target) {
	PCREDENTIALW cred = nullptr;
	auto* name = reinterpret_cast<LPCWSTR>(target.utf16()); // NOLINT
	if (!CredReadW(name, CRED_TYPE_GENERIC, 0, &cred)) return false;
	if (cred != nullptr) CredFree(cred);
	return true;
}

} // namespace qs::windows::sys
