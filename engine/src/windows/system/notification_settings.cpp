#include "notification_settings.hpp"

#include <qt_windows.h>

#include <shellapi.h>

#include <qlogging.h>
#include <qloggingcategory.h>

#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logNotificationSettings, "quickshell.windows.notificationsettings", QtWarningMsg);

void openUri(const wchar_t* uri) {
	auto result = reinterpret_cast<INT_PTR>( // NOLINT
	    ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL)
	);

	if (result <= 32) {
		qCWarning(logNotificationSettings) << "Could not open" << QString::fromWCharArray(uri) << ":"
		                                   << result;
	}
}
} // namespace

void NotificationSettings::openSettings() { openUri(L"ms-settings:notifications"); }
void NotificationSettings::openAccessSettings() { openUri(L"ms-settings:privacy-notifications"); }

} // namespace qs::windows::sys
