import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Windows as Win
import "../Settings"

pragma Singleton

Item {
    id: service

    readonly property string storagePath: PathSettings.stateDir + "/productivity.json"
    readonly property string _legacyStoragePath: PathSettings.shellDir + "/productivity.json"
    
    // --- Reactive Properties (Source of Truth) ---
    property int duration: 0
    property int remaining: 0
    property bool running: false
    property bool isBeeping: false
    property var tasks: []

    signal refreshData()

    Component.onCompleted: load()

    function load() {
        loadProcess.running = true
    }

    function initData(parsed) {
        if (parsed) {
            tasks = parsed.tasks || [];
            if (parsed.timer) {
                duration = Math.max(0, parsed.timer.duration || 0);
                remaining = Math.max(0, parsed.timer.remaining || 0);
                running = (parsed.timer.running === true && remaining > 0);
            }
        }
        
        isBeeping = false;
        refreshData();
    }

    function save() {
        let data = {
            tasks: tasks,
            timer: {
                duration: duration,
                remaining: remaining,
                running: running
            }
        };
        
        Win.FsUtils.makePath(storagePath.slice(0, storagePath.lastIndexOf("/"))); // Windows: no python
        winStore.setText(JSON.stringify(data));
    }

    // --- Timer Actions ---
    function setDuration(secs) {
        stopBeeping();
        running = false;
        duration = Math.max(0, secs);
        remaining = duration;
        save();
    }

    function adjustDuration(delta) {
        if (running || isBeeping) return;
        
        let next = Math.max(0, duration + delta);
        if (next === duration) return;
        
        duration = next;
        remaining = duration;
        save();
    }

    function toggleTimer() {
        if (isBeeping) {
            dismissAlarm();
            return;
        }
        if (duration <= 0) return;
        
        running = !running;
        if (remaining <= 0) remaining = duration;
        save();
    }

    function resetTimer() {
        running = false;
        remaining = 0;
        duration = 0;
        stopBeeping();
        save();
    }

    function dismissAlarm() {
        stopBeeping();
        running = false;
        remaining = 0;
        duration = 0;
        save();
    }

    function stopBeeping() {
        isBeeping = false;
        beepProcess.running = false;
        shellExec.command = ["pkill", "-f", "ffplay|mpv|paplay|pw-play|canberra-gtk-play"];
        shellExec.running = true;
    }

    // --- Task Management ---
    function addTask(task) {
        tasks.push({ task: task, completed: false, id: Date.now() });
        save();
    }

    function removeTask(id) {
        tasks = tasks.filter(t => t.id !== id);
        save();
    }

    // --- Background Countdown ---
    Timer {
        interval: 1000
        running: service.running
        repeat: true
        onTriggered: {
            if (service.running) {
                if (service.remaining > 0) {
                    service.remaining--;
                } else {
                    service.running = false;
                    service.isBeeping = true;
                }
                
                if (service.remaining % 10 === 0 || service.remaining === 0) {
                    service.save();
                }
            }
        }
    }

    // --- Sound Loop ---
    Timer {
        id: alarmLoop
        interval: 2000
        running: service.isBeeping
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            beepProcess.running = false;
            beepProcess.running = true;
        }
    }

    // Priority sound player: ffplay -> mpv -> paplay -> pw-play / canberra
    Process { 
        id: beepProcess
        command: ["sh", "-c", 
            "SOUND_FILE='/run/current-system/sw/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga'; " +
            "[ ! -f \"$SOUND_FILE\" ] && SOUND_FILE='/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga'; " +
            "ffplay -nodisp -autoexit \"$SOUND_FILE\" 2>/dev/null || " +
            "mpv --no-video --no-terminal \"$SOUND_FILE\" 2>/dev/null || " +
            "paplay \"$SOUND_FILE\" 2>/dev/null || " +
            "pw-play \"$SOUND_FILE\" 2>/dev/null || " +
            "canberra-gtk-play -i alarm-clock-elapsed 2>/dev/null"
        ] 
    }
    Process { id: shellExec }

    Process { 
        id: loadProcess; 
        command: ["cmd.exe", "/d", "/c", "type", service.storagePath.split("/").join("\\")] // Windows
        stdout: StdioCollector {
            onStreamFinished: {
                try { 
                    let parsed = JSON.parse(text);
                    initData(parsed);
                } catch(e) {
                    initData(null);
                }
            }
        }
        onExited: (exitCode) => {
            if (exitCode !== 0) initData(null);
        }
    }
    Process { id: saveProcess }
    FileView { id: winStore; path: service.storagePath; blockWrites: false; atomicWrites: true; printErrors: false } // Windows
}
