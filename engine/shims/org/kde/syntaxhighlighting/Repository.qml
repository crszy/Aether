pragma Singleton
import QtQml

QtObject {
    id: root

    property Component _definitionComponent: Component {
        QtObject {
            property string name: "Plain Text"
        }
    }

    function definitionForName(name) {
        return root._definitionComponent.createObject(root, {
            name: name && name.length > 0 ? name : "Plain Text",
        });
    }

    function definitionForFileName(fileName) {
        return root._definitionComponent.createObject(root, {name: "Plain Text"});
    }
}
