# Builds dotfiles/caelestia-shell-win: the upstream Caelestia clone + the Windows overlay (dotfiles/caelestia-win)
# + CMake patches so the C++ plugin builds on Windows (clang-cl, Qt 6.8). The upstream clone is never edited.
#   py -I make-caelestia-win.py
import io, os, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'caelestia-shell')
OVL = os.path.join(HERE, 'caelestia-win')
DST = os.path.join(HERE, 'caelestia-shell-win')

def rd(p): return io.open(p, encoding='utf-8').read()
def wr(p, s): io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
def rep(p, a, b):
    p = os.path.join(DST, p); s = rd(p)
    assert s.count(a) == 1, (p, a[:80], s.count(a))
    wr(p, s.replace(a, b))

# 1. fresh copy (keep the build folder between runs)
if os.path.isdir(DST):
    for n in os.listdir(DST):
        if n == 'build': continue
        q = os.path.join(DST, n)
        shutil.rmtree(q) if os.path.isdir(q) else os.remove(q)
shutil.copytree(SRC, DST, dirs_exist_ok=True, ignore=shutil.ignore_patterns('.git'))

# 2. overlay the Windows files
n = 0
for root, _, files in os.walk(OVL):
    for f in files:
        s = os.path.join(root, f); d = os.path.join(DST, os.path.relpath(s, OVL))
        os.makedirs(os.path.dirname(d), exist_ok=True); shutil.copy2(s, d); n += 1

# 3. CMake: Linux libraries only off Windows
rep('plugin/CMakeLists.txt', '''find_package(PkgConfig REQUIRED)
pkg_check_modules(Qalculate IMPORTED_TARGET libqalculate REQUIRED)
pkg_check_modules(Pipewire IMPORTED_TARGET libpipewire-0.3 REQUIRED)
pkg_check_modules(Aubio IMPORTED_TARGET aubio REQUIRED)
pkg_check_modules(Cava IMPORTED_TARGET libcava QUIET)
if(NOT Cava_FOUND)
    pkg_check_modules(Cava IMPORTED_TARGET cava REQUIRED)
endif()
include(cmake/sensorslib.cmake)''', '''if(WIN32)
    # Windows build: libqalculate / PipeWire / aubio / cava / lm_sensors are replaced by Windows code
    foreach(t PkgConfig::Qalculate PkgConfig::Pipewire PkgConfig::Aubio PkgConfig::Cava Sensors::Sensors)
        add_library(${t} INTERFACE IMPORTED)
    endforeach()
    target_link_libraries(PkgConfig::Pipewire INTERFACE ole32)
    target_link_libraries(PkgConfig::Aubio INTERFACE powrprof)
    target_link_libraries(Sensors::Sensors INTERFACE gdi32)
else()
find_package(PkgConfig REQUIRED)
pkg_check_modules(Qalculate IMPORTED_TARGET libqalculate REQUIRED)
pkg_check_modules(Pipewire IMPORTED_TARGET libpipewire-0.3 REQUIRED)
pkg_check_modules(Aubio IMPORTED_TARGET aubio REQUIRED)
pkg_check_modules(Cava IMPORTED_TARGET libcava QUIET)
if(NOT Cava_FOUND)
    pkg_check_modules(Cava IMPORTED_TARGET cava REQUIRED)
endif()
include(cmake/sensorslib.cmake)
endif()''')
# GCC-style warning flags: clang-cl reads -Wall as -Weverything
rep('CMakeLists.txt', '''add_compile_options(
    -Wall -Wextra''', '''if(NOT MSVC)
add_compile_options(
    -Wall -Wextra''')
rep('CMakeLists.txt', '''    -Wunreachable-code
)''', '''    -Wunreachable-code
)
endif()''')
# 4. Qt 6.9-only API -> Qt 6.8 (enums are never 64-bit before 6.9; no QJsonObject::asKeyValueRange)
rep('plugin/include/util/metaenum.hpp', 'metaEnum.isValid() && !metaEnum.is64Bit()', 'metaEnum.isValid()')
rep('plugin/src/Caelestia/cutils.cpp', '!metaEnum.isValid() || metaEnum.is64Bit()', '!metaEnum.isValid()')
rep('plugin/src/Caelestia/Settings/objectnode.cpp', '    for (const auto [k, v] : json.asKeyValueRange()) {',
    '    for (auto it_ = json.constBegin(); it_ != json.constEnd(); ++it_) {\n'
    '        const QString kStr_ = it_.key(); const QStringView k = kStr_; const QJsonValue v = it_.value();')
rep('plugin/src/Caelestia/Settings/listnode.cpp', '#include <qjsonarray.h>', '#include <ranges>\n#include <qjsonarray.h>')
# QML: the Windows port's WlrLayershell attached object has no exclusionMode; it is a PanelWindow property anyway
for root, dirs, files in os.walk(DST):
    if 'build' in dirs: dirs.remove('build')
    for f in files:
        if f.endswith('.qml'):
            p = os.path.join(root, f); s = rd(p)
            if 'WlrLayershell.exclusionMode:' in s: wr(p, s.replace('WlrLayershell.exclusionMode:', 'exclusionMode:'))
# QML: the port's WlSessionLock stand-in already declares `signal unlock()`
rep('modules/lock/Lock.qml', '        signal unlock\n\n', '')
# QML: no Wayland IdleMonitor on Windows, and Windows already locks/sleeps on idle itself -> leave the daemon out
rep('shell.qml', '''    IdleMonitors {
        lock: lock
    }
''', '''    // IdleMonitors: not on Windows (no IdleMonitor type; Windows handles idle lock/sleep)
''')
# shaders: Qt Quick on Windows renders with Direct3D 11, so it needs the HLSL variants Linux builds skip
for d in ('Blobs', 'Components', 'Shaders'):
    rep('plugin/src/Caelestia/%s/CMakeLists.txt' % d, 'BATCHABLE OPTIMIZED NOHLSL NOMSL', 'BATCHABLE OPTIMIZED NOMSL')
# QML: Windows paths and system info
rep('utils/Paths.qml', 'readonly property string home: Quickshell.env("HOME")',
    'readonly property string home: (Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").replace(/\\\\/g, "/")')
p = os.path.join(DST, 'utils/SysInfo.qml'); s = rd(p)
s = s.replace('import qs.utils\n', 'import qs.utils\nimport Quickshell.Windows as Win\n', 1)
s = s.replace('readonly property string user: Quickshell.env("USER")', 'readonly property string user: Quickshell.env("USERNAME") || Quickshell.env("USER") || ""')
s = s.replace('readonly property string wm: Quickshell.env("XDG_CURRENT_DESKTOP") || Quickshell.env("XDG_SESSION_DESKTOP")', 'readonly property string wm: "Quickshell (Windows)"')
s = s.replace('readonly property string shell: Quickshell.env("SHELL").split("/").pop()', 'readonly property string shell: "PowerShell"')
# /etc/os-release, /proc and /sys -> Windows
for path in ('"/etc/os-release"', '"/proc/sys/kernel/osrelease"', '"/proc/sys/kernel/hostname"', '"/sys/class/dmi/id/sys_vendor"',
             '"/sys/class/dmi/id/product_name"', '"/sys/class/dmi/id/bios_version"', '"/proc/uptime"'):
    s = s.replace('path: ' + path, 'path: ""   // ' + path + ' does not exist on Windows')
s = s.replace('        onTriggered: fileUptime.reload()', '        onTriggered: root.updateUptime(Win.SystemMonitor.uptimeSeconds)')
a = s.rindex('\n}')
s = s[:a] + '''

    // ---- Windows ----
    function updateUptime(up: int): void {
        const days = Math.floor(up / 86400), hours = Math.floor((up % 86400) / 3600), minutes = Math.floor((up % 3600) / 60);
        const sep = Tr.trCtx(", ", "uptime component separator");
        const parts = [];
        if (days > 0) parts.push(Tr.trN("%n day", "%n days", days));
        if (hours > 0) parts.push(Tr.trN("%n hour", "%n hours", hours));
        if (minutes > 0 || parts.length === 0) parts.push(Tr.trN("%n minute", "%n minutes", minutes));
        root.uptime = parts.join(sep);
        root.uptimeShort = parts.slice(0, 2).join(sep);
    }
    Connections {
        target: Win.SystemMonitor
        function onUptimeSecondsChanged(): void { root.updateUptime(Win.SystemMonitor.uptimeSeconds); }
    }
    Component.onCompleted: {
        const os = Win.Session.osInfo();
        root.osName = os.name ?? "Windows";
        root.osPrettyName = `${os.name ?? "Windows"} ${os.version ?? ""}`.trim();
        root.osId = "windows";
        root.osIdLike = [];
        root.kernel = os.build ?? "";
        root.hostname = Quickshell.env("COMPUTERNAME") || "";
        // the port's SystemMonitor only samples while active; uptime needs nothing faster than this
        if (Win.SystemMonitor.intervalMs < 30000)
            Win.SystemMonitor.intervalMs = 30000;
        Win.SystemMonitor.active = true;
        root.updateUptime(Win.SystemMonitor.uptimeSeconds);
    }
}'''
wr(p, s)
# image cache: Qt on Windows will not load Caelestia's BC3 .ktx textures through Image ("Unsupported image
# format"), so cache PNGs instead (same keys, same eviction)
rep('plugin/src/Caelestia/Multimedia/imagecacheworker.cpp', 'constexpr auto k_suffix = ".ktx"_L1;',
    '#ifdef _WIN32\nconstexpr auto k_suffix = ".png"_L1;\n#else\nconstexpr auto k_suffix = ".ktx"_L1;\n#endif')
rep('plugin/src/Caelestia/Multimedia/imagecacheworker.cpp', '    const auto data = ktx::encodeBc3(image);',
    '#ifdef _WIN32\n    QByteArray data;\n    {\n        QBuffer buf(&data);\n        buf.open(QIODevice::WriteOnly);\n'
    '        image.save(&buf, "PNG");\n    }\n#else\n    const auto data = ktx::encodeBc3(image);\n#endif')
rep('plugin/src/Caelestia/Multimedia/imagecacheworker.cpp', '#include <qdatetime.h>', '#include <qbuffer.h>\n#include <qdatetime.h>')
rep('plugin/src/Caelestia/Multimedia/imagecache.cpp', "    const auto path = m_dir + u'/' + key + u\".ktx\"_s;",
    "#ifdef _WIN32\n    const auto path = m_dir + u'/' + key + u\".png\"_s;\n#else\n    const auto path = m_dir + u'/' + key + u\".ktx\"_s;\n#endif")
# ---- Settings (nexus): Windows page + All settings in place of the two "under construction" System pages ----
rep('modules/nexus/PageRegistry.qml', '''        {
            label: Tr.tr("Updates"),
            icon: "update",
            description: Tr.tr("System updates"),
            category: "system"
        },
        {
            label: Tr.tr("Plugins"),
            icon: "extension",
            description: Tr.tr("Manage plugins"),
            category: "system"
        },''', '''        {
            label: Tr.tr("Windows"),
            icon: "desktop_windows",
            description: Tr.tr("Taskbar, startup, lock, wallpaper folder, config files"),
            category: "system"
        },
        {
            label: Tr.tr("Shortcuts"),
            icon: "keyboard",
            description: Tr.tr("Keys that open apps, folders and Caelestia's panels"),
            category: "system"
        },
        {
            label: Tr.tr("Hyprland"),
            icon: "dashboard",
            description: Tr.tr("Tile windows like Hyprland, from your hyprland.conf"),
            category: "system"
        },
        {
            label: Tr.tr("All settings"),
            icon: "tune",
            description: Tr.tr("Every option in shell.json and shell-tokens.json"),
            category: "system"
        },''')
rep('modules/nexus/PageCompRegistry.qml', '''        // System
        Component {
            PlaceholderComp {}
        },
        Component {
            PlaceholderComp {}
        },''', '''        // System
        Component {
            StackPage {
                Component {
                    WindowsPage {}
                }
            }
        },
        Component {
            StackPage {
                Component {
                    ShortcutsPage {}
                }
            }
        },
        Component {
            StackPage {
                Component {
                    HyprlandPage {}
                }
            }
        },
        Component {
            StackPage {
                Component {
                    AllSettingsPage {}
                }
            }
        },''')
# Settings window: no Windows title bar (Caelestia draws its own close button); drag empty space to move it
rep('modules/nexus/WindowFactory.qml', 'import Caelestia.Config\n', 'import Caelestia.Config\nimport Quickshell.Windows as Win\n')
rep('modules/nexus/WindowFactory.qml', '''            onVisibleChanged: {
                if (!visible)
                    destroy();
            }''', '''            onVisibleChanged: {
                if (!visible)
                    destroy();
                else
                    Qt.callLater(() => {
                        Win.WindowFrame.hideTitleBar(nexus.Window.window);
                        Win.WindowFrame.bringToFront(nexus.Window.window);   // not behind the app you are in
                    });
            }

            Component.onCompleted: root.current = win
            Component.onDestruction: if (root.current === win) root.current = null
            onStartPageChanged: nexus.goToStartPage()''')
# one Settings window: opening it again brings the open one to the front (on the page asked for, if any)
rep('modules/nexus/WindowFactory.qml', '''    function create(parent: Item, props: var): void {
        nexusComp.createObject(parent ?? dummy, props);
    }''', '''    property var current: null

    function create(parent: Item, props: var): void {
        if (root.current) {
            if (props && props.startPage) root.current.startPage = props.startPage;
            Win.WindowFrame.bringToFront(root.current.contentItem.Window.window);
            return;
        }
        nexusComp.createObject(parent ?? dummy, props);
    }''')
rep('modules/nexus/Nexus.qml', '''    TapHandler {
        onTapped: root.focus = true
    }''', '''    TapHandler {
        onTapped: root.focus = true
    }

    // Windows: no title bar, so dragging any empty part of the window moves it
    DragHandler {
        target: null
        enabled: root.nState.isWindow
        onActiveChanged: if (active && root.Window.window) root.Window.window.startSystemMove()
    }''')
# open Settings on a given page: qs ipc call nexus openPage windows  (Caelestia.exe --settings <page>)
rep('modules/nexus/WindowFactory.qml', '''        FloatingWindow {
            id: win
''', '''        FloatingWindow {
            id: win

            property string startPage
''')
rep('modules/nexus/WindowFactory.qml', '''                nState.isWindow: true
                onClose: win.destroy()''', '''                nState.isWindow: true
                onClose: win.destroy()

                function goToStartPage(): void {
                    if (!win.startPage)
                        return;
                    const want = win.startPage.toLowerCase();
                    const i = PageRegistry.pages.findIndex(p => p.label.toLowerCase() === want || p.label.toLowerCase().startsWith(want));
                    if (i >= 0)
                        nState.currentPageIdx = i;
                }

                Component.onCompleted: goToStartPage()''')
# bar entry "apps": open windows as app icons (modules/bar/components/RunningApps.qml), on by default after workspaces
rep('plugin/src/Caelestia/Config/barconfig.hpp', '''            LIST_ENTRY(workspaces, true),
''', '''            LIST_ENTRY(workspaces, true),
            LIST_ENTRY(apps, true),
''')
rep('modules/bar/Bar.qml', '''            DelegateChoice {
                roleValue: "activeWindow"''', '''            DelegateChoice {
                roleValue: "apps"
                delegate: EntryWrapper {
                    RunningApps {
                        objectName: "taskbarApps"
                    }
                }
            }
            DelegateChoice {
                roleValue: "activeWindow"''')
# launcher ">lock": loginctl is Linux; use this shell's own lock
rep('plugin/src/Caelestia/Config/launcherconfig.hpp', 'QStringList{ u"loginctl"_s, u"lock-session"_s }',
    'QStringList{ u"caelestia"_s, u"shell"_s, u"lock"_s, u"lock"_s }')
rep('daemons/Shortcuts.qml', '''        function open(): void {
            WindowFactory.create();
        }

        target: "nexus"''', '''        function open(): void {
            WindowFactory.create();
        }

        function openPage(page: string): void {
            WindowFactory.create(null, { startPage: page });
        }

        target: "nexus"''')
# lock: Caelestia's own lock screen, or Windows' (Settings -> Windows)
rep('modules/lock/Lock.qml', 'import qs.components.misc\n', 'import qs.components.misc\nimport qs.services\n')
rep('modules/lock/Lock.qml', '''    WlSessionLock {
        id: lock
''', '''    WlSessionLock {
        id: lock

        useWindowsLock: WinSettings.useWindowsLock
''')
# lock background: Caelestia's default is a blurred capture of the screen, but on Windows that capture is taken
# once the (black) lock window is already up -> use the wallpaper by default (All settings -> Lock to change)
# lock background: the blurred desktop (Caelestia's default) works now that the lock windows are excluded from
# capture; blur strength comes from Settings -> Windows (Caelestia's own is very strong)
rep('modules/lock/LockSurface.qml', '''        ScreencopyView {
            captureSource: root.screen
        }''', '''        // Windows: the lock grabs each screen just before it appears (live capture is refused on some accounts)
        Image {
            source: root.lock.snapshotFor(root.screen)
            fillMode: Image.PreserveAspectCrop
            asynchronous: false
            cache: false
        }''')
rep('modules/lock/LockSurface.qml', '''            blurEnabled: true
            blur: 1
            blurMax: 64''', '''            blurEnabled: WinSettings.lockBlur > 0
            blur: WinSettings.lockBlur
            blurMax: 48''')
# Alt+Tab switcher / Super+Tab task view (modules/switcher/Switcher.qml)
rep('shell.qml', 'import qs.modules.lock\n', 'import qs.modules.lock\nimport qs.modules.switcher\nimport qs.modules.waybar\n')
rep('shell.qml', '''    Lock {
        id: lock
    }''', '''    Lock {
        id: lock
    }
    Switcher {}
    LazyLoader {   // Waybar for Windows (Settings > Setup / Windows), config in ~\\.config\\waybar
        active: WinSettings.waybar
        Waybar {}
    }''')
# loading screen (modules/splash/Splash.qml)
rep('shell.qml', 'import qs.modules.waybar\n', 'import qs.modules.waybar\nimport qs.modules.splash\n')
rep('shell.qml', '    Switcher {}\n', '    Switcher {}\n    Splash {}\n')
# Settings pages: a page still being built when you switch away used to attach later on top of the new one
# (two pages overlapping). Only the most recent request may attach; older ones are thrown away.
rep('modules/nexus/Pages.qml', '''    function loadPage(idx: int): void {
        if (currentItem)
            currentItem.destroy();

        const comp = PageCompRegistry.pageComps[idx] ?? PageCompRegistry.placeholderComp;
        const incubator = comp.incubateObject(container, {
            nState
        });

        const attach = () => {
            incubator.object.anchors.fill = container;
            currentItem = incubator.object;
        };''', '''    property int loadSerial: 0

    function loadPage(idx: int): void {
        if (currentItem)
            currentItem.destroy();
        currentItem = null;

        const serial = ++loadSerial;
        const comp = PageCompRegistry.pageComps[idx] ?? PageCompRegistry.placeholderComp;
        const incubator = comp.incubateObject(container, {
            nState
        });

        const attach = () => {
            if (serial !== root.loadSerial) {   // superseded while it was being built
                incubator.object.destroy();
                return;
            }
            if (currentItem && currentItem !== incubator.object)
                currentItem.destroy();
            incubator.object.anchors.fill = container;
            currentItem = incubator.object;
        };''')
# the loading cover is drawn inside each screen's drawers window (a separate overlay window was never drawn here)
rep('modules/drawers/ContentWindow.qml', 'import qs.daemons\n', 'import qs.daemons\nimport qs.modules.splash\n')
rep('modules/drawers/ContentWindow.qml', '''    ShellState.ComponentRef {
        screen: root.screen
        slot: "bar"''', '''    SplashCover {
        anchors.fill: parent
    }

    ShellState.ComponentRef {
        screen: root.screen
        slot: "bar"''')
# Settings window: animated background (modules/nexus/NexusFx.qml), only in the window, behind everything
rep('modules/nexus/Nexus.qml', '''    BlobGroup {
        id: blobGroup''', '''    NexusFx {
        anchors.fill: parent
        visible: root.nState.isWindow && style !== "none"
    }

    BlobGroup {
        id: blobGroup''')
# Settings > Look & feel (after Wallpaper & style) and Settings > Shortcuts (after Windows)
rep('modules/nexus/PageRegistry.qml', '''            category: "appearance"
        },''', '''            category: "appearance"
        },
        {
            label: Tr.tr("Look & feel"),
            icon: "brush",
            description: Tr.tr("Size, bar width, animations, wallpaper effects, loading screen"),
            category: "appearance"
        },''')
rep('modules/nexus/PageCompRegistry.qml', '''                Component {
                    ColourSelect {}
                }
            }
        },''', '''                Component {
                    ColourSelect {}
                }
            }
        },
        Component {
            StackPage {
                Component {
                    LookPage {}
                }
            }
        },''')
# Settings > Setup (questions that write the configs), first of the System pages
rep('modules/nexus/PageRegistry.qml', '''        {
            label: Tr.tr("Windows"),''', '''        {
            label: Tr.tr("Setup"),
            icon: "auto_fix_high",
            description: Tr.tr("Bar, apps, launcher, dashboard, lock - answer a few questions"),
            category: "system"
        },
        {
            label: Tr.tr("Windows"),''')
rep('modules/nexus/PageCompRegistry.qml', '''        // System
        Component {''', '''        // System
        Component {
            StackPage {
                Component {
                    SetupPage {}
                }
            }
        },
        Component {''')
# load the Windows options (hide taskbar etc.) at start
rep('shell.qml', '        Notifs;\n', '        Notifs;\n        WinSettings;\n        HyprConf;\n')
# paths: "C:/..." would be read as a URL with scheme "c" and turn into nothing - this is why a wallpaper folder
# on Windows never worked
rep('utils/Paths.qml', '''    function absolutePath(path: string): string {
        return toLocalFile(path.replace(/~|(\\$({?)HOME(}?))+/, home));
    }''', '''    function absolutePath(path: string): string {
        path = path.replace(/~|(\\$({?)HOME(}?))+/, home).replace(/\\\\/g, "/");
        if (/^[A-Za-z]:\\//.test(path))   // Windows drive path: already local
            return path;
        return toLocalFile(path);
    }''')
# headers newer Qt pulls in transitively
for f in ['plugin/src/Caelestia/Services/lyrics.cpp', 'plugin/src/Caelestia/Services/hyprextras.cpp']:
    p = os.path.join(DST, f); s = rd(p)
    wr(p, s.replace('#include', '#include <qjsondocument.h>\n#include', 1))

# 5. Windows DLLs: functions are exported by CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS, but data (staticMetaObject) used from
#    another DLL needs __declspec(dllimport). Every class with a base gets its module's CAE_<MOD>_API (export inside
#    its own DLL, import elsewhere). Classes stamped out by macros in Settings/macros.hpp take CAE_MACRO_API, which
#    each header re-points at its own module before using those macros. The macros live in a force-included header.
import re
MODS = {'': 'CORE', 'Settings': 'SETTINGS', 'Components': 'COMPONENTS', 'Config': 'CONFIG', 'State': 'STATE',
        'Models': 'MODELS', 'Services': 'SERVICES', 'Blobs': 'BLOBS', 'Images': 'IMAGES', 'I18n': 'I18N',
        'Multimedia': 'MULTIMEDIA', 'Shaders': 'SHADERS'}
TARGET = {'CORE': 'caelestia-core', 'SETTINGS': 'caelestia-settings', 'COMPONENTS': 'caelestia-components',
          'CONFIG': 'caelestia-config', 'STATE': 'caelestia-state', 'MODELS': 'caelestia-models',
          'SERVICES': 'caelestia-services', 'BLOBS': 'caelestia-blobs', 'IMAGES': 'caelestia-images',
          'I18N': 'caelestia-i18n', 'MULTIMEDIA': 'caelestia-multimedia', 'SHADERS': 'caelestia-shaders'}
hdr = ['#pragma once', '#if defined(__cplusplus) && defined(_WIN32)']
for m in TARGET:
    hdr += ['#ifdef CAE_BUILD_%s' % m, '#define CAE_%s_API __declspec(dllexport)' % m, '#else',
            '#define CAE_%s_API __declspec(dllimport)' % m, '#endif']
hdr += ['#else'] + ['#define CAE_%s_API' % m for m in TARGET] + ['#endif', '#define CAE_MACRO_API', '']
wr(os.path.join(HERE, 'caelestia-winexport.hpp'), '\n'.join(hdr))

cls = re.compile(r'(^|\n)([ \t]*)class (\w+(?:##\w+)*) : public')
base = os.path.join(DST, 'plugin/src/Caelestia')
marked = 0
for d, m in MODS.items():
    folder = os.path.join(base, d) if d else base
    for root, dirs, files in os.walk(folder):
        if not d: dirs[:] = []                       # core = files directly in Caelestia/
        for f in files:
            if not f.endswith('.hpp'): continue
            p = os.path.join(root, f); s = rd(p); orig = s
            api = 'CAE_MACRO_API' if f == 'macros.hpp' and d == 'Settings' else 'CAE_%s_API' % m
            def sub(mt):
                pre = s[:mt.start()].rstrip().splitlines()[-1:] or ['']
                if pre[0].lstrip().startswith('template'): return mt.group(0)
                eol = s.find(chr(10), mt.end()); line = s[mt.start():eol if eol >= 0 else len(s)]
                use = 'CAE_MACRO_API' if line.rstrip().endswith(chr(92)) else api   # class inside a #define body
                return '%s%sclass %s %s : public' % (mt.group(1), mt.group(2), use, mt.group(3))
            s = cls.sub(sub, s)
            # enum namespaces carry a staticMetaObject too
            s = re.sub(r'\bQ_NAMESPACE\b(?!_)', 'Q_NAMESPACE_EXPORT(%s)' % api, s)
            if f != 'macros.hpp':
                # point the shared macros at this module (after the includes, before any use)
                inc = list(re.finditer(r'^#include .*$', s, re.M))
                at = inc[-1].end() if inc else s.index('\n', s.index('#pragma once')) 
                s = s[:at] + '\n#undef CAE_MACRO_API\n#define CAE_MACRO_API CAE_%s_API\n' % m + s[at:]
            if s != orig: wr(p, s); marked += s.count('_API ') 
for f, m in [('plugin/src/Caelestia/%s/CMakeLists.txt' % d if d else 'plugin/src/Caelestia/CMakeLists.txt', m) for d, m in MODS.items()]:
    p = os.path.join(DST, f); s = rd(p)
    s += '\nif(WIN32)\n    target_compile_definitions(%s PRIVATE CAE_BUILD_%s)\nendif()\n' % (TARGET[m], m)
    wr(p, s)
print('export markers:', marked)

print('caelestia-shell-win ready,', n, 'Windows files laid over')
