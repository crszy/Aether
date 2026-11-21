pragma Singleton
pragma ComponentBehavior: Bound

import qs.end4
import qs.end4.modules.common
import qs.end4.modules.common.functions
import Quickshell
import Quickshell.Windows as Win
import QtQuick

/**
 * Windows version of end-4's KeyringStorage (upstream: secret-tool / the GNOME keyring).
 * The same small JSON (API keys...) is kept in Windows Credential Manager under "illogical-impulse Safe Storage".
 */
Singleton {
    id: root

    signal dataChanged()

    property bool loaded: false
    property var keyringData: ({})

    readonly property string credentialTarget: "illogical-impulse Safe Storage"

    function setNestedField(path, value) {
        if (!root.keyringData)
            root.keyringData = {};
        let keys = path;
        let obj = root.keyringData;
        let parents = [obj];
        for (let i = 0; i < keys.length - 1; ++i) {
            if (!obj[keys[i]] || typeof obj[keys[i]] !== "object")
                obj[keys[i]] = {};
            obj = obj[keys[i]];
            parents.push(obj);
        }
        obj[keys[keys.length - 1]] = value;
        for (let i = keys.length - 2; i >= 0; --i)
            parents[i][keys[i]] = Object.assign({}, parents[i][keys[i]]);
        root.keyringData = Object.assign({}, root.keyringData);
        saveKeyringData();
    }

    function fetchKeyringData() {
        const data = Win.Credentials.read(root.credentialTarget);
        if (data && data.startsWith("{")) {
            try {
                root.keyringData = JSON.parse(data);
            } catch (e) {
                root.keyringData = {};
            }
        } else {
            root.keyringData = {};
        }
        root.loaded = true;
    }

    function saveKeyringData() {
        Win.Credentials.write(root.credentialTarget, JSON.stringify(root.keyringData));
        root.dataChanged();
    }
}
