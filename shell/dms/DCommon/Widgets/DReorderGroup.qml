import QtQuick

QtObject {
    id: root

    required property Item coordinateItem
    property var lists: []
    property bool active: false
    property var source: null
    property var target: null
    property Item sourceItem: null
    property int sourceIndex: -1
    property int targetIndex: -1
    property point position

    signal transferred(var sourceList, int sourceIndex, var targetList, int targetIndex)

    function registerList(list) {
        if (!lists.includes(list))
            lists = lists.concat([list]);
    }

    function unregisterList(list) {
        if (source === list || target === list)
            cancel();
        lists = lists.filter(item => item !== list);
    }

    function begin(list, index, point) {
        source = list;
        target = list;
        sourceIndex = index;
        sourceItem = list.itemAt(index);
        position = list.mapToItem(coordinateItem, point.x, point.y);
        active = true;
    }

    function move(point) {
        if (!active || !source)
            return;
        position = source.mapToItem(coordinateItem, point.x, point.y);
        let nearest = null;
        let nearestDistance = Infinity;
        for (const list of lists) {
            const area = list.dropArea;
            if (!list.enabled || !area.visible)
                continue;
            const top = area.mapToItem(coordinateItem, 0, 0).y;
            const bottom = top + area.height;
            const distance = Math.max(top - position.y, position.y - bottom, 0);
            if (distance >= nearestDistance)
                continue;
            nearest = list;
            nearestDistance = distance;
        }
        if (target && target !== nearest)
            target.gapIndex = -1;
        target = nearest;
        source.crossSectionActive = target !== source;
        if (!target || target === source) {
            targetIndex = -1;
            return;
        }
        targetIndex = target.insertionIndex(target.mapFromItem(coordinateItem, position.x, position.y).y);
        target.gapHeight = sourceItem.height;
        target.gapIndex = targetIndex;
    }

    function finish() {
        if (!active)
            return;
        if (target === source) {
            source.commit();
            clear();
            return;
        }
        const from = source;
        const to = target;
        const fromIndex = sourceIndex;
        const toIndex = targetIndex;
        cancel();
        if (to)
            transferred(from, fromIndex, to, toIndex);
    }

    function clear() {
        if (target)
            target.gapIndex = -1;
        active = false;
        source = null;
        target = null;
        sourceItem = null;
        sourceIndex = -1;
        targetIndex = -1;
    }

    function cancel() {
        const list = source;
        clear();
        if (list)
            list.cancel();
    }
}
