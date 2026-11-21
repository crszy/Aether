#include "registry_util.hpp"

#include <qlist.h>

namespace qs::windows::sys {

QString readRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extraFlags, &key) != ERROR_SUCCESS) return QString();

	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) {
		RegCloseKey(key);
		return QString();
	}

	if (type != REG_SZ && type != REG_EXPAND_SZ) {
		RegCloseKey(key);
		return QString();
	}

	QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
	auto ok =
	    RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
	RegCloseKey(key);
	if (ok != ERROR_SUCCESS) return QString();

	auto str = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buffer.constData()));
	while (str.endsWith(QChar(u'\0'))) str.chop(1);
	return str;
}

QByteArray
readRegistryBinary(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extraFlags, &key) != ERROR_SUCCESS) {
		return QByteArray();
	}

	DWORD type = 0;
	DWORD size = 0;
	if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0) {
		RegCloseKey(key);
		return QByteArray();
	}

	if (type != REG_BINARY) {
		RegCloseKey(key);
		return QByteArray();
	}

	QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
	auto ok =
	    RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
	RegCloseKey(key);
	if (ok != ERROR_SUCCESS) return QByteArray();

	return buffer;
}

bool writeRegistryString(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* name,
    const QString& value,
    REGSAM extraFlags
) {
	HKEY key = nullptr;
	auto result = RegCreateKeyExW(
	    root,
	    subkey,
	    0,
	    nullptr,
	    0,
	    KEY_SET_VALUE | extraFlags,
	    nullptr,
	    &key,
	    nullptr
	);
	if (result != ERROR_SUCCESS) return false;

	auto wide = value.toStdWString();
	auto ok = RegSetValueExW(
	    key,
	    name,
	    0,
	    REG_SZ,
	    reinterpret_cast<const BYTE*>(wide.c_str()),
	    static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t))
	);
	RegCloseKey(key);
	return ok == ERROR_SUCCESS;
}

bool writeRegistryBinary(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* name,
    const QByteArray& data,
    REGSAM extraFlags
) {
	HKEY key = nullptr;
	auto result = RegCreateKeyExW(
	    root,
	    subkey,
	    0,
	    nullptr,
	    0,
	    KEY_SET_VALUE | extraFlags,
	    nullptr,
	    &key,
	    nullptr
	);
	if (result != ERROR_SUCCESS) return false;

	auto ok = RegSetValueExW(
	    key,
	    name,
	    0,
	    REG_BINARY,
	    reinterpret_cast<const BYTE*>(data.constData()),
	    static_cast<DWORD>(data.size())
	);
	RegCloseKey(key);
	return ok == ERROR_SUCCESS;
}

bool deleteRegistryValue(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags) {
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE | extraFlags, &key) != ERROR_SUCCESS) {
		return false;
	}

	auto ok = RegDeleteValueW(key, name);
	RegCloseKey(key);
	return ok == ERROR_SUCCESS || ok == ERROR_FILE_NOT_FOUND;
}

QList<RegistryValue>
enumerateRegistryStrings(HKEY root, const wchar_t* subkey, REGSAM extraFlags) {
	auto results = QList<RegistryValue>();

	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extraFlags, &key) != ERROR_SUCCESS) {
		return results;
	}

	DWORD index = 0;
	for (;;) {
		wchar_t nameBuf[16384];
		DWORD nameLen = 16384;
		DWORD type = 0;
		BYTE dataBuf[32768];
		DWORD dataLen = sizeof(dataBuf);

		auto status =
		    RegEnumValueW(key, index, nameBuf, &nameLen, nullptr, &type, dataBuf, &dataLen);
		if (status == ERROR_NO_MORE_ITEMS) break;
		index++;
		if (status != ERROR_SUCCESS) continue;
		if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

		auto value = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(dataBuf));
		while (value.endsWith(QChar(u'\0'))) value.chop(1);

		results.append(RegistryValue {QString::fromWCharArray(nameBuf, static_cast<int>(nameLen)), value});
	}

	RegCloseKey(key);
	return results;
}

} // namespace qs::windows::sys
