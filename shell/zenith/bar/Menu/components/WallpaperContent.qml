import QtQuick
import qs.services as Cae
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.folderlistmodel
import Quickshell
import Quickshell.Io
import "../../../"
import "../../../services"
import "../../../Settings"

ColumnLayout {
    id: root
    spacing: Theme.scaled(20)
    focus: true
    Keys.onPressed: (event) => handleKeys(event)

    property var selectedWalls: []
    property int refreshTrigger: 0
    property string activeSubTab: CenterState.wallpaperSubTab
    onActiveSubTabChanged: {
        if (CenterState.wallpaperSubTab !== activeSubTab) {
            CenterState.wallpaperSubTab = activeSubTab;
        }
    }
    property bool thumbnailsReady: false
    property int selectedIndex: 0

    function handleKeys(event) {
        if (event.key === Qt.Key_Tab) {
            let subTabs = ["Wallpaper", "Themes", "Slideshow", "Animated"];
            let sIdx = subTabs.indexOf(root.activeSubTab);
            root.activeSubTab = subTabs[(sIdx + 1) % subTabs.length];
            root.selectedIndex = 0;
            event.accepted = true;
            return;
        } else if (event.key === Qt.Key_Backtab) {
            let subTabs = ["Wallpaper", "Themes", "Slideshow", "Animated"];
            let sIdx = subTabs.indexOf(root.activeSubTab);
            root.activeSubTab = subTabs[(sIdx - 1 + subTabs.length) % subTabs.length];
            root.selectedIndex = 0;
            event.accepted = true;
            return;
        }

        if (root.activeSubTab === "Themes") return;

        let cols = 4;
        let isAnimated = (root.activeSubTab === "Animated");
        let model = isAnimated ? animFolderModel : wallFolderModel;
        let maxIdx = model ? model.count - 1 : -1;

        if (maxIdx < 0) return;

        if (event.key === Qt.Key_Right) {
            root.selectedIndex = Math.min(root.selectedIndex + 1, maxIdx);
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            root.selectedIndex = Math.max(root.selectedIndex - 1, 0);
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            root.selectedIndex = Math.min(root.selectedIndex + cols, maxIdx);
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            root.selectedIndex = Math.max(root.selectedIndex - cols, 0);
            event.accepted = true;
        } else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === 16777220) {
            let path = model.get(root.selectedIndex, "fileUrl");
            if (path) {
                let pathString = path.toString();
                if (root.activeSubTab === "Wallpaper") applyWallpaper(pathString);
                else if (root.activeSubTab === "Slideshow") toggleSelection(pathString);
                else if (root.activeSubTab === "Animated") applyVideo(pathString);
            }
            event.accepted = true;
        }
    }

    onVisibleChanged: { if (visible) refreshThumbnails(); }

    function refreshThumbnails() {
        thumbnailsReady = false;
        thumbGen.running = false;
        thumbGen.running = true;
    }

    readonly property string logPath: PathSettings.cacheDir + "/wallpaper.log"
    readonly property string scriptsPath: PathSettings.scriptsDir

    // --- SUB-TABS ---
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(10)
        
        Repeater {
            model: ["Wallpaper", "Themes", "Slideshow", "Animated"]
            delegate: Rectangle {
                width: Theme.scaled(100); height: Theme.scaled(32); radius: Theme.bubbleRadiusPill
                color: root.activeSubTab === modelData ? Theme.accentColor : (subTabMouse.containsMouse ? Theme.cardBackgroundHover : "transparent")
                border.width: 0
                scale: subTabMouse.pressed ? 0.95 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.animInstant; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                Text { 
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: Theme.scaled(11)
                    font.weight: Font.Black
                    color: root.activeSubTab === modelData ? Colors.on_primary : (subTabMouse.containsMouse ? Theme.text : Theme.subtext0)
                }
                MouseArea { 
                    id: subTabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.activeSubTab = modelData;
                        root.selectedIndex = 0;
                    }
                }
            }
        }
        Item { Layout.fillWidth: true }
        
        // Thumbnail Generation Status
        RowLayout {
            spacing: Theme.scaled(8)
            visible: root.activeSubTab !== "Themes" && thumbGen.running
            Text {
                text: "󱑐"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(14); color: Theme.blue
                RotationAnimator on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: thumbGen.running }
            }
            Text { text: "Updating..."; font.pixelSize: Theme.scaled(10); font.weight: Font.Black; color: Theme.subtext1 }
        }
        
        // Refresh Button
        Rectangle {
            width: Theme.scaled(32); height: Theme.scaled(32); radius: Theme.scaled(8)
            color: refreshMouse.containsMouse ? Qt.rgba(1,1,1,0.1) : "transparent"
            visible: root.activeSubTab !== "Themes" && !thumbGen.running
            Text { anchors.centerIn: parent; text: "󰑐"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(16); color: refreshMouse.containsMouse ? Theme.text : Theme.subtext1 }
            MouseArea { id: refreshMouse; anchors.fill: parent; hoverEnabled: true; onClicked: refreshThumbnails() }
        }
    }

    // --- GRID AREA ---
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ThemeContent {
            id: themeSubContent
            anchors.fill: parent
            visible: root.activeSubTab === "Themes"
        }

        Flickable {
            FastWheel {}
            id: wallFlickable
            anchors.fill: parent
            contentHeight: wallFlow.height
            visible: root.activeSubTab === "Wallpaper" || root.activeSubTab === "Slideshow"
            clip: true
            ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded }

            Connections {
                target: root
                function onSelectedIndexChanged() {
                    let item = wallRepeater.itemAt(root.selectedIndex);
                    if (item) {
                        let itemPos = item.y;
                        if (itemPos < wallFlickable.contentY) wallFlickable.contentY = itemPos;
                        else if (itemPos + item.height > wallFlickable.contentY + wallFlickable.height) wallFlickable.contentY = itemPos + item.height - wallFlickable.height;
                    }
                }
            }

            Flow {
                id: wallFlow
                width: parent.width
                spacing: Theme.scaled(15)
                Repeater {
                    id: wallRepeater
                    model: FolderListModel {
                        id: wallFolderModel
                        folder: WallpaperService.wallpaperDir
                        nameFilters: ["*.jpg", "*.png", "*.jpeg", "*.webp"]
                        onCountChanged: if (root.visible) refreshThumbnails();
                    }
                    delegate: Rectangle {
                        width: (wallFlow.width - Theme.scaled(45)) / 4
                        height: width * 0.6
                        radius: Theme.bubbleRadiusMedium
                        color: Theme.cardBackground
                        clip: true
                        property bool isSelected: root.selectedWalls.indexOf(fileUrl) !== -1
                        property int selectionIndex: root.selectedWalls.indexOf(fileUrl)
                        property bool isFocused: index === root.selectedIndex
                        border.color: isFocused ? Theme.accentColor : (isSelected ? Theme.accentColor : Theme.glassBorder)
                        border.width: 1
                        
                        // Darken if selected
                        Rectangle {
                            anchors.fill: parent; color: isSelected ? Qt.rgba(0,0,0,0.4) : "transparent"; radius: Theme.scaled(12)
                        }

                        // Selection Number Badge
                        Rectangle {
                            visible: isSelected
                            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: Theme.scaled(8)
                            width: Theme.scaled(24); height: Theme.scaled(24); radius: Theme.scaled(12)
                            color: Theme.accentColor
                            Text {
                                anchors.centerIn: parent; text: (selectionIndex + 1).toString()
                                font.pixelSize: Theme.scaled(12); font.bold: true; color: Theme.base
                            }
                        }
                        
                        Loader {
                            id: thumbLoader; anchors.fill: parent
                            sourceComponent: (fileName && root.thumbnailsReady) ? thumbComponent : undefined
                            Component {
                                id: thumbComponent
                                Image {
                                    anchors.fill: parent; anchors.margins: 2
                                    source: fileUrl; sourceSize.width: 320 // Windows
                                    fillMode: Image.PreserveAspectCrop; cache: false; asynchronous: true
                                    opacity: status === Image.Ready ? 1.0 : 0.0
                                    Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                                }
                            }
                        }
                        
                        Text {
                            anchors.centerIn: parent; text: "󱑐"; font.family: Theme.iconFont; font.pixelSize: Theme.scaled(20); color: Theme.blue
                            visible: !root.thumbnailsReady || (thumbLoader.item && thumbLoader.item.status !== Image.Ready)
                            RotationAnimator on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: parent.visible }
                        }
                        
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true
                            onEntered: root.selectedIndex = index
                            onClicked: { 
                                let pathString = fileUrl.toString();
                                if (root.activeSubTab === "Wallpaper") applyWallpaper(pathString); 
                                else if (root.activeSubTab === "Slideshow") toggleSelection(pathString);
                            }
                            Rectangle {
                                anchors.fill: parent; radius: Theme.scaled(12)
                                color: parent.containsMouse ? Qt.rgba(1,1,1,0.1) : "transparent"
                            }
                        }                    }
                }
            }

            // Empty state placeholder for Wallpapers / Slideshow
            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.scaled(10)
                visible: wallFolderModel.count === 0 && !thumbGen.running
                Text {
                    text: "󰉏"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(48)
                    color: Theme.surface2
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "Directory is empty"
                    font.pixelSize: Theme.scaled(14)
                    font.weight: Font.Bold
                    color: Theme.subtext1
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "~/Pictures/Wallpapers"
                    font.pixelSize: Theme.scaled(11)
                    color: Theme.surface2
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        Flickable {
            id: animFlickable
            anchors.fill: parent
            contentHeight: animFlow.height
            visible: root.activeSubTab === "Animated"
            clip: true
            ScrollBar.vertical: ScrollBar { width: 4; policy: ScrollBar.AsNeeded }

            Connections {
                target: root
                function onSelectedIndexChanged() {
                    let item = animRepeater.itemAt(root.selectedIndex);
                    if (item) {
                        let itemPos = item.y;
                        if (itemPos < animFlickable.contentY) animFlickable.contentY = itemPos;
                        else if (itemPos + item.height > animFlickable.contentY + animFlickable.height) animFlickable.contentY = itemPos + item.height - animFlickable.height;
                    }
                }
            }

            Flow {
                id: animFlow
                width: parent.width
                spacing: Theme.scaled(15)
                Repeater {
                    id: animRepeater
                    model: FolderListModel {
                        id: animFolderModel
                        folder: WallpaperService.animationDir
                        nameFilters: ["*.mp4", "*.mkv", "*.webm"]
                        onCountChanged: if (root.visible) refreshThumbnails();
                    }
                    delegate: Rectangle {
                        width: (animFlow.width - Theme.scaled(45)) / 4
                        height: width * 0.6
                        radius: Theme.bubbleRadiusMedium
                        color: Theme.cardBackground
                        clip: true
                        property bool isFocused: index === root.selectedIndex
                        border.color: isFocused ? Theme.accentColor : Theme.glassBorder
                        border.width: 1
                        
                        Loader {
                            id: animThumbLoader; anchors.fill: parent
                            sourceComponent: (fileName && root.thumbnailsReady) ? animThumbComponent : undefined
                            Component {
                                id: animThumbComponent
                                Image {
                                    anchors.fill: parent; anchors.margins: 2
                                    source: (fileName && fileName.lastIndexOf('.') > 0) ? ("file:///" + ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.cache/animation_thumbs/" + fileName.substring(0, fileName.lastIndexOf('.')) + ".png") : ""
                                    fillMode: Image.PreserveAspectCrop; cache: false; asynchronous: true
                                    opacity: status === Image.Ready ? 1.0 : 0.0
                                    Behavior on opacity { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent; hoverEnabled: true
                            onEntered: root.selectedIndex = index
                            onClicked: applyVideo(fileUrl.toString())
                        }
                    }
                }
            }

            // Empty state placeholder for Animated Videos
            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.scaled(10)
                visible: animFolderModel.count === 0 && !thumbGen.running
                Text {
                    text: "󰕧"
                    font.family: Theme.iconFont
                    font.pixelSize: Theme.scaled(48)
                    color: Theme.surface2
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "Directory is empty"
                    font.pixelSize: Theme.scaled(14)
                    font.weight: Font.Bold
                    color: Theme.subtext1
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    text: "~/Videos/Animations"
                    font.pixelSize: Theme.scaled(11)
                    color: Theme.surface2
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }
    }

    // --- SLIDESHOW CONTROLS ---
    RowLayout {
        Layout.alignment: Qt.AlignHCenter; spacing: Theme.scaled(20); visible: root.activeSubTab === "Slideshow"
        Rectangle {
            width: Theme.scaled(180); height: Theme.scaled(40); radius: Theme.scaled(10)
            color: root.selectedWalls.length > 0 ? Theme.accentColor : Theme.surface1
            Text { anchors.centerIn: parent; text: "Start Slideshow"; color: root.selectedWalls.length > 0 ? Theme.base : Theme.subtext1; font.bold: true }
            MouseArea { anchors.fill: parent; enabled: root.selectedWalls.length > 0; onClicked: startSlideshow() }
        }
        Rectangle {
            width: Theme.scaled(180); height: Theme.scaled(40); radius: Theme.scaled(10)
            color: Theme.red
            visible: slideshowRunning
            Text { anchors.centerIn: parent; text: "Stop Slideshow"; color: Theme.base; font.bold: true }
            MouseArea { anchors.fill: parent; onClicked: stopSlideshow() }
        }
    }

    property bool slideshowRunning: false
    Timer {
        interval: 2000; running: root.activeSubTab === "Slideshow"; repeat: true; triggeredOnStart: true
        onTriggered: checkSlideshowStatus()
    }
    function checkSlideshowStatus() {
        checkStatus.command = ["systemctl", "--user", "is-active", "--quiet", "zenith-slideshow.service"];
        checkStatus.running = true;
    }
    Process { id: checkStatus; onExited: (exitCode, exitStatus) => slideshowRunning = (exitCode === 0) }

    // --- LOGIC FUNCTIONS ---
    function toggleSelection(path) {
        let arr = [...root.selectedWalls];
        let idx = arr.indexOf(path);
        if (idx !== -1) arr.splice(idx, 1);
        else arr.push(path);
        root.selectedWalls = arr;
    }
    function applyWallpaper(path) { // Windows: Caelestia sets it
        Cae.Wallpapers.setWallpaper(decodeURIComponent(String(path).replace(/^file:\/+/, "")));
        CenterState.close();
    }
    function applyVideo(path) {
        killawww.running = true; killMpv.running = true; stopSlideshow();
        videoDelay.videoPath = path.replace("file:///", ""); videoDelay.start();
    }
    function startSlideshow() {
        if (root.selectedWalls.length === 0) return;
        let scriptPath = root.scriptsPath + "/slideshow.sh";
        let listPath = PathSettings.cacheDir + "/slideshow_list";
        let paths = root.selectedWalls.map(p => p.replace("file:///", "")).join("\n");
        // One process writes the list and installs the unit; the service is
        // started only after both are on disk.
        let unit = [
            "[Unit]", "Description=Zenith wallpaper slideshow", "",
            "[Service]", "ExecStart=/usr/bin/env bash " + scriptPath,
            "Restart=on-failure", "RestartSec=5", "",
            "[Install]", "WantedBy=default.target"
        ].join("\n");
        installService.command = ["sh", "-c",
            "mkdir -p \"$1\" \"$(dirname \"$2\")\" && printf '%s\\n' \"$3\" > \"$2\" && printf '%s\\n' \"$4\" > \"$1/zenith-slideshow.service\" && systemctl --user daemon-reload",
            "_", PathSettings.configDir + "/systemd/user", listPath, paths, unit];
        installService.running = true;
    }
    Process {
        id: installService
        onExited: (code) => {
            if (code !== 0) return;
            serviceCmd.command = ["systemctl", "--user", "enable", "--now", "zenith-slideshow.service"];
            serviceCmd.running = true;
            CenterState.close();
        }
    }
    function stopSlideshow() {
        serviceCmd.command = ["sh", "-c", "systemctl --user disable --now zenith-slideshow.service 2>/dev/null; rm -f \"$1\"; systemctl --user daemon-reload", "_", PathSettings.configDir + "/systemd/user/zenith-slideshow.service"];
        serviceCmd.running = true;
    }
    function log(msg) { logger.command = ["sh", "-c", "echo '[$(date +%T)] " + msg + "' >> " + logPath]; logger.running = true; }

    Timer { id: wallDelay; property string wallPath: ""; interval: 600; onTriggered: { setWall.command = ["sh", "-c", "awww img --resize=crop '" + wallPath + "' --transition-type fade >> " + root.logPath + " 2>&1 && " + PathSettings.scriptsDir + "/zenith-theme.sh --autoselect"]; setWall.running = true; } }
    Timer { id: videoDelay; property string videoPath: ""; interval: 400; onTriggered: { mpvProcess.command = ["sh", "-c", "MONITOR=$(awww query | head -n1 | cut -d: -f1); if [ -z \"$MONITOR\" ]; then MONITOR=$(wlr-randr | head -n1 | awk '{print $1}'); fi; mpvpaper -vsf -o 'no-audio loop' $MONITOR '" + videoPath + "' >> " + root.logPath + " 2>&1"]; mpvProcess.running = true; CenterState.close(); } }

    Process { id: logger }
    // Only start the daemon if one is not already listening. Starting it
    // unconditionally -- which applyWallpaper() does on every single wallpaper
    // change -- makes it abort with a Rust panic ("already running on this
    // socket") whenever it is already up, which is the normal case. Those
    // panics and their backtraces were the entire contents of zenith.log.
    Process { id: awwwDaemon; command: ["sh", "-c", "awww query >/dev/null 2>&1 || awww-daemon >> " + logPath + " 2>&1"] }
    Process { id: setWall; onExited: { CenterState.close(); } }
    Process { id: killawww; command: ["killall", "awww-daemon"] }
    Process { id: killMpv; command: ["killall", "mpvpaper"] }
    Process { id: serviceCmd }
    Process { id: mpvProcess }
    Process { id: saveCurrentWall; property string path: ""; command: ["sh", "-c", "mkdir -p " + ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config && echo '" + path + "' > " + ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/.config/current_wallpaper.txt"] }
    Process { id: thumbGen; command: ["cmd.exe", "/c", "exit 0"] /* Windows */; onRunningChanged: { if (!running) { root.thumbnailsReady = true; root.refreshTrigger++; } } }
    Component.onCompleted: refreshThumbnails()

    function resetScroll() {
        root.activeSubTab = "Wallpaper";
        root.selectedIndex = 0;
        wallFlickable.contentY = 0;
        animFlickable.contentY = 0;
    }
}
