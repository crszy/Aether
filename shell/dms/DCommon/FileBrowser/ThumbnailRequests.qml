pragma ComponentBehavior: Bound

import QtQuick

QtObject {
    property var backend: null
    property string size: "normal"
    property real sizeLimit: -1

    property var _inFlight: ({})

    function wanted(entry) {
        if (!entry.thumbnailable || entry.thumbnail !== "" || _inFlight[entry.path] === true)
            return false;
        return sizeLimit < 0 || entry.size <= sizeLimit || !(entry.mime ?? "").startsWith("image/");
    }

    function request(paths, watchId, apply) {
        if (paths.length === 0 || !backend)
            return;
        for (const path of paths)
            _inFlight[path] = true;
        backend.thumbnails(paths, size, watchId, result => {
            for (const path of paths)
                delete _inFlight[path];
            if (result.error)
                return;
            apply((result.results || []).filter(item => item.thumbnail));
        });
    }

    function reset() {
        _inFlight = ({});
    }
}
