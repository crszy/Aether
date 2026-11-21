#include <qapplication.h>
#include <qcoreapplication.h>
#include <qcryptographichash.h>
#include <qdebug.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfile.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qlist.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qprocess.h>
#include <qqmldebug.h>
#include <qquickwindow.h>
#include <qstandardpaths.h>
#include <qstring.h>
#include <qtenvironmentvariables.h>
#include <qtextstream.h>
#include <qthreadpool.h>
#include <qtimer.h>

#include "../core/common.hpp"
#include "../core/instanceinfo.hpp"
#include "../core/logcat.hpp"
#include "../core/logging.hpp"
#include "../core/paths.hpp"
#include "../core/plugin.hpp"
#include "../core/qmlglobal.hpp"
#include "../core/rootwrapper.hpp"
#include "../ipc/ipc.hpp"
#include "build.hpp"
#include "launch_p.hpp"

#ifdef _WIN32
#include <stdio.h>
#endif

#if CRASH_HANDLER
#ifdef _WIN32
#include "../windows/crash/handler.hpp"
#else
#include "../crash/handler.hpp"
#endif
#endif

namespace qs::launch {

namespace {

template <typename T>
QString base36Encode(T number) {
	const QString digits = "0123456789abcdefghijklmnopqrstuvwxyz";
	QString result;

	do {
		result.prepend(digits[number % 36]);
		number /= 36;
	} while (number > 0);

	for (auto i = 0; i < result.length() / 2; i++) {
		auto opposite = result.length() - i - 1;
		auto c = result.at(i);
		result[i] = result.at(opposite);
		result[opposite] = c;
	}

	return result;
}

} // namespace

namespace {
QS_LOGGING_CATEGORY(logStartup, "quickshell.startup", QtWarningMsg);
}

int launch(const LaunchArgs& args, char** argv) {
	auto timer = QElapsedTimer();
	timer.start();
	auto pathId = QCryptographicHash::hash(args.configPath.toUtf8(), QCryptographicHash::Md5).toHex();
	auto shellId = QString(pathId);

	qInfo() << "Launching config:" << args.configPath;

	auto file = QFile(args.configPath);
	if (!file.open(QFile::ReadOnly | QFile::Text)) {
		qCritical() << "Could not open config file" << args.configPath;
		return -1;
	}

	struct {
		bool useQApplication = false;
		bool nativeTextRendering = false;
		bool desktopSettingsAware = true;
		bool useSystemStyle = false;
		QString iconTheme = qEnvironmentVariable("QS_ICON_THEME");
		QHash<QString, QString> envOverrides;
		QHash<QString, QString> defaultEnv;
		QString appId = qEnvironmentVariable("QS_APP_ID");
		bool dropExpensiveFonts = false;
		QString dataDir;
		QString stateDir;
		QString cacheDir;
	} pragmas;

	auto stream = QTextStream(&file);
	while (!stream.atEnd()) {
		auto line = stream.readLine().trimmed();
		if (line.startsWith("//@ pragma ")) {
			auto pragma = line.sliced(11).trimmed();

			auto isEnv = pragma.startsWith("Env ");
			auto isDefaultEnv = pragma.startsWith("DefaultEnv ");

			if (isEnv || isDefaultEnv) {
				auto content = pragma.sliced(isDefaultEnv ? 11 : 4);
				auto splitIdx = content.indexOf('=');

				if (splitIdx == -1) {
					qCritical() << "Env pragma" << pragma << "not in the form 'VAR = VALUE'";
					return -1;
				}

				auto var = content.sliced(0, splitIdx).trimmed();
				auto val = content.sliced(splitIdx + 1).trimmed();

				if (isDefaultEnv) pragmas.defaultEnv.insert(var, val);
				else pragmas.envOverrides.insert(var, val);
			} else if (pragma == "UseQApplication") pragmas.useQApplication = true;
			else if (pragma == "NativeTextRendering") pragmas.nativeTextRendering = true;
			else if (pragma == "IgnoreSystemSettings") pragmas.desktopSettingsAware = false;
			else if (pragma == "RespectSystemStyle") pragmas.useSystemStyle = true;
			else if (pragma == "DropExpensiveFonts") pragmas.dropExpensiveFonts = true;
			else if (pragma.startsWith("IconTheme ")) pragmas.iconTheme = pragma.sliced(10);
			else if (pragma.startsWith("AppId ")) {
				pragmas.appId = pragma.sliced(6).trimmed();
			} else if (pragma.startsWith("ShellId ")) {
				shellId = pragma.sliced(8).trimmed();
			} else if (pragma.startsWith("DataDir ")) {
				pragmas.dataDir = pragma.sliced(8).trimmed();
			} else if (pragma.startsWith("StateDir ")) {
				pragmas.stateDir = pragma.sliced(9).trimmed();
			} else if (pragma.startsWith("CacheDir ")) {
				pragmas.cacheDir = pragma.sliced(9).trimmed();
			} else {
				qWarning() << "Unrecognized pragma" << pragma;
			}
		} else if (line.startsWith("import")) break;
	}

	file.close();

	if (!pragmas.iconTheme.isEmpty()) {
		QIcon::setThemeName(pragmas.iconTheme);
	}

	qInfo() << "Shell ID:" << shellId << "Path ID" << pathId;

	auto launchTime = qs::Common::LAUNCH_TIME.toSecsSinceEpoch();
	auto appId = pragmas.appId.isEmpty() ? QStringLiteral("org.quickshell") : pragmas.appId;

	InstanceInfo::CURRENT = InstanceInfo {
	    .instanceId = base36Encode(QCoreApplication::applicationPid()) + base36Encode(launchTime),
	    .configPath = args.configPath,
	    .shellId = shellId,
	    .appId = appId,
	    .launchTime = qs::Common::LAUNCH_TIME,
	    .pid = static_cast<pid_t>(QCoreApplication::applicationPid()),
	    .display = getDisplayConnection(),
	};

#if CRASH_HANDLER
	if (qEnvironmentVariableIsSet("QS_DISABLE_CRASH_HANDLER")) {
		qInfo() << "Crash handling disabled.";
	} else {
		crash::CrashHandler::init();

		auto* log = LogManager::instance();
		crash::CrashHandler::setRelaunchInfo({
		    .instance = InstanceInfo::CURRENT,
		    .noColor = !log->colorLogs,
		    .timestamp = log->timestampLogs,
		    .sparseLogsOnly = log->isSparse(),
		    .defaultLogLevel = log->defaultLevel(),
		    .logRules = log->rulesString(),
		});
	}
#endif

	QsPaths::init(shellId, pathId, pragmas.dataDir, pragmas.stateDir, pragmas.cacheDir);
	QsPaths::instance()->linkRunDir();
	QsPaths::instance()->linkPathDir();

	Common::INITIAL_ENVIRONMENT = QProcessEnvironment::systemEnvironment();

	if (!pragmas.useSystemStyle) {
		qunsetenv("QT_STYLE_OVERRIDE");
		qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
	}

	for (auto [var, val]: pragmas.defaultEnv.asKeyValueRange()) {
		if (!qEnvironmentVariableIsSet(var.toUtf8())) qputenv(var.toUtf8(), val.toUtf8());
	}

	for (auto [var, val]: pragmas.envOverrides.asKeyValueRange()) {
		qputenv(var.toUtf8(), val.toUtf8());
	}

	pragmas.dropExpensiveFonts |= qEnvironmentVariableIntValue("QS_DROP_EXPENSIVE_FONTS") == 1;

#ifdef _WIN32
	if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
	}

	// Aether: the Linux shells take the user's name from $USER / $LOGNAME, which Windows does not set - they showed
	// "user". The name is Aether's "displayName" setting (~/.config/caelestia/windows.json, eg. "fora"), else the
	// Windows account; everything started from here (Ryoku and its programs too) inherits it.
	{
		QString name;
		QFile settings(QDir::homePath() + "/.config/caelestia/windows.json");
		if (settings.open(QFile::ReadOnly)) {
			name = QJsonDocument::fromJson(settings.readAll()).object().value("displayName").toString().trimmed();
		}
		if (name.isEmpty()) name = qEnvironmentVariable("USERNAME");
		if (!name.isEmpty()) {
			if (!qEnvironmentVariableIsSet("USER")) qputenv("USER", name.toUtf8());
			if (!qEnvironmentVariableIsSet("LOGNAME")) qputenv("LOGNAME", name.toUtf8());
		}
		if (!qEnvironmentVariableIsSet("HOSTNAME")) qputenv("HOSTNAME", qgetenv("COMPUTERNAME"));
	}

	_setmaxstdio(8192);

	if (pragmas.dropExpensiveFonts) {
		qWarning() << "DropExpensiveFonts is not supported on Windows, ignoring";
	}
#else
	if (pragmas.dropExpensiveFonts) {
		if (auto* runDir = QsPaths::instance()->instanceRunDir()) {
			auto baseConfigPath = qEnvironmentVariable("FONTCONFIG_FILE");
			if (baseConfigPath.isEmpty()) baseConfigPath = "/etc/fonts/fonts.conf";

			auto filterPath = runDir->filePath("fonts-override.conf");
			auto filterFile = QFile(filterPath);
			if (filterFile.open(QFile::WriteOnly | QFile::Truncate | QFile::Text)) {
				auto filterTemplate = QStringLiteral(R"(<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
	<include ignore_missing="no">%1</include>
	<selectfont>
		<rejectfont>
			<pattern>
				<patelt name="fontwrapper">
					<string>woff</string>
				</patelt>
			</pattern>
			<pattern>
				<patelt name="fontwrapper">
					<string>woff2</string>
				</patelt>
			</pattern>
		</rejectfont>
	</selectfont>
</fontconfig>
)");

				QTextStream(&filterFile) << filterTemplate.arg(baseConfigPath);
				filterFile.close();
				qputenv("FONTCONFIG_FILE", filterPath.toUtf8());
			} else {
				qCritical() << "Could not write fontconfig filter to" << filterPath;
			}
		} else {
			qCritical() << "Could not create fontconfig filter: instance run directory unavailable";
		}
	}
#endif

	// The qml engine currently refuses to cache non file (qsintercept) paths.

	// if (auto* cacheDir = QsPaths::instance()->cacheDir()) {
	// 	auto qmlCacheDir = cacheDir->filePath("qml-engine-cache");
	// 	qputenv("QML_DISK_CACHE_PATH", qmlCacheDir.toLocal8Bit());
	//
	// 	if (!qEnvironmentVariableIsSet("QML_DISK_CACHE")) {
	// 		qputenv("QML_DISK_CACHE", "aot,qmlc");
	// 	}
	// }

	// While the simple animation driver can lead to better animations in some cases,
	// it also can cause excessive repainting at excessively high framerates which can
	// lead to noticeable amounts of gpu usage, including overheating on some systems.
	// This gets worse the more windows are open, as repaints trigger on all of them for
	// some reason. See QTBUG-126099 for details.

	// if (!qEnvironmentVariableIsSet("QSG_USE_SIMPLE_ANIMATION_DRIVER")) {
	// 	qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", "1");
	// }

	// Some programs place icons in the pixmaps folder instead of the icons folder.
	// This seems to be controlled by the QPA and qt6ct does not provide it.
	{
		QList<QString> dataPaths;

		if (qEnvironmentVariableIsSet("XDG_DATA_DIRS")) {
			auto var = qEnvironmentVariable("XDG_DATA_DIRS");
			dataPaths = var.split(QDir::listSeparator(), Qt::SkipEmptyParts);
		} else {
#ifdef _WIN32
			dataPaths = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
#else
			dataPaths.push_back("/usr/local/share");
			dataPaths.push_back("/usr/share");
#endif
		}

		auto fallbackPaths = QIcon::fallbackSearchPaths();

		for (auto& path: dataPaths) {
			auto newPath = QDir(path).filePath("pixmaps");

			if (!fallbackPaths.contains(newPath)) {
				fallbackPaths.push_back(newPath);
			}
		}

		QIcon::setFallbackSearchPaths(fallbackPaths);
	}

	QGuiApplication::setDesktopSettingsAware(pragmas.desktopSettingsAware);

	QGuiApplication* app = nullptr;
	auto qArgC = 0;

	if (pragmas.useQApplication) {
		app = new QApplication(qArgC, argv);
	} else {
		app = new QGuiApplication(qArgC, argv);
	}

	auto appMs = timer.restart();
	LogManager::initThreadLogging();
	LogManager::initFs();
	qCDebug(logStartup) << "Created the application in" << appMs << "ms, logging in" << timer.restart() << "ms";

	RootWrapper::prefetch(args.configPath);

	QGuiApplication::setDesktopFileName(appId);

	if (args.debugPort != -1) {
		QQmlDebuggingEnabler::enableDebugging(true);
		auto wait = args.waitForDebug ? QQmlDebuggingEnabler::WaitForClient
		                              : QQmlDebuggingEnabler::DoNotWaitForClient;
#ifdef _WIN32
		QQmlDebuggingEnabler::startTcpDebugServer(args.debugPort, wait, QStringLiteral("127.0.0.1"));
#else
		QQmlDebuggingEnabler::startTcpDebugServer(args.debugPort, wait);
#endif
	}

	// This needs to run early to get the first connection to QGuiApplication::screenAdded() in Qs.
	// If we don't do that, attempts to get a QuickshellScreenInfo from a QScreen will fail in screenAdded bindings.
	QuickshellTracked::init();

	timer.restart();
	QsEnginePlugin::initPlugins();
	qCDebug(logStartup) << "Initialized plugins in" << timer.restart() << "ms";

	// Base window transparency appears to be additive.
	// Use a fully transparent window with a colored rect.
	QQuickWindow::setDefaultAlphaBuffer(true);

	if (pragmas.nativeTextRendering) {
		QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
	}

	qs::ipc::IpcServer::start();
	QsPaths::instance()->createLock();
	qCDebug(logStartup) << "Started IPC and took the lock in" << timer.restart() << "ms";

	auto root = RootWrapper(args.configPath, shellId);
	QGuiApplication::setQuitOnLastWindowClosed(false);

#ifdef _WIN32
	if (auto* runDir = QsPaths::instance()->baseRunDir()) {
		QTimer::singleShot(30000, app, [base = *runDir]() {
			QThreadPool::globalInstance()->start([base]() { QsPaths::pruneDeadInstances(base, 5); });
		});
	}
#endif

	exitDaemon(0);

	auto code = QGuiApplication::exec();
	delete app;
	return code;
}

} // namespace qs::launch
