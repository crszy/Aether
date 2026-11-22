pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Widgets

Singleton {
    id: root

    readonly property int budgetBytes: 4 * 1024 * 1024
    property var _idle: ({})
    property var _idleKeys: []
    property int _idleBytes: 0

    Item {
        id: shelf
    }

    Component {
        id: imageComponent

        IconImage {
            required property string poolKey
            required property int poolSize
            required property int poolBytes

            anchors.fill: parent
            backer.sourceSize: Qt.size(poolSize, poolSize)
            mipmap: true
            asynchronous: true
            visible: status === Image.Ready
        }
    }

    function acquire(slot, key, source, size, dpr) {
        let item = _idle[key];
        if (item) {
            delete _idle[key];
            _idleKeys.splice(_idleKeys.indexOf(key), 1);
            _idleBytes -= item.poolBytes;
        } else {
            const px = Math.round(size * dpr);
            item = imageComponent.createObject(shelf, {
                poolKey: key,
                poolSize: size,
                poolBytes: px * px * 4,
                source: source
            });
        }
        item.parent = slot;
        return item;
    }

    function release(item) {
        item.parent = shelf;
        if (_idle[item.poolKey]) {
            item.destroy();
            return;
        }
        _idle[item.poolKey] = item;
        _idleKeys.push(item.poolKey);
        _idleBytes += item.poolBytes;
        while (_idleBytes > budgetBytes && _idleKeys.length > 0) {
            const key = _idleKeys.shift();
            const old = _idle[key];
            delete _idle[key];
            _idleBytes -= old.poolBytes;
            old.destroy();
        }
    }
}
