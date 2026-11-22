import QtQuick

// Windows stand-in for the Wayland keyboard-shortcuts inhibitor: Windows has no
// compositor shortcuts to pause, so it never becomes active.
QtObject {
	property var window: null
	property bool enabled: false
	readonly property bool active: false
	signal cancelled()
}
