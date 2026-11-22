#pragma once

#include <qbytearray.h>
#include <qlist.h>
#include <qstring.h>
#include <qt_windows.h>

namespace qs::windows::sys {

struct RegistryValue {
	QString name;
	QString stringValue;
};

QString readRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags = 0);
QByteArray readRegistryBinary(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags = 0);
bool writeRegistryString(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* name,
    const QString& value,
    REGSAM extraFlags = 0
);
bool writeRegistryBinary(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* name,
    const QByteArray& data,
    REGSAM extraFlags = 0
);
bool deleteRegistryValue(HKEY root, const wchar_t* subkey, const wchar_t* name, REGSAM extraFlags = 0);
QList<RegistryValue>
enumerateRegistryStrings(HKEY root, const wchar_t* subkey, REGSAM extraFlags = 0);

} // namespace qs::windows::sys
