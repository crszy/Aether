import QtQuick
import qs.dms.DCommon.Common

Rectangle {
    id: root
    property Item target
    property bool expressive: false
    property bool closing: false
    signal closed
    height: expressive ? LockMetrics.keyboardHeight : 60 * 5
    anchors.bottom: parent.bottom
    anchors.horizontalCenter: parent.horizontalCenter
    width: expressive ? Math.min(parent.width, LockMetrics.keyboardWidth) : parent.width
    radius: expressive ? Style.cornerRadiusM + Style.spacingS : 0
    bottomLeftRadius: 0
    bottomRightRadius: 0
    Keys.onEscapePressed: root.dismissed()
    onOpacityChanged: {
        if (closing && opacity === 0)
            closed();
    }
    Behavior on opacity {
        enabled: root.expressive
        NumberAnimation {
            duration: LockMetrics.effectsDuration
            easing.type: Easing.BezierSpline
            easing.bezierCurve: Style.expressiveCurves.expressiveEffects
        }
    }
    color: Style.cardSurface
    border.width: Style.layerOutlineWidth
    border.color: Style.outlineMedium

    signal dismissed

    property double rowSpacing: expressive ? Style.spacingS : 0.01 * width
    property double columnSpacing: expressive ? Style.spacingS : 0.02 * height
    property bool shift: false
    property bool symbols: false
    property double columns: 10
    property double rows: 4

    property string strShift: '\u2191'
    property string strBackspace: "Backspace"
    property string strEnter: "Enter"
    property string strClose: "keyboard_hide"

    property var modelKeyboard: {
        "row_1": [
            {
                "text": 'q',
                "symbol": '1',
                "width": 1
            },
            {
                "text": 'w',
                "symbol": '2',
                "width": 1
            },
            {
                "text": 'e',
                "symbol": '3',
                "width": 1
            },
            {
                "text": 'r',
                "symbol": '4',
                "width": 1
            },
            {
                "text": 't',
                "symbol": '5',
                "width": 1
            },
            {
                "text": 'y',
                "symbol": '6',
                "width": 1
            },
            {
                "text": 'u',
                "symbol": '7',
                "width": 1
            },
            {
                "text": 'i',
                "symbol": '8',
                "width": 1
            },
            {
                "text": 'o',
                "symbol": '9',
                "width": 1
            },
            {
                "text": 'p',
                "symbol": '0',
                "width": 1
            }
        ],
        "row_2": [
            {
                "text": 'a',
                "symbol": '-',
                "width": 1
            },
            {
                "text": 's',
                "symbol": '/',
                "width": 1
            },
            {
                "text": 'd',
                "symbol": ':',
                "width": 1
            },
            {
                "text": 'f',
                "symbol": ';',
                "width": 1
            },
            {
                "text": 'g',
                "symbol": '(',
                "width": 1
            },
            {
                "text": 'h',
                "symbol": ')',
                "width": 1
            },
            {
                "text": 'j',
                "symbol": '€',
                "width": 1
            },
            {
                "text": 'k',
                "symbol": '&',
                "width": 1
            },
            {
                "text": 'l',
                "symbol": '@',
                "width": 1
            }
        ],
        "row_3": [
            {
                "text": strShift,
                "symbol": strShift,
                "width": 1.5
            },
            {
                "text": 'z',
                "symbol": '.',
                "width": 1
            },
            {
                "text": 'x',
                "symbol": ',',
                "width": 1
            },
            {
                "text": 'c',
                "symbol": '?',
                "width": 1
            },
            {
                "text": 'v',
                "symbol": '!',
                "width": 1
            },
            {
                "text": 'b',
                "symbol": "'",
                "width": 1
            },
            {
                "text": 'n',
                "symbol": "%",
                "width": 1
            },
            {
                "text": 'm',
                "symbol": '"',
                "width": 1
            },
            {
                "text": strBackspace,
                "symbol": strBackspace,
                "width": 1.5
            }
        ],
        "row_4": [
            {
                "text": strClose,
                "symbol": strClose,
                "width": 1.5
            },
            {
                "text": "123",
                "symbol": 'ABC',
                "width": 1.5
            },
            {
                "text": ' ',
                "symbol": ' ',
                "width": 4.5
            },
            {
                "text": '.',
                "symbol": '.',
                "width": 1
            },
            {
                "text": strEnter,
                "symbol": strEnter,
                "width": 1.5
            }
        ]
    }

    property var tableKeyEvent: {
        "_0": Qt.Key_0,
        "_1": Qt.Key_1,
        "_2": Qt.Key_2,
        "_3": Qt.Key_3,
        "_4": Qt.Key_4,
        "_5": Qt.Key_5,
        "_6": Qt.Key_6,
        "_7": Qt.Key_7,
        "_8": Qt.Key_8,
        "_9": Qt.Key_9,
        "_a": Qt.Key_A,
        "_b": Qt.Key_B,
        "_c": Qt.Key_C,
        "_d": Qt.Key_D,
        "_e": Qt.Key_E,
        "_f": Qt.Key_F,
        "_g": Qt.Key_G,
        "_h": Qt.Key_H,
        "_i": Qt.Key_I,
        "_j": Qt.Key_J,
        "_k": Qt.Key_K,
        "_l": Qt.Key_L,
        "_m": Qt.Key_M,
        "_n": Qt.Key_N,
        "_o": Qt.Key_O,
        "_p": Qt.Key_P,
        "_q": Qt.Key_Q,
        "_r": Qt.Key_R,
        "_s": Qt.Key_S,
        "_t": Qt.Key_T,
        "_u": Qt.Key_U,
        "_v": Qt.Key_V,
        "_w": Qt.Key_W,
        "_x": Qt.Key_X,
        "_y": Qt.Key_Y,
        "_z": Qt.Key_Z,
        "_←": Qt.Key_Backspace,
        "_return": Qt.Key_Return,
        "_ ": Qt.Key_Space,
        "_-": Qt.Key_Minus,
        "_/": Qt.Key_Slash,
        "_:": Qt.Key_Colon,
        "_;": Qt.Key_Semicolon,
        "_(": Qt.Key_BracketLeft,
        "_)": Qt.Key_BracketRight,
        "_€": parseInt("20ac", 16),
        "_&": Qt.Key_Ampersand,
        "_@": Qt.Key_At,
        '_"': Qt.Key_QuoteDbl,
        "_.": Qt.Key_Period,
        "_,": Qt.Key_Comma,
        "_?": Qt.Key_Question,
        "_!": Qt.Key_Exclam,
        "_'": Qt.Key_Apostrophe,
        "_%": Qt.Key_Percent,
        "_*": Qt.Key_Asterisk
    }

    component Key: Loader {
        id: keySlot
        required property var modelData
        readonly property string keyText: root.symbols ? modelData.symbol : root.shift ? modelData.text.toUpperCase() : modelData.text
        width: modelData.width * (keyboard_container.width + (root.expressive ? root.rowSpacing : 0)) / root.columns - root.rowSpacing
        height: (keyboard_container.height + (root.expressive ? root.columnSpacing : 0)) / root.rows - root.columnSpacing
        sourceComponent: root.expressive ? expressiveKey : classicKey

        Component {
            id: expressiveKey
            ExpressiveKeyboardKey {
                text: keySlot.keyText
                isShift: root.shift && text === root.strShift
                onClicked: root.clicked(text)
            }
        }

        Component {
            id: classicKey
            CustomButtonKeyboard {
                text: keySlot.keyText
                isShift: root.shift && text === root.strShift
                onClicked: root.clicked(text)
            }
        }
    }

    Item {
        id: keyboard_container
        anchors.left: parent.left
        anchors.leftMargin: root.expressive ? Style.spacingS : 5
        anchors.right: parent.right
        anchors.rightMargin: root.expressive ? Style.spacingS : 0
        anchors.top: parent.top
        anchors.topMargin: root.expressive ? Style.spacingS : 5
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.expressive ? Style.spacingS : 5

        Column {
            spacing: columnSpacing

            Row {
                id: row_1
                spacing: rowSpacing
                Repeater {
                    model: modelKeyboard["row_1"]
                    delegate: Key {}
                }
            }
            Row {
                id: row_2
                spacing: rowSpacing
                Repeater {
                    model: modelKeyboard["row_2"]
                    delegate: Key {}
                }
            }
            Row {
                id: row_3
                spacing: rowSpacing
                Repeater {
                    model: modelKeyboard["row_3"]
                    delegate: Key {}
                }
            }
            Row {
                id: row_4
                spacing: rowSpacing
                Repeater {
                    model: modelKeyboard["row_4"]
                    delegate: Key {}
                }
            }
        }
    }
    signal clicked(string text)

    Connections {
        target: root
        function onClicked(text) {
            if (!root.target)
                return;
            switch (text) {
            case root.strShift:
                root.shift = !root.shift;
                return;
            case '123':
                root.symbols = true;
                return;
            case 'ABC':
                root.symbols = false;
                return;
            case root.strEnter:
                if (root.target.accepted)
                    root.target.accepted();
                return;
            case root.strClose:
                root.dismissed();
                return;
            case root.strBackspace:
                root.target.backspace();
                break;
            default:
                root.target.insertText(root.symbols ? text : root.shift ? text.toUpperCase() : text);
            }
            if (root.shift)
                root.shift = false;
        }
    }
}
