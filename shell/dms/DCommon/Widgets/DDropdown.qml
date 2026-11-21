import "../Common/fzf.js" as Fzf
import "ScrollConstants.js" as Scroll
import QtQuick
import Quickshell
import Quickshell.Wayland
import qs.dms.DCommon.Common

FocusScope {
    id: root

    LayoutMirroring.enabled: I18n.isRtl
    LayoutMirroring.childrenInherit: true

    property string text: ""
    property string description: ""
    property string currentValue: ""
    property var options: []
    // Parallel to options: when set, currentValue and valueChanged allow sharing a label.
    property var optionValues: []
    property var optionIcons: []
    property var optionImages: []
    property bool enableFuzzySearch: false
    property var optionIconMap: ({})
    property var optionImageMap: ({})
    property var optionColorMap: ({})
    // Handlers can fire mid-initialisation before this has a value, so rebuild from it, not from its inputs
    readonly property var optionKeys: {
        if (Array.isArray(optionValues) && optionValues.length > 0)
            return optionValues;
        return Array.isArray(options) ? options : [];
    }

    function labelOf(key) {
        if (!Array.isArray(optionValues) || optionValues.length === 0)
            return key;
        const index = optionValues.indexOf(key);
        if (index < 0 || !Array.isArray(options))
            return key;
        return options[index] ?? key;
    }

    function rebuildIconMap() {
        const icons = {};
        const images = {};
        const keys = Array.isArray(optionKeys) ? optionKeys : [];
        const iconList = Array.isArray(optionIcons) ? optionIcons : [];
        const imageList = Array.isArray(optionImages) ? optionImages : [];
        for (let i = 0; i < keys.length; i++) {
            if (iconList.length > i)
                icons[keys[i]] = iconList[i];
            if (imageList.length > i && imageList[i])
                images[keys[i]] = imageList[i];
        }
        optionIconMap = icons;
        optionImageMap = images;
    }

    onOptionKeysChanged: rebuildIconMap()
    onOptionIconsChanged: rebuildIconMap()
    onOptionImagesChanged: rebuildIconMap()

    property int popupWidthOffset: 0
    property int maxPopupHeight: Style.menuMaxHeight
    property bool openUpwards: false
    property int popupWidth: 0
    property bool alignPopupRight: false
    property int dropdownWidth: 200
    property bool showLabel: true
    property bool compactMode: !showLabel || (text === "" && description === "")
    property bool showTrigger: true
    property Item popupAnchorItem: null
    property Item focusReturnTarget: null
    property int focusPolicy: Qt.ClickFocus
    property bool addHorizontalPadding: false
    property string emptyText: ""
    property bool usePopupTransparency: !Style.isFloatingWindow(root)
    property color backgroundColor: Style.chipSurface
    property color hoverBackgroundColor: Style._blend(backgroundColor, Style.onSurface, Style.stateLayerHover)
    property color menuBackgroundColor: usePopupTransparency ? Style.floatingSurface : Style.floatingWindowSurface
    property color normalBorderColor: Style.outlineVariant
    property color focusedBorderColor: Style.primary
    property var transientSurfaceTracker: null
    // Hyprland drops a focus grab when a whitelisted popup takes its own xdg grab
    property bool popupGrabsFocus: !(Host.hyprlandFocusGrab && transientSurfaceTracker)
    property bool downKeyOpens: true
    property bool menuBlurEnabled: true

    signal valueChanged(string value)

    property bool menuOpen: false
    property bool menuClosing: false
    readonly property bool menuVisible: menuLoader.item?.visible ?? false

    property string searchQuery: ""
    property var fzfFinder: null
    property int selectedIndex: -1
    readonly property var filteredOptions: {
        if (!enableFuzzySearch || searchQuery.length === 0)
            return optionKeys;
        if (!fzfFinder)
            return optionKeys;
        return fzfFinder.find(searchQuery).map(r => r.item);
    }

    onMenuOpenChanged: {
        if (!menuOpen)
            transientSurfaceTracker?.setActive(root, false, null);
    }

    property int triggerHeight: Style.iconButtonSize
    property real triggerRadius: Style.cornerRadiusXS
    readonly property int menuItemHeight: Style.menuRowHeight
    readonly property int menuPadding: Style.menuPadding
    readonly property real menuItemRadius: Math.max(0, Style.windowRadius - menuPadding)
    readonly property int menuSpacing: Style.spacingXXS

    property bool clampMenuWidth: false

    readonly property int menuWidth: {
        if (root.popupWidth > 0)
            return root.popupWidth;
        const fieldWidth = root.showLabel ? Math.max(1, dropdown.width + root.popupWidthOffset) : root.dropdownWidth;
        if (root.clampMenuWidth || !(root.menuOpen || root.menuVisible))
            return fieldWidth;
        const windowWidth = root.QsWindow.window?.width ?? fieldWidth;
        return Math.min(Math.max(fieldWidth, widestOptionWidth()), Math.max(fieldWidth, windowWidth));
    }

    FontMetrics {
        id: optionMetrics
        font.family: Style.fontFor("ui")
        font.pixelSize: Style.fontSizeMedium
        font.weight: Style.fontWeightMedium
    }

    function widestOptionWidth() {
        let textWidth = 0;
        for (const key of root.optionKeys)
            textWidth = Math.max(textWidth, optionMetrics.advanceWidth(root.labelOf(key)));
        let leading = 0;
        if (Object.keys(root.optionColorMap ?? {}).length > 0)
            leading += Style.iconSizeSmall + Style.spacingS;
        if (Object.keys(root.optionIconMap).length > 0 || Object.keys(root.optionImageMap).length > 0)
            leading += Style.iconSizeMedium + Style.spacingS;
        // The option text reserves its row's left margin twice, hence three spacingM
        return Math.ceil(textWidth + leading + Style.spacingM * 3 + root.menuPadding * 2);
    }

    readonly property int menuHeight: {
        let h = root.enableFuzzySearch ? root.triggerHeight + Style.spacingXS : 0;
        if (root.optionKeys.length === 0 && root.emptyText !== "")
            h += menuItemHeight;
        else
            h += Math.min(root.filteredOptions.length, 10) * (menuItemHeight + menuSpacing);
        return Math.min(root.maxPopupHeight, h + menuPadding * 2);
    }

    function initFinder() {
        fzfFinder = new Fzf.Finder(root.optionKeys, {
            "selector": key => root.labelOf(key),
            "limit": 50,
            "casing": "case-insensitive",
            "sort": true,
            "tiebreakers": [(a, b, selector) => selector(a.item).length - selector(b.item).length]
        });
    }

    function scrollMenuTo(index, mode) {
        const menu = menuLoader.item;
        if (!menu)
            return;
        menu.scrollTo(index, mode);
    }

    function selectNext() {
        if (filteredOptions.length === 0)
            return;
        selectedIndex = (selectedIndex + 1) % filteredOptions.length;
        scrollMenuTo(selectedIndex, ListView.Contain);
    }

    function selectPrevious() {
        if (filteredOptions.length === 0)
            return;
        selectedIndex = selectedIndex <= 0 ? filteredOptions.length - 1 : selectedIndex - 1;
        scrollMenuTo(selectedIndex, ListView.Contain);
    }

    function selectCurrent() {
        if (selectedIndex < 0 || selectedIndex >= filteredOptions.length)
            return;
        root.currentValue = filteredOptions[selectedIndex];
        root.valueChanged(filteredOptions[selectedIndex]);
        root.closeDropdownMenu();
    }

    function positionMenuInHost() {
        const menu = menuLoader.item;
        const qsWin = root.QsWindow.window;
        const anchorItem = root.popupAnchorItem || dropdown;
        if (!menu || !qsWin || !anchorItem)
            return false;

        menu.anchor.window = qsWin;
        menu.anchor.rect.x = 0;
        menu.anchor.rect.y = 0;
        menu.anchor.edges = Edges.Top | Edges.Left;
        menu.anchor.gravity = Edges.Bottom | Edges.Right;
        menu.anchor.margins.top = 0;
        menu.anchor.margins.bottom = 0;
        menu.anchor.adjustment = PopupAdjustment.None;
        menu.implicitWidth = qsWin.width;
        menu.implicitHeight = qsWin.height;

        const pos = root.QsWindow.itemPosition(anchorItem);
        const menuW = root.menuWidth;
        const menuH = root.menuHeight;
        const gap = Style.spacingXS;
        let alignRight = root.alignPopupRight;
        if (!alignRight && pos.x + menuW > qsWin.width)
            alignRight = true;
        if (alignRight && pos.x + anchorItem.width - menuW < 0)
            alignRight = false;
        let x = alignRight ? pos.x + anchorItem.width - menuW : pos.x;
        let goUp = root.openUpwards;
        if (!goUp && pos.y + anchorItem.height + menuH + gap > qsWin.height)
            goUp = true;
        if (goUp && pos.y - menuH - gap < 0)
            goUp = false;
        let y = goUp ? pos.y - menuH - gap : pos.y + anchorItem.height + gap;
        menu.menuX = Math.max(0, Math.min(qsWin.width - menuW, x));
        menu.menuY = Math.max(0, Math.min(qsWin.height - menuH, y));
        menu.opensUpwards = goUp;
        menu.anchor.updateAnchor();
        return true;
    }

    function closeDropdownMenu() {
        if (!root.menuOpen && !root.menuVisible)
            return;
        root.menuClosing = true;
        root.menuOpen = false;
        closeTimer.restart();
    }

    function showDropdownMenu() {
        if (root.optionKeys.length === 0)
            return;
        if (root.menuOpen)
            return;
        if (!root.QsWindow.window)
            return;

        root.menuOpen = true;
        const menu = menuLoader.item;
        if (!menu || !positionMenuInHost()) {
            root.menuOpen = false;
            return;
        }

        closeTimer.stop();
        root.menuClosing = false;
        menu.visible = true;
        const currentIndex = root.optionKeys.indexOf(root.currentValue);
        scrollMenuTo(currentIndex >= 0 ? currentIndex : 0, ListView.Beginning);
        root.selectedIndex = root.filteredOptions.indexOf(root.currentValue);
        transientSurfaceTracker?.setActive(root, true, menu);
    }

    function openDropdownMenu() {
        if (root.menuOpen) {
            closeDropdownMenu();
            return;
        }
        showDropdownMenu();
    }

    function resetSearch() {
        const menu = menuLoader.item;
        if (menu)
            menu.clearSearch();
        root.fzfFinder = null;
        root.searchQuery = "";
        root.selectedIndex = -1;
    }

    readonly property real labelFreeWidth: Style.spacingS * 2 + contentRow.implicitWidth + Style.spacingXS + Style.iconSize
    width: !showTrigger ? 0 : (!showLabel ? labelFreeWidth : compactMode ? dropdownWidth : parent.width)
    implicitHeight: !showTrigger ? 0 : (compactMode ? triggerHeight : Math.max(Style.listItemHeight + Style.spacingXS, labelColumn.implicitHeight + Style.spacingM))
    activeFocusOnTab: showTrigger && enabled && focusPolicy !== Qt.NoFocus
    readonly property Item focusTarget: dropdown
    readonly property var focusTargets: showTrigger ? [dropdown] : []

    Component.onDestruction: {
        transientSurfaceTracker?.unregister(root);
        const menu = menuLoader.item;
        if (menu && (root.menuOpen || menu.visible))
            menu.visible = false;
    }

    Connections {
        target: root.transientSurfaceTracker
        ignoreUnknownSignals: true

        function onCloseRequested() {
            root.closeDropdownMenu();
        }
    }

    Timer {
        id: closeTimer

        interval: Style.reduceMotion ? 0 : Style.expressiveDurations.expressiveEffects
        onTriggered: {
            const menu = menuLoader.item;
            if (menu)
                menu.visible = false;
            root.menuClosing = false;
        }
    }

    Column {
        id: labelColumn

        anchors.left: parent.left
        anchors.right: dropdown.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.addHorizontalPadding ? Style.spacingM : 0
        anchors.rightMargin: Style.spacingL
        spacing: Style.spacingXS
        visible: !root.compactMode && root.showTrigger

        StyledText {
            text: root.text
            font.pixelSize: Style.fontSizeMedium
            color: Style.surfaceText
            font.weight: Style.fontWeightMedium
            width: parent.width
            horizontalAlignment: Text.AlignLeft
        }

        StyledText {
            text: root.description
            font.pixelSize: Style.fontSizeSmall
            color: Style.onSurfaceVariant
            visible: description.length > 0
            wrapMode: Text.WordWrap
            width: parent.width
            horizontalAlignment: Text.AlignLeft
        }
    }

    StyledButton {
        id: dropdown
        focus: true
        focusPolicy: root.focusPolicy
        Accessible.ignored: root.Accessible.ignored

        Accessible.role: Accessible.ComboBox
        Accessible.name: root.Accessible.name || root.text || root.labelOf(root.currentValue)
        Accessible.description: root.Accessible.description || root.description
        onClicked: root.openDropdownMenu()
        Keys.onDownPressed: event => {
            if (!root.downKeyOpens && !(event.modifiers & Qt.AltModifier)) {
                event.accepted = false;
                return;
            }
            root.showDropdownMenu();
        }

        readonly property bool active: root.menuVisible || visualFocus

        visible: root.showTrigger
        width: root.compactMode ? parent.width : (root.popupWidth === -1 ? undefined : (root.popupWidth > 0 ? root.popupWidth : root.dropdownWidth))
        height: root.triggerHeight
        anchors.right: parent.right
        anchors.rightMargin: root.addHorizontalPadding && !root.compactMode ? Style.spacingM : 0
        anchors.verticalCenter: parent.verticalCenter
        radius: root.triggerRadius
        color: {
            const lit = dropdown.hovered || root.menuVisible;
            if (!root.showLabel)
                return root.enabled && lit ? Style.withAlpha(Style.onSurface, Style.stateLayerHover) : "transparent";
            if (!root.enabled)
                return Style.onSurface_12;
            return lit ? root.hoverBackgroundColor : Style.foregroundColor(root.backgroundColor, !root.usePopupTransparency);
        }
        border.color: !root.enabled || (!root.showLabel && !visualFocus) ? "transparent" : (active || dropdown.hovered ? root.focusedBorderColor : root.normalBorderColor)
        border.width: active ? Style.outlineWidthFocused : Style.outlineWidth

        Behavior on color {
            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DColorAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        Behavior on border.color {
            enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
            DColorAnim {
                duration: Style.expressiveDurations.expressiveEffects
                easing.bezierCurve: Style.expressiveCurves.expressiveEffects
            }
        }

        StateLayer {
            control: dropdown
            disabled: !root.enabled
            stateColor: "transparent"
            enableRipple: false
        }

        Row {
            id: contentRow

            anchors.left: parent.left
            anchors.right: expandIcon.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: root.showLabel ? Style.spacingL : Style.spacingS
            anchors.rightMargin: root.showLabel ? Style.spacingS : Style.spacingXS
            spacing: Style.spacingS

            DColorSwatch {
                id: triggerSwatch

                width: Style.iconSizeSmall
                height: Style.iconSizeSmall
                anchors.verticalCenter: parent.verticalCenter
                visible: root.optionColorMap[root.currentValue] !== undefined
                swatchColor: visible ? root.optionColorMap[root.currentValue] : "transparent"
            }

            DIcon {
                id: triggerIcon

                name: root.optionIconMap[root.currentValue] ?? ""
                size: Style.iconSizeMedium
                color: root.enabled ? Style.surfaceText : Style.onSurface_38
                anchors.verticalCenter: parent.verticalCenter
                visible: name !== ""
            }

            Loader {
                id: triggerImage

                readonly property string imageValue: root.optionImageMap[root.currentValue] ?? ""

                width: Style.iconSizeMedium
                height: Style.iconSizeMedium
                anchors.verticalCenter: parent.verticalCenter
                active: imageValue !== ""
                visible: active
                sourceComponent: AppIconRenderer {
                    iconValue: triggerImage.imageValue
                    iconSize: Style.iconSizeMedium
                    fallbackText: root.labelOf(root.currentValue).charAt(0).toUpperCase()
                }
            }

            StyledText {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.showLabel
                text: root.currentValue !== "" ? root.labelOf(root.currentValue) : root.emptyText
                font.pixelSize: Style.fontSizeMedium
                color: !root.enabled ? Style.onSurface_38 : (root.currentValue !== "" ? Style.surfaceText : Style.onSurfaceVariant)
                width: contentRow.width - (triggerSwatch.visible ? triggerSwatch.width + contentRow.spacing : 0) - (triggerIcon.visible ? triggerIcon.width + contentRow.spacing : 0) - (triggerImage.visible ? triggerImage.width + contentRow.spacing : 0)
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                horizontalAlignment: Text.AlignLeft
            }
        }

        DIcon {
            id: expandIcon

            name: "arrow_drop_down"
            size: Style.iconSize
            color: root.enabled ? Style.onSurfaceVariant : Style.onSurface_38
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: Style.spacingS
            rotation: root.menuVisible ? 180 : 0

            Behavior on rotation {
                enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveFastSpatial
                    easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                }
            }
        }
    }

    Loader {
        id: menuLoader

        active: root.menuOpen || root.menuClosing

        sourceComponent: PopupWindow {
            id: dropdownMenu

            grabFocus: root.popupGrabsFocus

            property real menuX: 0
            property real menuY: 0
            property bool opensUpwards: false

            function scrollTo(index, mode) {
                if (mode === ListView.Contain) {
                    listView.revealIndex(index);
                    return;
                }
                listView.positionViewAtIndex(index, mode);
            }

            function focusInput() {
                const field = searchLoader.item;
                if (root.enableFuzzySearch && field) {
                    field.forceActiveFocus();
                    return;
                }
                menuKeyHandler.forceActiveFocus();
            }

            function clearSearch() {
                const field = searchLoader.item;
                if (!field)
                    return;
                field.text = "";
            }

            color: "transparent"
            visible: false

            onVisibleChanged: {
                if (!visible && root.menuOpen)
                    root.closeDropdownMenu();
                if (!visible && root.enabled && (root.showTrigger || root.focusReturnTarget))
                    (root.focusReturnTarget ?? dropdown).forceActiveFocus(Qt.PopupFocusReason);
                if (visible)
                    Qt.callLater(focusInput);
            }

            // Windows: no compositor blur - BackgroundEffect.blurRegion: (visible && !root.menuClosing && root.menuBlurEnabled && Style.blurLayersActive) ? menuBlurRegion : null

            Region {
                id: menuBlurRegion
                x: menuContainer.x
                y: menuContainer.y
                width: menuContainer.width
                height: menuContainer.height
                radius: Style.windowRadius
            }

            MouseArea {
                anchors.fill: parent
                z: -1
                enabled: dropdownMenu.visible
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                onClicked: root.closeDropdownMenu()
            }

            FocusScope {
                id: menuKeyHandler
                anchors.fill: parent
                focus: dropdownMenu.visible
                z: 0

                Keys.onPressed: event => {
                    switch (event.key) {
                    case Qt.Key_Escape:
                        root.closeDropdownMenu();
                        event.accepted = true;
                        return;
                    case Qt.Key_Down:
                        root.selectNext();
                        event.accepted = true;
                        return;
                    case Qt.Key_Up:
                        root.selectPrevious();
                        event.accepted = true;
                        return;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        root.selectCurrent();
                        event.accepted = true;
                        return;
                    }
                }
            }

            Item {
                id: menuContainer
                x: dropdownMenu.menuX
                y: dropdownMenu.menuY
                width: root.menuWidth
                height: root.menuHeight
                z: 1

                Rectangle {
                    id: contentSurface

                    readonly property bool shown: dropdownMenu.visible && !root.menuClosing

                    anchors.fill: parent
                    LayoutMirroring.enabled: I18n.isRtl
                    LayoutMirroring.childrenInherit: true
                    color: "transparent"
                    border.color: "transparent"
                    border.width: 0
                    radius: Style.windowRadius
                    opacity: shown ? 1 : 0
                    scale: shown ? 1 : Style.popupEnterScale
                    transformOrigin: dropdownMenu.opensUpwards ? Item.Bottom : Item.Top

                    Behavior on opacity {
                        enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                        DAnim {
                            duration: Style.expressiveDurations.expressiveEffects
                            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                        }
                    }
                    Behavior on scale {
                        enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                        DAnim {
                            duration: Style.expressiveDurations.expressiveFastSpatial
                            easing.bezierCurve: Style.expressiveCurves.expressiveDefaultSpatial
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        acceptedButtons: Qt.AllButtons
                        onPressed: mouse => mouse.accepted = true
                        onClicked: mouse => mouse.accepted = true
                    }

                    ElevationShadow {
                        id: shadowLayer
                        anchors.fill: parent
                        z: -1
                        level: Style.elevationLevel2
                        fallbackOffset: Style.spacingXS
                        targetRadius: contentSurface.radius
                        targetColor: root.menuBackgroundColor
                        borderColor: root.normalBorderColor
                        borderWidth: Style.outlineWidth
                        shadowEnabled: Style.elevationEnabled && Style.popoutElevationEnabled
                    }

                    Column {
                        anchors.fill: parent
                        anchors.margins: root.menuPadding

                        Rectangle {
                            id: searchContainer

                            width: parent.width
                            height: root.triggerHeight
                            visible: root.enableFuzzySearch
                            radius: root.menuItemRadius
                            color: "transparent"

                            Loader {
                                id: searchLoader

                                anchors.fill: parent
                                active: root.enableFuzzySearch

                                sourceComponent: DTextField {
                                    id: searchField

                                    placeholderText: I18n.tr("Search...")
                                    cornerRadius: root.menuItemRadius
                                    backgroundColor: root.backgroundColor
                                    normalBorderColor: root.normalBorderColor
                                    focusedBorderColor: root.focusedBorderColor
                                    topPadding: Style.spacingS
                                    bottomPadding: Style.spacingS
                                    Component.onCompleted: text = root.searchQuery
                                    onTextChanged: searchDebounce.restart()
                                    Keys.onDownPressed: root.selectNext()
                                    Keys.onUpPressed: root.selectPrevious()
                                    Keys.onReturnPressed: root.selectCurrent()
                                    Keys.onEnterPressed: root.selectCurrent()
                                    Keys.onEscapePressed: event => {
                                        root.closeDropdownMenu();
                                        event.accepted = true;
                                    }
                                    Keys.onPressed: event => {
                                        if (!(event.modifiers & Qt.ControlModifier))
                                            return;
                                        switch (event.key) {
                                        case Qt.Key_N:
                                        case Qt.Key_J:
                                            root.selectNext();
                                            event.accepted = true;
                                            break;
                                        case Qt.Key_P:
                                        case Qt.Key_K:
                                            root.selectPrevious();
                                            event.accepted = true;
                                            break;
                                        }
                                    }

                                    Timer {
                                        id: searchDebounce
                                        interval: 50
                                        onTriggered: {
                                            if (searchField.text === root.searchQuery)
                                                return;
                                            if (!root.fzfFinder)
                                                root.initFinder();
                                            root.searchQuery = searchField.text;
                                            root.selectedIndex = -1;
                                        }
                                    }
                                }
                            }
                        }

                        Item {
                            width: parent.width
                            height: Style.spacingXS
                            visible: root.enableFuzzySearch
                        }

                        Item {
                            width: parent.width
                            height: root.menuItemHeight
                            visible: root.optionKeys.length === 0 && root.emptyText !== ""

                            StyledText {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Style.spacingM
                                anchors.rightMargin: Style.spacingM
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.emptyText
                                font.pixelSize: Style.fontSizeMedium
                                color: Style.onSurfaceVariant
                                horizontalAlignment: Text.AlignLeft
                            }
                        }

                        DListView {
                            id: listView

                            width: parent.width
                            height: parent.height - (root.enableFuzzySearch ? searchContainer.height + Style.spacingXS : 0) - (root.optionKeys.length === 0 && root.emptyText !== "" ? root.menuItemHeight : 0)
                            clip: true
                            visible: root.optionKeys.length > 0
                            model: ScriptModel {
                                values: root.filteredOptions
                            }
                            spacing: root.menuSpacing

                            interactive: true
                            flickDeceleration: Scroll.flickDeceleration
                            maximumFlickVelocity: Scroll.maximumFlickVelocity
                            boundsBehavior: Flickable.DragAndOvershootBounds
                            boundsMovement: Flickable.FollowBoundsBehavior
                            pressDelay: 0
                            flickableDirection: Flickable.VerticalFlick

                            delegate: StyledButton {
                                id: delegateRoot

                                focusPolicy: Qt.NoFocus
                                Accessible.role: Accessible.ListItem
                                Accessible.name: root.labelOf(modelData)
                                Accessible.selected: isCurrentValue
                                onClicked: {
                                    root.currentValue = modelData;
                                    root.valueChanged(modelData);
                                    root.closeDropdownMenu();
                                }

                                required property var modelData
                                required property int index

                                HoverHandler {
                                    cursorShape: Qt.PointingHandCursor
                                }

                                property bool isSelected: root.selectedIndex === index
                                property bool isCurrentValue: root.currentValue === modelData
                                property string iconName: root.optionIconMap[modelData] ?? ""
                                property string imageValue: root.optionImageMap[modelData] ?? ""
                                property var swatchColor: root.optionColorMap[modelData]
                                readonly property color contentColor: isCurrentValue ? Style.onSelectedContainer : Style.surfaceText

                                width: ListView.view.width
                                height: root.menuItemHeight
                                radius: root.menuItemRadius
                                color: {
                                    if (isCurrentValue)
                                        return Style.selectedContainer;
                                    if (isSelected)
                                        return Style.withAlpha(Style.onSurface, Style.stateLayerFocus);
                                    if (delegateRoot.hovered || delegateRoot.visualFocus)
                                        return Style.withAlpha(Style.onSurface, Style.stateLayerHover);
                                    return Style.withAlpha(Style.onSurface, 0);
                                }

                                Behavior on color {
                                    enabled: !Style.reduceMotion && Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                                    DColorAnim {
                                        duration: Style.expressiveDurations.expressiveEffects
                                        easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                                    }
                                }

                                Row {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.leftMargin: Style.spacingM
                                    anchors.rightMargin: Style.spacingM
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Style.spacingS

                                    DColorSwatch {
                                        id: optionSwatch

                                        width: Style.iconSizeSmall
                                        height: Style.iconSizeSmall
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: delegateRoot.swatchColor !== undefined
                                        swatchColor: visible ? delegateRoot.swatchColor : Style.withAlpha(delegateRoot.swatchColor, 0)
                                        ringColor: delegateRoot.isCurrentValue ? Style.primary : Style.outline
                                    }

                                    DIcon {
                                        name: delegateRoot.iconName
                                        size: Style.menuItemIconSize
                                        color: delegateRoot.contentColor
                                        visible: name !== ""
                                    }

                                    Loader {
                                        id: optionImage

                                        width: Style.menuItemIconSize
                                        height: Style.menuItemIconSize
                                        anchors.verticalCenter: parent.verticalCenter
                                        active: delegateRoot.imageValue !== ""
                                        visible: active
                                        sourceComponent: AppIconRenderer {
                                            iconValue: delegateRoot.imageValue
                                            iconSize: Style.menuItemIconSize
                                            iconColor: delegateRoot.contentColor
                                            fallbackText: root.labelOf(delegateRoot.modelData).charAt(0).toUpperCase()
                                        }
                                    }

                                    StyledText {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: root.labelOf(delegateRoot.modelData)
                                        font.pixelSize: Style.fontSizeMedium
                                        color: delegateRoot.contentColor
                                        font.weight: delegateRoot.isCurrentValue ? Style.fontWeightMedium : Style.fontWeight
                                        width: root.popupWidth > 0 ? undefined : (delegateRoot.width - parent.x - Style.spacingM * 2 - (optionSwatch.visible ? optionSwatch.width + parent.spacing : 0) - (optionImage.visible ? optionImage.width + parent.spacing : 0))
                                        elide: root.popupWidth > 0 ? Text.ElideNone : Text.ElideRight
                                        wrapMode: Text.NoWrap
                                        horizontalAlignment: Text.AlignLeft
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
