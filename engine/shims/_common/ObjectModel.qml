import QtQml

QtObject {
    property list<QtObject> values: []

    signal objectInsertedPre(QtObject object, int index)
    signal objectInsertedPost(QtObject object, int index)
    signal objectRemovedPre(QtObject object, int index)
    signal objectRemovedPost(QtObject object, int index)

    function indexOf(object) {
        return values.indexOf(object);
    }
}
