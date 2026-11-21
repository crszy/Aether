import QtQuick
import Quickshell.Widgets
import qs.dms.DCommon.Common
import "../Common/Contrast.js" as Contrast

Item {
    id: root

    default property alias content: contentColumn.data
    property real paddingV: Style.spacingXL + Style.spacingS
    property real accentSize: Style.buttonHeightM * 1.6
    property Component backdrop: defaultBackdrop

    readonly property real availableWidth: width - Style.spacingXL * 2
    readonly property real backdropSize: Math.min(height, accentSize * 2.5)
    readonly property var textCandidates: [Style.onPrimaryContainer, Style.surfaceText, Style.surface, Qt.rgba(1, 1, 1, 1), Qt.rgba(0, 0, 0, 1)]
    readonly property var fullTints: [Style.withAlpha(Style.primary, Style.tonalTintAlpha), Style.withAlpha(Style.tertiary, Style.tonalTintAlpha), Style.withAlpha(Style.secondary, Style.tonalTintAlpha), Style.withAlpha(Style.onPrimaryContainer, Style.stateLayerHover)]
    readonly property color contentColor: {
        const full = surfacesUnder(fullTints);
        const pick = Contrast.readableOnAll(full, textCandidates);
        return Contrast.worstRatio(pick, full) >= 4.5 ? pick : Contrast.readableOn(Style.primaryContainer, textCandidates);
    }
    // Mid-tone containers cannot hold 4.5:1 under every tint, so the shapes fade instead of the text
    readonly property real tintScale: {
        if (Contrast.worstRatio(contentColor, surfacesUnder(fullTints)) >= 4.5)
            return 1;
        let low = 0;
        let high = 1;
        for (let i = 0; i < 8; i++) {
            const mid = (low + high) / 2;
            if (Contrast.worstRatio(contentColor, surfacesUnder(scaledTints(mid))) >= 4.5)
                low = mid;
            else
                high = mid;
        }
        return low;
    }
    readonly property var tints: scaledTints(tintScale)
    readonly property var surfaces: surfacesUnder(tints)
    readonly property color accentColor: Contrast.readableOnAll(surfaces, [Style.primary, contentColor], 3)

    function scaledTints(scale) {
        return fullTints.map(tint => Qt.rgba(tint.r, tint.g, tint.b, tint.a * scale));
    }

    function surfacesUnder(layers) {
        const over = (under, tint) => Contrast.mix(under, Qt.rgba(tint.r, tint.g, tint.b, 1), tint.a);
        const result = [Style.primaryContainer];
        layers.forEach((tint, i) => {
            const single = over(Style.primaryContainer, tint);
            result.push(single);
            layers.slice(i + 1).forEach(next => result.push(over(single, next)));
        });
        return result;
    }

    height: contentColumn.implicitHeight + paddingV * 2

    component Decoration: DMaterialShape {
        respectThemeShape: false
    }

    Component {
        id: defaultBackdrop

        Item {
            Decoration {
                width: root.backdropSize * 1.15
                height: width
                x: parent.width - width * 0.62
                y: -height * 0.42
                shape: "cookie12"
                color: root.tints[0]
                rotation: 12
            }

            Decoration {
                width: root.backdropSize * 0.8
                height: width
                x: -width * 0.38
                y: parent.height - height * 0.58
                shape: "softBurst"
                color: root.tints[1]
                rotation: -8
            }

            Decoration {
                width: root.accentSize * 0.6
                height: width
                x: parent.width * 0.12
                y: root.paddingV * 0.6
                shape: "flower"
                color: root.tints[3]
            }

            Decoration {
                width: root.accentSize * 0.75
                height: width
                x: parent.width * 0.8
                y: parent.height - height - root.paddingV * 0.5
                shape: "pill"
                color: root.tints[2]
                rotation: -40
            }
        }
    }

    ClippingRectangle {
        anchors.fill: parent
        radius: Style.cornerRadiusXL
        color: Style.primaryContainer

        Loader {
            anchors.fill: parent
            sourceComponent: root.backdrop
        }
    }

    Column {
        id: contentColumn
        anchors.centerIn: parent
        width: root.availableWidth
        spacing: Style.spacingM
    }
}
