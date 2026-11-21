import QtQuick
import QtQuick.Controls 2.15
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import "../.."
import "../../../"
import "../../../services"

Rectangle {
    id: todoRoot
    color: "transparent"

    readonly property string todoPath: ((Quickshell.env("HOME") || Quickshell.env("USERPROFILE") || "").split("\\").join("/")) + "/Documents/Task/todo.json"
    property bool isLoaded: false
    property int activeTabIndex: 0
    property var todoData: [] // Central source of truth
    property int editingIndex: -1 // Track which task is being edited
    property int selectedIndex: 0

    function handleKeys(event) {
        if (editingIndex !== -1 || inputField.visible || tabNameInputContainer.visible || keyInputRow.visible) return;
        let count = tasksModel.count;
        if (count === 0) return;

        if (event.key === Qt.Key_Down || event.key === Qt.Key_J) {
            selectedIndex = Math.min(count - 1, selectedIndex + 1);
            taskListView.positionViewAtIndex(selectedIndex, ListView.Contain);
            event.accepted = true;
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_K) {
            selectedIndex = Math.max(0, selectedIndex - 1);
            taskListView.positionViewAtIndex(selectedIndex, ListView.Contain);
            event.accepted = true;
        } else if (event.key === Qt.Key_Space) {
            if (selectedIndex >= 0 && selectedIndex < count) {
                let currentItem = tasksModel.get(selectedIndex);
                let newCompleted = !currentItem.completed;
                tasksModel.setProperty(selectedIndex, "completed", newCompleted);
                tasksModel.setProperty(selectedIndex, "dirty", true);
                saveTasks();
                event.accepted = true;
            }
        }
    }

    property bool reloadPending: false

    function cancelEdit() {
        editingIndex = -1;
        inputField.visible = false;
        inputField.text = "";
        if (reloadPending) reloadTasks();
    }

    // Re-reading mid-edit would clear the model out from under the text field,
    // so a reload that lands while the user is typing waits for them to finish.
    function reloadTasks() {
        if (editingIndex !== -1 || inputField.visible) { reloadPending = true; return; }
        reloadPending = false;
        loadProcess.running = false;
        loadProcess.running = true;
    }

    // The task file is read the first time the tab is shown, not at shell
    // start: nothing can look at the list before then.
    property bool started: false

    function startIfNeeded() {
        if (started || !visible) return;
        started = true;
        loadProcess.running = true;
    }

    Component.onCompleted: startIfNeeded()

    // Auto-close input fields when menu is hidden without losing data
    onVisibleChanged: {
        if (!visible) {
            cancelEdit();
            tabNameInputContainer.visible = false;
        } else {
            startIfNeeded();
            if (TodoistService.hasToken) {
                // Pull anything added from the phone or web app since last time.
                TodoistService.syncNow();
            }
        }
    }

    Connections {
        target: TodoistService
        // The sync script rewrites todo.json in place, so re-read it rather
        // than keeping the stale in-memory copy.
        function onTasksChanged() {
            todoRoot.reloadTasks();
        }
    }

    // Every row carries the same four roles, always. A ListModel fixes its
    // roles from the first item appended, so a task added here (no Todoist link
    // yet) landing first would leave todoist_id unreadable on every row after
    // it -- silently breaking the link between the two sides.
    function fillTasksModel() {
        tasksModel.clear();
        if (activeTabIndex < 0 || activeTabIndex >= todoData.length) return;
        let tasks = todoData[activeTabIndex].tasks || [];
        for (let i = 0; i < tasks.length; i++) {
            if (tasks[i].deleted) continue; // tombstone: awaiting push, not shown
            tasksModel.append({
                task: String(tasks[i].task || ""),
                completed: !!tasks[i].completed,
                todoist_id: String(tasks[i].todoist_id || ""),
                dirty: !!tasks[i].dirty
            });
        }
    }

    function updateModels() {
        tabsModel.clear();
        for (let i = 0; i < todoData.length; i++) {
            tabsModel.append({ name: todoData[i].name });
        }
        fillTasksModel();
    }

    function syncCurrentTasks() {
        if (!isLoaded || activeTabIndex < 0 || activeTabIndex >= todoData.length) return;

        let currentTasks = [];
        for (let i = 0; i < tasksModel.count; i++) {
            let row = tasksModel.get(i);
            let entry = { task: row.task, completed: row.completed, deleted: false };
            // Carrying todoist_id through is what keeps a task the *same* task.
            // Rebuilding rows as bare {task, completed} dropped it, so the next
            // sync read every task as newly created and pushed a duplicate of
            // the entire tab.
            if (row.todoist_id) entry.todoist_id = String(row.todoist_id);
            if (row.dirty) entry.dirty = true;
            currentTasks.push(entry);
        }

        // Tombstones are deliberately absent from the model -- they must not
        // appear in the list -- but they still have to reach the sync so the
        // deletion gets replayed to Todoist.
        let previous = todoData[activeTabIndex].tasks || [];
        for (let j = 0; j < previous.length; j++) {
            if (previous[j].deleted) currentTasks.push(previous[j]);
        }

        todoData[activeTabIndex].tasks = currentTasks;
    }

    // Record a deletion rather than just forgetting the task: with no tombstone
    // the next pull finds it still alive on Todoist and brings it right back.
    function tombstoneTask(row) {
        if (!row || String(row.todoist_id || "") === "") return;
        todoData[activeTabIndex].tasks.push({
            task: String(row.task || ""),
            completed: !!row.completed,
            todoist_id: String(row.todoist_id),
            deleted: true
        });
    }

    function saveTasks() {
        if (!isLoaded) return;
        syncCurrentTasks();

        let jsonStr = JSON.stringify(todoData);
        saveProcess.command = [
            "sh", "-c", 
            "mkdir -p $(dirname '" + todoRoot.todoPath + "') && echo \"$1\" > '" + todoRoot.todoPath + ".tmp' && mv '" + todoRoot.todoPath + ".tmp' '" + todoRoot.todoPath + "'",
            "--", 
            jsonStr
        ];
        saveProcess.running = true;
        pushTimer.restart();
    }

    function switchTab(index) {
        if (index === activeTabIndex) return;
        syncCurrentTasks();
        activeTabIndex = index;
        cancelEdit();
        
        fillTasksModel();
    }

    function addTab(name) {
        syncCurrentTasks();
        todoData.push({ name: name, tasks: [] });
        activeTabIndex = todoData.length - 1;
        updateModels();
        saveTasks();
    }

    ListModel { id: tabsModel }
    ListModel { id: tasksModel }

    Process { id: saveProcess; running: false }
    Process {
        id: loadProcess
        command: ["sh", "-c", "mkdir -p $(dirname '" + todoRoot.todoPath + "') && (cat '" + todoRoot.todoPath + "' 2>/dev/null || echo '[]')"]
        // Loaded when the tab is first shown rather than at shell start.
        running: false
        // StdioCollector, not SplitParser: SplitParser delivers one *line* per
        // callback, so as soon as the file was written pretty-printed (which
        // the sync script does, to keep it readable) the first callback got
        // "[", JSON.parse threw, and the list fell back to an empty "General"
        // tab -- the whole task list gone from the UI while intact on disk.
        stdout: StdioCollector {
            onStreamFinished: {
                let output = String(text).trim();
                let parsed = null;

                if (output !== "" && output !== "[]") {
                    try {
                        let content = JSON.parse(output);
                        if (Array.isArray(content) && content.length > 0) {
                            parsed = (content[0].tasks === undefined)
                                   ? [{ name: "General", tasks: content }] // legacy flat file
                                   : content;
                        }
                    } catch (e) {
                        // Keep what is already on screen rather than replacing
                        // it with an empty list: a truncated or corrupt file
                        // must never render as "you have no tasks".
                        if (todoRoot.isLoaded) return;
                    }
                }

                // Hold the user's place across a sync-triggered reload.
                let previousTab = (todoRoot.isLoaded && activeTabIndex >= 0
                                   && activeTabIndex < todoData.length)
                                ? todoData[activeTabIndex].name : "";

                todoData = parsed || [{ name: "General", tasks: [] }];

                activeTabIndex = 0;
                if (previousTab !== "") {
                    for (let i = 0; i < todoData.length; i++) {
                        if (todoData[i].name === previousTab) { activeTabIndex = i; break; }
                    }
                }

                todoRoot.isLoaded = true;
                updateModels();
            }
        }
    }

    // A local change is pushed shortly after it settles instead of waiting for
    // the next retry tick, so a task typed here reaches the phone quickly.
    Timer {
        id: pushTimer
        interval: 1500
        repeat: false
        onTriggered: if (TodoistService.hasToken) TodoistService.syncNow()
    }

    ColumnLayout {
        anchors.fill: parent; spacing: 10

        // Section Tabs
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                contentWidth: tabsRow.width
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                
                Row {
                    id: tabsRow
                    spacing: 8
                    Repeater {
                        model: tabsModel
                        delegate: Rectangle {
                            width: Math.max(50, tabText.implicitWidth + 20); height: 28; radius: Theme.bubbleRadiusPill
                            color: activeTabIndex === index ? Theme.accentColor : "transparent"
                            border.color: activeTabIndex === index ? Theme.accentColor : Theme.glassBorder
                            border.width: 1
                            
                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: model.name
                                color: activeTabIndex === index ? Colors.on_primary : Theme.subtext0
                                font.pixelSize: 11; font.weight: activeTabIndex === index ? Font.Bold : Font.Normal
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: switchTab(index)
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: 28; height: 28; radius: 14; color: "transparent"; border.color: Theme.accentColor; border.width: 1
                Text { anchors.centerIn: parent; text: "󰐕"; font.family: Theme.iconFont; color: Theme.accentColor; font.pixelSize: 12 }
                MouseArea { 
                    anchors.fill: parent
                    onClicked: { 
                        tabNameInput.text = ""; 
                        tabNameInputContainer.visible = !tabNameInputContainer.visible;
                        if (tabNameInputContainer.visible) tabNameInput.forceActiveFocus();
                    } 
                }
            }

            Rectangle {
                width: 28; height: 28; radius: 14; color: "transparent"; border.color: Theme.powerRed; border.width: 1
                visible: todoData.length > 1
                Text { anchors.centerIn: parent; text: "󰆴"; font.family: Theme.iconFont; color: Theme.powerRed; font.pixelSize: 12 }
                MouseArea { 
                    anchors.fill: parent
                    onClicked: {
                        if (todoData.length > 1) {
                            todoData.splice(activeTabIndex, 1);
                            if (activeTabIndex >= todoData.length) activeTabIndex = todoData.length - 1;
                            updateModels();
                            saveTasks();
                        }
                    }
                }
            }
        }

        // Tab Name Input
        Rectangle {
            id: tabNameInputContainer
            visible: false
            Layout.fillWidth: true; Layout.preferredHeight: 32
            radius: 8; color: Theme.glassBackground; border.color: Theme.mauve
            TextInput {
                id: tabNameInput
                anchors.fill: parent; anchors.margins: 6
                color: Theme.text; font.pixelSize: 13
                verticalAlignment: TextInput.AlignVCenter
                Text {
                    text: "Tab Name..."; color: Theme.surface2; font.pixelSize: 13
                    visible: !parent.text && !parent.activeFocus; verticalAlignment: Text.AlignVCenter
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                        if (text.trim() !== "") {
                            addTab(text.trim());
                            text = ""; tabNameInputContainer.visible = false;
                            event.accepted = true;
                        }
                    } else if (event.key === Qt.Key_Escape) {
                        tabNameInputContainer.visible = false;
                        event.accepted = true;
                    }
                }
            }
        }

        // Add Task Header
        RowLayout {
            Layout.fillWidth: true
            Text { 
                text: (activeTabIndex >= 0 && activeTabIndex < todoData.length) ? todoData[activeTabIndex].name : "Tasks"
                color: Theme.text; font.weight: Font.Bold; font.pixelSize: 14 
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                width: 24; height: 24; radius: 6; color: Theme.surface1
                Text { anchors.centerIn: parent; text: "󰐕"; font.family: Theme.iconFont; color: Theme.blue }
                MouseArea { 
                    anchors.fill: parent
                    onClicked: { 
                        todoRoot.cancelEdit();
                        inputField.text = ""; 
                        inputField.visible = true; 
                        inputField.forceActiveFocus(); 
                    } 
                }
            }
        }

        // Add/Edit Task Input
        TextArea {
            id: inputField
            visible: false
            Layout.fillWidth: true; Layout.preferredHeight: contentHeight + 20
            background: Rectangle { radius: 8; color: Theme.glassBackground; border.color: Theme.blue }
            color: Theme.text; padding: 10
            wrapMode: TextArea.Wrap
            verticalAlignment: TextInput.AlignTop
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                    if (event.modifiers & Qt.ShiftModifier) return;
                    if (text.trim() !== "") {
                        if (todoRoot.editingIndex !== -1) {
                            tasksModel.setProperty(todoRoot.editingIndex, "task", text.trim());
                            // Flag the edit so the sync pushes it; without this
                            // "Todoist wins" quietly reverts it on the next pull.
                            tasksModel.setProperty(todoRoot.editingIndex, "dirty", true);
                            todoRoot.editingIndex = -1;
                        } else {
                            tasksModel.append({ "task": text.trim(), "completed": false,
                                                "todoist_id": "", "dirty": false });
                        }
                        inputField.text = ""; inputField.visible = false; 
                        saveTasks();
                        event.accepted = true;
                    }
                } else if (event.key === Qt.Key_Escape) {
                    todoRoot.cancelEdit();
                    event.accepted = true;
                }
            }
        }

        // --- TODOIST CONNECT PROMPT ---
        //
        // Sits above the task list rather than inside tasksModel: it is not a
        // task, and putting it in the model would make it draggable, savable,
        // and part of the reorder indices. It disappears the moment a token is
        // stored.
        Rectangle {
            id: todoistPrompt
            Layout.fillWidth: true
            Layout.preferredHeight: keyInputRow.visible ? Theme.scaled(112) : Theme.scaled(44)
            Layout.bottomMargin: Theme.scaled(8)
            visible: !TodoistService.hasToken

            radius: Theme.bubbleRadiusMedium
            color: Theme.menuBackground2
            border.color: Theme.glassBorder
            border.width: 1

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.scaled(8)
                spacing: Theme.scaled(6)

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(8)

                    Text {
                        text: "󰌆"
                        font.family: Theme.iconFont
                        font.pixelSize: Theme.scaled(15)
                        color: Theme.accentColor
                        Layout.leftMargin: Theme.scaled(4)
                    }

                    Text {
                        Layout.fillWidth: true
                        text: TodoistService.lastError !== ""
                              ? TodoistService.lastError
                              : "Add API key for Todoist to connect"
                        color: TodoistService.lastError !== "" ? Theme.powerRed : Theme.text
                        font.pixelSize: Theme.scaled(11)
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    // The key button: opens the inline field.
                    Rectangle {
                        implicitWidth: Theme.scaled(28)
                        implicitHeight: Theme.scaled(28)
                        radius: Theme.scaled(8)
                        color: keyBtnMouse.containsMouse
                               ? Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.25)
                               : Qt.rgba(Theme.accentColor.r, Theme.accentColor.g, Theme.accentColor.b, 0.15)

                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                        Text {
                            anchors.centerIn: parent
                            text: "󰌆"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(13)
                            color: Theme.accentColor
                        }

                        MouseArea {
                            id: keyBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                keyInputRow.visible = !keyInputRow.visible;
                                if (keyInputRow.visible) {
                                    tokenInput.text = "";
                                    tokenInput.forceActiveFocus();
                                }
                            }
                        }
                    }
                }

                // Inline entry. Hidden until the key button is pressed.
                RowLayout {
                    id: keyInputRow
                    Layout.fillWidth: true
                    visible: false
                    spacing: Theme.scaled(6)

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: Theme.scaled(28)
                        radius: Theme.scaled(8)
                        color: Qt.rgba(0, 0, 0, 0.35)
                        border.color: Theme.glassBorder
                        border.width: 1

                        TextInput {
                            id: tokenInput
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            verticalAlignment: TextInput.AlignVCenter
                            font.pixelSize: Theme.scaled(11)
                            color: Theme.text
                            selectByMouse: true
                            clip: true
                            // A credential on a desktop others can see.
                            echoMode: TextInput.Password

                            onAccepted: todoistPrompt.submitToken()
                            Keys.onEscapePressed: keyInputRow.visible = false

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tokenInput.text.length === 0
                                text: "Paste your Todoist API token"
                                color: Theme.subtext0
                                font.pixelSize: Theme.scaled(11)
                            }
                        }
                    }

                    Rectangle {
                        implicitWidth: Theme.scaled(28)
                        implicitHeight: Theme.scaled(28)
                        radius: Theme.scaled(8)
                        color: tokenInput.text.length > 0 ? Theme.accentColor : Theme.surface1
                        Behavior on color { ColorAnimation { duration: Theme.animInstant } }

                        Text {
                            anchors.centerIn: parent
                            text: TodoistService.verifying ? "󰅖" : "󰄬"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(13)
                            color: tokenInput.text.length > 0 ? Colors.on_primary : Theme.subtext0

                            RotationAnimator on rotation {
                                running: TodoistService.verifying
                                from: 0; to: 360; duration: 900; loops: Animation.Infinite
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: tokenInput.text.length > 0
                            cursorShape: Qt.PointingHandCursor
                            onClicked: todoistPrompt.submitToken()
                        }
                    }
                }

                // Where to actually get the token. Clicking opens the page
                // directly rather than making the user hunt through settings.
                // A plain Item the ColumnLayout manages, so the MouseArea can
                // anchor-fill it. Anchoring directly inside a layout is
                // undefined behaviour -- the layout owns the geometry.
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(16)
                    Layout.leftMargin: Theme.scaled(4)
                    visible: keyInputRow.visible

                    RowLayout {
                        anchors.fill: parent
                        spacing: Theme.scaled(5)

                        Text {
                            text: "󰏌"
                            font.family: Theme.iconFont
                            font.pixelSize: Theme.scaled(10)
                            color: linkMouse.containsMouse ? Theme.accentColor : Theme.subtext0
                            Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        }

                        Text {
                            text: "Get a token: Todoist -> Settings -> Integrations -> Developer"
                            font.pixelSize: Theme.scaled(10)
                            font.underline: linkMouse.containsMouse
                            color: linkMouse.containsMouse ? Theme.accentColor : Theme.subtext0
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Behavior on color { ColorAnimation { duration: Theme.animInstant } }
                        }
                    }

                    MouseArea {
                        id: linkMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: todoistPrompt.openTokenPage()
                    }
                }
            }

            readonly property string tokenPageUrl:
                "https://app.todoist.com/app/settings/integrations/developer"

            Process {
                id: openTokenPageProc
                command: ["xdg-open", todoistPrompt.tokenPageUrl]
            }

            function openTokenPage() {
                openTokenPageProc.running = false;
                openTokenPageProc.running = true;
            }

            function submitToken() {
                if (tokenInput.text.trim() === "") return;
                TodoistService.saveToken(tokenInput.text);
                tokenInput.text = "";
                keyInputRow.visible = false;
            }
        }

        // Tasks List
        ListView {
            id: taskListView
            Layout.fillWidth: true; Layout.fillHeight: true
            model: tasksModel; clip: true; spacing: 8

            FastWheel {}
            
            displaced: Transition {
                NumberAnimation { properties: "y"; duration: Theme.animFast; easing.type: Easing.OutCubic }
            }

            delegate: Item {
                id: delegateRoot
                width: taskListView.width; height: visible ? contentCol.height + 16 : 0
                z: dragArea.held ? 100 : 1
                visible: todoRoot.editingIndex !== index

                Rectangle {
                    id: visualContent
                    anchors.fill: parent; radius: Theme.bubbleRadiusMedium
                    color: (todoRoot.selectedIndex === index) ? Theme.cardBackgroundHover : Theme.cardBackground
                    border.color: (todoRoot.selectedIndex === index) ? Theme.accentColor : Theme.glassBorder
                    border.width: 1
                    y: 0
                    
                    ColumnLayout {
                        id: contentCol
                        width: parent.width - 16; anchors.margins: 8; x: 8; y: 8
                        RowLayout {
                            spacing: 8
                            
                            // Drag Handle
                            Text {
                                text: ": :"; font.family: Theme.iconFont; color: Theme.surface2; font.pixelSize: 16
                                MouseArea {
                                    id: dragArea
                                    anchors.fill: parent
                                    property bool held: false
                                    preventStealing: true
                                    
                                    onPressed: held = true
                                    onReleased: {
                                        held = false;
                                        visualContent.y = 0;
                                        saveTasks();
                                    }
                                    
                                    onPositionChanged: (mouse) => {
                                        if (held) {
                                            // Get point relative to the content area (scrollable area)
                                            let pointInContent = taskListView.contentItem.mapFromItem(dragArea, mouse.x, mouse.y);
                                            let itemHeight = delegateRoot.height + taskListView.spacing;
                                            let targetIndex = Math.floor(pointInContent.y / itemHeight);
                                            
                                            // Ensure within bounds
                                            targetIndex = Math.max(0, Math.min(targetIndex, tasksModel.count - 1));
                                            
                                            if (targetIndex !== index) {
                                                tasksModel.move(index, targetIndex, 1);
                                            }
                                            
                                            // Follow mouse feedback (relative to stable delegate root)
                                            let pointInDelegate = delegateRoot.mapFromItem(dragArea, mouse.x, mouse.y);
                                            visualContent.y = pointInDelegate.y - height / 2;
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                width: 22; height: 22; radius: 6; color: model.completed ? Theme.accentColor : Qt.rgba(255, 255, 255, 0.12); border.width: 0
                                Text { anchors.centerIn: parent; text: model.completed ? "✓" : ""; color: Colors.on_primary; font.pixelSize: 14; font.bold: true }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        tasksModel.setProperty(index, "completed", !model.completed);
                                        tasksModel.setProperty(index, "dirty", true);
                                        saveTasks();
                                    }
                                }
                            }

                            Text { 
                                text: model.task; Layout.fillWidth: true
                                color: model.completed ? Theme.surface2 : Theme.text
                                font.strikeout: model.completed
                                wrapMode: Text.WordWrap
                            }

                            Text { 
                                text: "󰏫"; font.family: Theme.iconFont; color: Theme.accentColor; font.pixelSize: Theme.scaled(18)
                                MouseArea { 
                                    anchors.fill: parent; 
                                    onClicked: { 
                                        todoRoot.editingIndex = index;
                                        inputField.text = model.task;
                                        inputField.visible = true; 
                                        inputField.forceActiveFocus();
                                        inputField.cursorPosition = inputField.text.length;
                                    } 
                                }
                            }
                            
                            Text { 
                                text: "󰆴"; font.family: Theme.iconFont; color: Theme.powerRed; font.pixelSize: Theme.scaled(18)
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        todoRoot.tombstoneTask(tasksModel.get(index));
                                        tasksModel.remove(index);
                                        saveTasks();
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
