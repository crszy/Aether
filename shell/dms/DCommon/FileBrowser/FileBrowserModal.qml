pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import qs.dms.DCommon.Common
import qs.dms.DCommon.Widgets

FloatingWindow {
    id: modal

    property string mode: folderMode ? "openFolder" : saveMode ? "save" : "open"
    property var filters: fileExtensions
    property bool multiple: false
    property string defaultName: defaultFileName
    property string startPath: revealPath
    property string bucket: browserType
    property string defaultViewMode: ["wallpaper", "profile"].includes(bucket) ? "grid" : "list"
    property var backend: Host.files

    property string browserTitle: I18n.tr("Select File", "default file browser window title")
    property string browserIcon: "folder_open" // !TODO: plugin compat, the window header no longer draws an icon
    property string browserType: "generic"
    property var fileExtensions: []
    property alias filterExtensions: modal.fileExtensions
    property bool showHiddenFiles: false
    property bool saveMode: false
    property bool folderMode: false
    property string defaultFileName: ""
    property string revealPath: ""

    property bool disablePopupTransparency: true
    property var parentModal: null
    property bool shouldHaveFocus: visible
    property bool allowFocusOverride: false
    property bool shouldBeVisible: visible
    property bool allowStacking: true // !TODO: plugin compat, unused since the browser became a floating window

    readonly property var _screen: screen ?? (Quickshell.screens.length > 0 ? Quickshell.screens[0] : null)
    readonly property int screenWidth: _screen?.width ?? 1920
    readonly property int screenHeight: _screen?.height ?? 1080
    readonly property bool useFullScreen: screenWidth < Style.smallBreakpoint || (screenWidth < Style.mediumBreakpoint && screenHeight > screenWidth)
    readonly property alias content: contentLoader.item

    property bool _settled: false

    signal accepted(var paths)
    signal rejected
    signal fileSelected(string path)
    signal dialogClosed

    function open() {
        visible = true;
    }

    function close() {
        visible = false;
    }

    parentWindow: parentModal
    title: I18n.tr("Files - %1", "file browser window title, %1 is the picker purpose").arg(browserTitle)
    minimumSize: Qt.size(Math.min(FileBrowserMetrics.pickerMinWidth, screenWidth), Math.min(FileBrowserMetrics.pickerMinHeight, screenHeight))
    implicitWidth: useFullScreen ? screenWidth : Math.min(FileBrowserMetrics.pickerWidth, screenWidth * FileBrowserMetrics.pickerScreenFraction)
    implicitHeight: useFullScreen ? screenHeight : Math.min(FileBrowserMetrics.pickerHeight, screenHeight * FileBrowserMetrics.pickerScreenFraction)
    color: "transparent"
    visible: false

    onClosed: close()

    onVisibleChanged: {
        if (visible) {
            _settled = false;
            if (parentModal && "shouldHaveFocus" in parentModal) {
                parentModal.shouldHaveFocus = false;
                parentModal.allowFocusOverride = true;
            }
            return;
        }
        if (parentModal && "allowFocusOverride" in parentModal) {
            parentModal.allowFocusOverride = false;
            parentModal.shouldHaveFocus = Qt.binding(() => parentModal.shouldBeVisible);
        }
        if (!_settled) {
            _settled = true;
            rejected();
        }
        dialogClosed();
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.windowRadius
        color: Style.floatingWindowSurface
    }

    WindowBlur {
        targetWindow: modal
        blurWidth: modal.visible ? modal.width : 0
        blurHeight: modal.visible ? modal.height : 0
        blurRadius: Style.windowRadius
    }

    Item {
        readonly property bool isFloatingWindowSurface: true

        anchors.fill: parent

        Loader {
            id: contentLoader

            anchors.fill: parent
            active: modal.visible
            focus: true
            sourceComponent: FilePicker {
                focus: true
                mode: modal.mode
                filters: modal.filters
                multiple: modal.multiple
                defaultName: modal.defaultName
                startPath: modal.startPath
                bucket: modal.bucket
                title: modal.browserTitle
                windowControls: windowControls
                defaultShowHidden: modal.showHiddenFiles
                defaultViewMode: modal.defaultViewMode
                backend: modal.backend
                onAccepted: paths => {
                    modal._settled = true;
                    modal.accepted(paths);
                    if (paths.length > 0)
                        modal.fileSelected(paths[0]);
                    modal.close();
                }
                onRejected: modal.close()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Style.windowRadius
        color: "transparent"
        border.color: Style.blurBorderColor
        border.width: Style.blurBorderWidth
        antialiasing: true
    }

    FloatingWindowControls {
        id: windowControls

        targetWindow: modal
    }
}
