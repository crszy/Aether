import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Windows as Win

// Windows stand-in for the Wayland session lock.
//  - useWindowsLock true (or no surface given): hands off to Windows' own lock screen (LockWorkStation).
//  - otherwise: shows the config's lock surface on every screen as a top-most overlay that takes the
//    keyboard, until the config calls unlock() and then sets locked = false (after its unlock animation).
// This is the shell's own lock screen drawn over the desktop, not Windows' secure lock: Ctrl+Alt+Del still
// reaches Windows. QS_LOCK_TEST_UNLOCK_MS=<n> (testing only) unlocks by itself after n ms.
Scope {
	id: root

	property bool locked: false
	property bool useWindowsLock: false
	default property Component surface: null
	readonly property bool secure: locked && !useWindowsLock && surface !== null && root._shown > 0

	// Live mode: only the main screen shows the lock surface. The others stay covered by a lock window that
	// swallows every click and key but is see-through, so what is going on behind is visible. The surface checks
	// isLive(screen) to draw itself (dimming, no lock box). mainScreen is read when locking (a screen name;
	// empty or unknown = the first screen) and kept until unlock, so the lock box never jumps monitors.
	property bool liveOthers: false
	property string mainScreen: ""
	property string _main: ""
	readonly property bool live: liveOthers && Quickshell.screens.length > 1

	function isMain(screen): bool {
		if (!screen) return true;
		const name = Quickshell.screens.some(s => s.name === root._main) ? root._main : (Quickshell.screens[0]?.name ?? "");
		return screen.name === name;
	}

	function isLive(screen): bool {
		return root.live && !root.isMain(screen);
	}

	property int _shown: 0
	property bool _ready: false

	// A picture of each screen taken just before the lock appears (file URLs by screen name), for a lock
	// surface that wants the desktop behind it. Windows.Graphics.Capture is refused on some accounts (the
	// built-in Administrator), so this uses the classic capture; the files are deleted on unlock.
	property var snapshots: ({})

	function snapshotFor(screen): string {
		return screen ? (root.snapshots[screen.name] ?? "") : "";
	}

	signal unlock()

	onLockedChanged: {
		if (!locked) {
			root._shown = 0;
			root._ready = false;
			for (const name in root.snapshots) {
				const url = root.snapshots[name];
				if (url) Win.FsUtils.removeFile(decodeURIComponent(url.replace(/^file:\/\/\//, "")));
			}
			root.snapshots = {};
			return;
		}
		if (root.useWindowsLock || root.surface === null) {
			Quickshell.execDetached(["rundll32.exe", "user32.dll,LockWorkStation"]);
			Qt.callLater(() => { root.locked = false; });
			return;
		}
		root._main = root.mainScreen;
		const temp = (Quickshell.env("TEMP") || Quickshell.env("TMP") || ".").split(String.fromCharCode(92)).join("/");
		const shots = {};
		let i = 0;
		for (const s of Quickshell.screens)
			if (!root.live || root.isMain(s))
				shots[s.name] = Win.WindowFrame.grabScreen(s.name, `${temp}/qs-lock-${i++}.png`);
		root.snapshots = shots;
		root._ready = true;
		const test = parseInt(Quickshell.env("QS_LOCK_TEST_UNLOCK_MS") || "0");
		if (test > 0) testTimer.interval = test, testTimer.start();
	}

	// a lock surface whose unlock animation never ran must not leave the screen covered for ever
	onUnlock: safetyTimer.restart()

	Timer {
		id: safetyTimer
		interval: 3000
		onTriggered: if (root.locked) root.locked = false
	}

	Timer {
		id: testTimer
		onTriggered: if (root.locked) root.unlock()
	}

	Variants {
		model: root.locked && root._ready && !root.useWindowsLock && root.surface !== null ? Quickshell.screens : []

		PanelWindow {
			id: win

			required property var modelData

			readonly property bool liveHere: root.live && !root.isMain(modelData)

			screen: modelData
			// a live monitor keeps a faint tint, so every pixel still belongs to the lock window and clicks never
			// reach the apps behind it (the surface draws the dimming on top, with a fade)
			color: liveHere ? Qt.rgba(0, 0, 0, 0.02) : "black"
			anchors {
				top: true
				bottom: true
				left: true
				right: true
			}
			exclusionMode: ExclusionMode.Ignore
			WlrLayershell.layer: WlrLayer.Overlay
			WlrLayershell.namespace: "quickshell:sessionlock"
			// only the main screen takes the keyboard, so the PIN box keeps it
			WlrLayershell.keyboardFocus: liveHere ? WlrKeyboardFocus.None : WlrKeyboardFocus.Exclusive

			// The lock windows are left out of screen capture, so a lock surface that captures the screen
			// (a blurred desktop behind the lock) sees the desktop, not the lock itself. They also stay out of
			// screenshots and recordings. Marked before the surface (and its capture) is created.
			Component.onCompleted: Qt.callLater(() => {
				if (Quickshell.env("QS_LOCK_ALLOW_CAPTURE") !== "1")   // testing only: lets a screenshot see the lock
					Win.WindowFrame.excludeFromCapture(loader.Window.window, true);
				loader.active = true;
			})

			Loader {
				id: loader

				anchors.fill: parent
				focus: !win.liveHere
				active: false
				sourceComponent: root.surface
				onLoaded: {
					item.screen = win.modelData;
					item.anchors.fill = loader;   // the window stays black behind it, so the desktop never shows through
					item.forceActiveFocus();
					root._shown++;
				}
			}

			Component.onDestruction: if (root._shown > 0) root._shown--
		}
	}
}
