#include <qcoreapplication.h>
#include <qdir.h>
#include <qdiriterator.h>
#include <qelapsedtimer.h>
#include <qevent.h>
#include <qfileinfo.h>
#include <qfont.h>
#include <qfontdatabase.h>
#include <qguiapplication.h>
#include <qicon.h>
#include <qlist.h>
#include <qlogging.h>
#include <qplugin.h>
#include <qpointer.h>
#include <qqml.h>
#include <qquickwindow.h>
#include <qrawfont.h>
#include <qstring.h>
#include <qtimer.h>

#include "../core/logcat.hpp"
#include "../core/plugin.hpp"
#include "animation_timing.hpp"
#include "appbar.hpp"
#include "desktopentry_backend.hpp"
#include "hotkeys.hpp"
#include "../core/generation.hpp"
#include "../core/instanceinfo.hpp"
#include "panel_window.hpp"
#include "util.hpp"
#include "../io/processcore.hpp"

#include <dwmapi.h>

Q_IMPORT_PLUGIN(QsWindowsSceneGraphPlugin);

namespace {

QS_LOGGING_CATEGORY(logStartup, "quickshell.startup", QtWarningMsg);

constexpr const char* PERUSER_FONT_PREFIX = "jetbrainsmononerdfont-";
constexpr const char* PERUSER_FONT_REGULAR_FILE = "jetbrainsmononerdfont-regular.ttf";
constexpr const char* PERUSER_FONT_FAMILY = "JetBrainsMono Nerd Font";

bool allPeruserFontsPresent(const QDir& dir, const QDir& userFontsDir) {
	if (!userFontsDir.exists()) return false;

	auto iter = QDirIterator(
	    dir.path(),
	    {"*.ttf", "*.otf", "*.ttc"},
	    QDir::Files,
	    QDirIterator::Subdirectories
	);
	auto any = false;

	while (iter.hasNext()) {
		iter.next();
		auto name = iter.fileName();
		if (!name.startsWith(PERUSER_FONT_PREFIX, Qt::CaseInsensitive)) continue;
		any = true;
		if (!userFontsDir.exists(name)) return false;
	}

	return any;
}

void loadBundledFonts() {
	auto dir = QDir(QCoreApplication::applicationDirPath() + "/fonts");
	if (!dir.exists()) return;

	auto userFontsDir = QDir(qEnvironmentVariable("LocalAppData") + "/Microsoft/Windows/Fonts");
	auto skipPeruser = allPeruserFontsPresent(dir, userFontsDir);
	auto installedFamilies = QFontDatabase::families();

	enum class Select { NonPeruser, PeruserRegularOnly, PeruserAll };

	auto load = [&](Select which) {
		auto iter = QDirIterator(
		    dir.path(),
		    {"*.ttf", "*.otf", "*.ttc"},
		    QDir::Files,
		    QDirIterator::Subdirectories
		);

		while (iter.hasNext()) {
			auto path = iter.next();
			auto name = iter.fileName();
			auto isPeruser = name.startsWith(PERUSER_FONT_PREFIX, Qt::CaseInsensitive);

			if (which == Select::NonPeruser) {
				if (isPeruser) continue;
				auto family = QRawFont(path, 12).familyName();
				if (!family.isEmpty() && installedFamilies.contains(family, Qt::CaseInsensitive)) {
					qInfo() << "Using the installed" << family << "instead of the bundled" << name;
					continue;
				}
			} else if (which == Select::PeruserRegularOnly) {
				if (!isPeruser || name.compare(PERUSER_FONT_REGULAR_FILE, Qt::CaseInsensitive) != 0)
					continue;
			} else {
				if (!isPeruser) continue;
			}

			if (QFontDatabase::addApplicationFont(path) == -1) {
				qWarning() << "Failed to load bundled font" << path;
			}
		}
	};

	auto timer = QElapsedTimer();
	timer.start();
	load(Select::NonPeruser);

	if (!skipPeruser) load(Select::PeruserRegularOnly);
	qCDebug(logStartup) << "Added the bundled font files in" << timer.restart() << "ms";

	if (!QFontDatabase::families().contains(QString(PERUSER_FONT_FAMILY))) {
		load(Select::PeruserAll);
	}
	qCDebug(logStartup) << "Listed the system fonts in" << timer.restart() << "ms";

	QFont::insertSubstitution("JetBrains Mono NF", PERUSER_FONT_FAMILY);
	QFont::insertSubstitution("JetBrains Mono", PERUSER_FONT_FAMILY);
}

void addBundledIconPath() {
	auto dir = QCoreApplication::applicationDirPath() + "/icons";
	if (!QDir(dir).exists()) return;

	auto paths = QIcon::fallbackSearchPaths();
	if (!paths.contains(dir)) paths.prepend(dir);
	QIcon::setFallbackSearchPaths(paths);
}

void setCloaked(QWindow* window, bool cloaked) {
	if (window == nullptr || window->handle() == nullptr) return;
	BOOL value = cloaked ? TRUE : FALSE;
	DwmSetWindowAttribute(qs::windows::hwndOf(window), DWMWA_CLOAK, &value, sizeof(value));
}

class FirstFrameCloak: public QObject {
public:
	bool eventFilter(QObject* object, QEvent* event) override {
		if (event->type() != QEvent::PlatformSurface) return false;

		auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
		if (surfaceEvent->surfaceEventType() != QPlatformSurfaceEvent::SurfaceCreated) return false;

		auto* window = qobject_cast<QQuickWindow*>(object);
		if (window == nullptr || window->parent() != nullptr || window->type() != Qt::Window) return false;
		if (qobject_cast<qs::windows::WinProxiedWindow*>(window) != nullptr) return false;

		setCloaked(window, true);

		auto uncloak = [guard = QPointer<QQuickWindow>(window)]() { setCloaked(guard, false); };
		QObject::connect(
		    window,
		    &QQuickWindow::frameSwapped,
		    window,
		    uncloak,
		    static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection)
		);
		QTimer::singleShot(2000, window, uncloak);

		return false;
	}
};

class WindowsPlugin: public QsEnginePlugin {
	QString name() override { return "windows"; }
	QList<QString> dependencies() override { return {"window"}; }

	bool applies() override { return QGuiApplication::platformName() == "windows"; }

	void init() override {
		qs::windows::optOutOfProcessPowerThrottling();
		qs::io::process::stopOrphanedHelpers(); // cava left by an earlier (killed) shell
		qs::windows::WinAppBar::removeStale();
		qs::windows::installAnimationTiming();
		auto timer = QElapsedTimer();
		timer.start();
		loadBundledFonts();
		qCDebug(logStartup) << "Loaded bundled fonts in" << timer.restart() << "ms";
		addBundledIconPath();
		qs::windows::WindowsDesktopEntryBackend::install();
		QCoreApplication::instance()->installEventFilter(new FirstFrameCloak());
		qCDebug(logStartup) << "Installed the desktop entry backend in" << timer.restart() << "ms";
	}

	// key binds for every config, not only the ones whose QML uses Aether's Hotkeys: a Linux shell installed from the
	// store gets <config>/defaults/windows/keybinds.json (Super -> its own launcher), and nothing in it asks for them
	void constructGeneration(EngineGeneration& generation) override {
		auto entry = QFileInfo(InstanceInfo::CURRENT.configPath).fileName();
		if (entry.compare("shell.qml", Qt::CaseInsensitive) != 0) return;
		if (!QFileInfo::exists(generation.rootPath.filePath("defaults/windows/keybinds.json"))) return;
		qs::windows::hotkeys::HotkeyManager::instance()->setShellDir(generation.rootPath.path());
	}

	void registerTypes() override {
		qmlRegisterType<qs::windows::WinPanelInterface>(
		    "Quickshell._WindowsOverlay",
		    1,
		    0,
		    "PanelWindow"
		);

		qmlRegisterModuleImport(
		    "Quickshell",
		    QQmlModuleImportModuleAny,
		    "Quickshell._WindowsOverlay",
		    QQmlModuleImportLatest
		);
	}
};

QS_REGISTER_PLUGIN(WindowsPlugin);

} // namespace
