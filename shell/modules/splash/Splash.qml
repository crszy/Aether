// Loading screen: state lives in qs.services SplashState, the cover in SplashCover.qml (inside the drawers window).
import QtQuick
import Quickshell
import qs.services

Scope {
    Component.onCompleted: SplashState.showing
}
