#include "fingerprint.hpp"

#include <thread>
#include <vector>

#include <qmetaobject.h>
#include <qpointer.h>

// clang-format off
#include <windows.h>
#include <winbio.h>
// clang-format on

namespace qs::windows::sys {

namespace {

bool sensorPresent() {
	WINBIO_UNIT_SCHEMA* units = nullptr;
	SIZE_T count = 0;
	if (FAILED(WinBioEnumBiometricUnits(WINBIO_TYPE_FINGERPRINT, &units, &count))) return false;
	if (units != nullptr) WinBioFree(units);
	return count > 0;
}

// the signed-in account's SID as a WinBio identity
bool currentIdentity(WINBIO_IDENTITY& identity) {
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, nullptr, 0, &size);
	std::vector<BYTE> buffer(size);
	bool ok = size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
	CloseHandle(token);
	if (!ok) return false;

	auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
	auto length = GetLengthSid(user->User.Sid);
	if (length > sizeof(identity.Value.AccountSid.Data)) return false;
	identity = {};
	identity.Type = WINBIO_ID_TYPE_SID;
	identity.Value.AccountSid.Size = length;
	return CopySid(length, identity.Value.AccountSid.Data, user->User.Sid) != 0;
}

QString describe(HRESULT hr, WINBIO_REJECT_DETAIL reject) {
	if (hr == WINBIO_E_NO_MATCH) return QStringLiteral("Fingerprint not recognized");
	if (hr == WINBIO_E_BAD_CAPTURE) {
		Q_UNUSED(reject);
		return QStringLiteral("Couldn't read the finger - try again");
	}
	if (hr == WINBIO_E_CANCELED) return QStringLiteral("cancelled");
	if (hr == WINBIO_E_ENROLLMENT_IN_PROGRESS) return QStringLiteral("The sensor is busy");
	if (hr == WINBIO_E_UNKNOWN_ID) return QStringLiteral("No fingerprint set up for this account (Windows Hello)");
	return QStringLiteral("Fingerprint unavailable (0x%1)").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0'));
}

} // namespace

Fingerprint::Fingerprint(QObject* parent): QObject(parent) { this->mAvailable = sensorPresent(); }

Fingerprint::~Fingerprint() { this->cancel(); }

void Fingerprint::refresh() {
	auto available = sensorPresent();
	if (available == this->mAvailable) return;
	this->mAvailable = available;
	emit this->availableChanged();
}

void Fingerprint::setScanning(bool scanning) {
	if (scanning == this->mScanning) return;
	this->mScanning = scanning;
	emit this->scanningChanged();
}

void Fingerprint::verify() {
	if (this->mScanning) return;
	if (!this->mAvailable) {
		emit this->finished(false, QStringLiteral("No fingerprint sensor"));
		return;
	}

	this->setScanning(true);
	auto session = std::make_shared<std::atomic<unsigned long>>(0);
	this->mSession = session;
	QPointer<Fingerprint> self(this);

	std::thread([self, session]() {
		bool ok = false;
		QString message;
		WINBIO_IDENTITY identity {};
		WINBIO_SESSION_HANDLE handle = 0;

		if (!currentIdentity(identity)) {
			message = QStringLiteral("Couldn't read the account");
		} else {
			HRESULT hr = WinBioOpenSession(
			    WINBIO_TYPE_FINGERPRINT,
			    WINBIO_POOL_SYSTEM,
			    WINBIO_FLAG_DEFAULT,
			    nullptr,
			    0,
			    WINBIO_DB_DEFAULT,
			    &handle
			);
			if (FAILED(hr)) {
				message = describe(hr, 0);
			} else {
				session->store(handle);
				WINBIO_UNIT_ID unit = 0;
				BOOLEAN match = FALSE;
				WINBIO_REJECT_DETAIL reject = 0;
				hr = WinBioVerify(handle, &identity, WINBIO_SUBTYPE_ANY, &unit, &match, &reject);
				ok = SUCCEEDED(hr) && match;
				if (!ok) message = describe(hr, reject);
				session->store(0);
				WinBioCloseSession(handle);
			}
		}

		QMetaObject::invokeMethod(
		    self.data(),
		    [self, ok, message] {
			    if (!self) return;
			    self->setScanning(false);
			    emit self->finished(ok, message);
		    },
		    Qt::QueuedConnection
		);
	}).detach();
}

void Fingerprint::cancel() {
	if (auto session = this->mSession) {
		if (auto handle = session->load(); handle != 0) WinBioCancel(handle);
	}
}

} // namespace qs::windows::sys
