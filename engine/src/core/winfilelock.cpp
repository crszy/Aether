#include "winfilelock.hpp"

#include <io.h>
#include <qfile.h>
#include <windows.h>

namespace qs::core::winlock {

namespace {

constexpr DWORD LOCK_OFFSET_LOW = 0xFFFFFFF0;
constexpr DWORD LOCK_OFFSET_HIGH = 0x7FFFFFFF;

HANDLE fileHandle(QFile& file) {
	auto fd = file.handle();
	if (fd == -1) return INVALID_HANDLE_VALUE;

	auto handle = _get_osfhandle(fd);
	if (handle == -1) return INVALID_HANDLE_VALUE;
	return reinterpret_cast<HANDLE>(handle); // NOLINT
}

OVERLAPPED lockRegion() {
	OVERLAPPED overlapped {};
	overlapped.Offset = LOCK_OFFSET_LOW;
	overlapped.OffsetHigh = LOCK_OFFSET_HIGH;
	return overlapped;
}

bool lock(HANDLE handle, DWORD flags) {
	auto overlapped = lockRegion();
	return LockFileEx(handle, flags, 0, 1, 0, &overlapped) != 0;
}

void unlock(HANDLE handle) {
	auto overlapped = lockRegion();
	UnlockFileEx(handle, 0, 1, 0, &overlapped);
}

} // namespace

bool lockExclusive(QFile& file) {
	auto handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;
	return lock(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY);
}

bool isLocked(QFile& file) {
	auto handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	if (lock(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)) {
		unlock(handle);
		return false;
	}

	auto error = GetLastError();
	return error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING;
}

bool waitUnlocked(QFile& file) {
	auto handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	if (!lock(handle, 0)) return false;
	unlock(handle);
	return true;
}

} // namespace qs::core::winlock
