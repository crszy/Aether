import QtQuick
import qs.dms.DCommon.Common

Item {
    id: root

    KeyNavigation.tab: keyNavigationTab
    KeyNavigation.backtab: keyNavigationBacktab

    onActiveFocusChanged: {
        if (activeFocus) {
            locationInput.forceActiveFocus();
        }
    }

    property string currentLocation: ""
    property string placeholderText: I18n.tr("Search for a location...")
    property bool _internalChange: false
    property bool _hasSelection: false
    property bool _pendingLocationUpdate: false
    property bool isLoading: false
    property string currentSearchText: ""
    property Item keyNavigationTab: null
    property Item keyNavigationBacktab: null

    signal locationSelected(string displayName, string coordinates)

    readonly property real fieldHeight: Style.fieldHeightLarge
    readonly property real resultRowHeight: Style.menuRowHeight

    function applyCurrentLocation() {
        if (locationInput.getActiveFocus()) {
            root._pendingLocationUpdate = true;
            return;
        }
        const value = root.currentLocation || "";
        if (locationInput.text !== value) {
            root._internalChange = true;
            locationInput.text = value;
            root._internalChange = false;
            root._hasSelection = value !== "";
        }
        root._pendingLocationUpdate = false;
    }

    Component.onCompleted: applyCurrentLocation()
    onCurrentLocationChanged: applyCurrentLocation()

    function resetSearchState() {
        locationSearchTimer.stop();
        dropdownHideTimer.stop();
        isLoading = false;
        searchResultsModel.clear();
    }

    // CJK city names are commonly two code points.
    function canSearch(t) {
        return t.length > 2 || (t.length >= 2 && /[぀-ヿ㐀-䶿一-鿿豈-﫿가-힯]/.test(t));
    }

    width: parent.width
    height: searchInputField.height + (searchDropdown.visible ? searchDropdown.height : 0)

    ListModel {
        id: searchResultsModel
    }

    Timer {
        id: locationSearchTimer

        interval: 500
        running: false
        repeat: false
        onTriggered: {
            if (root.canSearch(locationInput.text)) {
                searchResultsModel.clear();
                root.isLoading = true;
                const searchLocation = locationInput.text;
                root.currentSearchText = searchLocation;
                const encodedLocation = encodeURIComponent(searchLocation);
                const searchUrl = "https://nominatim.openstreetmap.org/search?q=" + encodedLocation + "&format=json&limit=5&addressdetails=1";
                Proc.runCommand("locationSearch", [Proc.dmsBin, "dl", "-4", "--timeout", "10", searchUrl], (output, exitCode) => {
                    root.isLoading = false;
                    if (exitCode !== 0) {
                        searchResultsModel.clear();
                        return;
                    }
                    if (root.currentSearchText !== locationInput.text)
                        return;
                    const raw = output.trim();
                    searchResultsModel.clear();
                    if (!raw || raw[0] !== "[") {
                        return;
                    }
                    try {
                        const data = JSON.parse(raw);
                        if (data.length === 0) {
                            return;
                        }
                        for (var i = 0; i < Math.min(data.length, 5); i++) {
                            const location = data[i];
                            if (location.display_name && location.lat && location.lon) {
                                const parts = location.display_name.split(', ');
                                let cleanName = parts[0];
                                if (parts.length > 1) {
                                    const state = parts[parts.length - 2];
                                    if (state && state !== cleanName)
                                        cleanName += `, ${state}`;
                                }
                                const query = `${location.lat},${location.lon}`;
                                searchResultsModel.append({
                                    "name": cleanName,
                                    "query": query
                                });
                            }
                        }
                    } catch (e) {}
                });
            }
        }
    }

    Timer {
        id: dropdownHideTimer

        interval: 200
        running: false
        repeat: false
        onTriggered: {
            if (!locationInput.getActiveFocus() && !searchResultsList.activeFocus && !searchDropdown.hovered)
                root.resetSearchState();
        }
    }

    Item {
        id: searchInputField

        width: parent.width
        height: root.fieldHeight

        DSearchField {
            id: locationInput

            width: parent.width
            height: parent.height
            placeholderText: root.placeholderText
            text: ""
            keyNavigationTab: searchResultsList.count > 0 ? searchResultsList.itemAtIndex(0) : root.keyNavigationTab
            Keys.onDownPressed: {
                const first = searchResultsList.itemAtIndex(0);
                if (first)
                    first.forceActiveFocus(Qt.TabFocusReason);
            }
            keyNavigationBacktab: root.keyNavigationBacktab
            onTextEdited: {
                if (root._internalChange)
                    return;
                root._hasSelection = false;
                if (getActiveFocus()) {
                    if (root.canSearch(text)) {
                        root.isLoading = true;
                        locationSearchTimer.restart();
                    } else {
                        root.resetSearchState();
                    }
                }
            }
            onFocusStateChanged: hasFocus => {
                if (hasFocus) {
                    dropdownHideTimer.stop();
                } else {
                    dropdownHideTimer.start();
                    if (root._pendingLocationUpdate)
                        root.applyCurrentLocation();
                }
            }
        }

        DIcon {
            name: root.isLoading ? "hourglass_empty" : ((searchResultsModel.count > 0 || root._hasSelection) ? "check_circle" : "error")
            size: Style.iconSizeMedium
            color: root.isLoading ? Style.onSurfaceVariant : ((searchResultsModel.count > 0 || root._hasSelection) ? Style.primary : Style.error)
            anchors.right: parent.right
            anchors.rightMargin: Style.spacingM
            anchors.verticalCenter: parent.verticalCenter
            opacity: (locationInput.getActiveFocus() && root.canSearch(locationInput.text)) ? 1 : 0

            Behavior on opacity {
                enabled: Style.currentAnimationSpeed !== Style.AnimationSpeed.None
                DAnim {
                    duration: Style.expressiveDurations.expressiveEffects
                    easing.bezierCurve: Style.expressiveCurves.expressiveEffects
                }
            }
        }
    }

    StyledRect {
        id: searchDropdown

        property bool hovered: false

        width: parent.width
        height: Math.min(Math.max(searchResultsModel.count * (root.resultRowHeight + Style.spacingXXS) + Style.spacingS * 2, root.resultRowHeight + Style.spacingS * 2), Style.menuMaxHeight / 2)
        y: searchInputField.height
        radius: Style.cornerRadiusM
        color: Style.withAlpha(Style.cardSurface, Style.popupTransparency)
        border.color: Style.outlineMedium
        border.width: Style.layerOutlineWidth
        visible: (locationInput.getActiveFocus() || searchResultsList.activeFocus) && root.canSearch(locationInput.text) && (searchResultsModel.count > 0 || root.isLoading)

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: {
                parent.hovered = true;
                dropdownHideTimer.stop();
            }
            onExited: {
                parent.hovered = false;
                if (!locationInput.getActiveFocus())
                    dropdownHideTimer.start();
            }
            acceptedButtons: Qt.NoButton
        }

        Item {
            anchors.fill: parent
            anchors.margins: Style.spacingS

            DListView {
                id: searchResultsList

                anchors.fill: parent
                clip: true
                model: searchResultsModel
                spacing: Style.spacingXXS

                onActiveFocusChanged: {
                    if (!activeFocus)
                        dropdownHideTimer.restart();
                }

                delegate: StyledButton {
                    id: resultButton
                    Accessible.name: model.name || I18n.tr("Unknown")
                    onClicked: {
                        root._internalChange = true;
                        root._hasSelection = true;
                        const selectedName = model.name;
                        const selectedQuery = model.query;
                        locationInput.text = selectedName;
                        root.locationSelected(selectedName, selectedQuery);
                        root.resetSearchState();
                        locationInput.setFocus(false);
                        root._internalChange = false;
                    }
                    width: searchResultsList.width
                    height: root.resultRowHeight
                    radius: Style.cornerRadiusS
                    color: hovered || visualFocus ? Style.withAlpha(Style.onSurface, Style.stateLayerHover) : Style.withAlpha(Style.onSurface, 0)

                    FocusRing {
                        visible: resultButton.visualFocus
                    }

                    StateLayer {
                        control: resultButton
                        stateColor: "transparent"
                        enableRipple: false
                    }

                    Row {
                        anchors.fill: parent
                        anchors.margins: Style.spacingM
                        spacing: Style.spacingS

                        DIcon {
                            name: "place"
                            size: Style.menuItemIconSize
                            color: Style.onSurfaceVariant
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        StyledText {
                            text: model.name || I18n.tr("Unknown")
                            font.pixelSize: Style.fontSizeMedium
                            color: Style.surfaceText
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            width: parent.width - Style.iconSizeLarge
                        }
                    }
                }
            }

            StyledText {
                anchors.centerIn: parent
                text: root.isLoading ? I18n.tr("Searching...") : I18n.tr("No locations found")
                font.pixelSize: Style.fontSizeMedium
                color: Style.onSurfaceVariant
                visible: searchResultsList.count === 0 && root.canSearch(locationInput.text)
            }
        }
    }
}
