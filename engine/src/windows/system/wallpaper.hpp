#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qvariant.h>

namespace qs::windows::sys {

class Wallpaper: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;
	Q_PROPERTY(bool lockScreenImageSupported READ lockScreenImageSupported NOTIFY lockScreenImageSupportedChanged);
	Q_PROPERTY(bool systemDarkMode READ systemDarkMode NOTIFY systemDarkModeChanged);
	Q_PROPERTY(QString currentDesktop READ currentDesktop NOTIFY currentDesktopChanged);
	Q_PROPERTY(QVariantList desktops READ desktops NOTIFY desktopsChanged);

public:
	explicit Wallpaper(QObject* parent = nullptr);

	Q_INVOKABLE static QString currentWallpaper();

	Q_INVOKABLE static bool setWallpaper(const QString& path, bool animate = true);

	Q_INVOKABLE static void setWallpaperInBackground(const QString& path);

	Q_INVOKABLE bool setDesktopWallpapers(const QVariantMap& paths, const QString& fallback);

	Q_INVOKABLE static bool isDarkMode();

	Q_INVOKABLE static void setDarkMode(bool dark);

	Q_INVOKABLE static bool setAccentColor(const QString& hex);

	Q_INVOKABLE static QString matugenPath();

	[[nodiscard]] bool lockScreenImageSupported() const { return this->mLockScreenImageSupported; }

	[[nodiscard]] bool systemDarkMode() const { return this->mSystemDarkMode; }

	[[nodiscard]] QString currentDesktop();
	[[nodiscard]] QVariantList desktops();

	Q_INVOKABLE void setLockScreenImage(const QString& path);

	Q_INVOKABLE static void openLockScreenPersonalizationSettings();

	Q_INVOKABLE static QString lockScreenMode();

signals:
	void lockScreenImageSupportedChanged();
	void lockScreenImageApplied(bool ok);
	void systemDarkModeChanged();
	void currentDesktopChanged();
	void desktopsChanged();

private:
	void applyLockScreenResult(bool ok, const QString& errorMessage);
	void refreshSystemDarkMode();
	void trackDesktops();

	bool mLockScreenImageSupported = true;
	bool mLockScreenAttemptRunning = false;
	bool mSystemDarkMode = false;
	bool mTrackingDesktops = false;
};

} // namespace qs::windows::sys
