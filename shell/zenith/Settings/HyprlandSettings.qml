pragma Singleton
import "."
import QtQuick
import Quickshell.Io

Item {
    id: hyprlandSettings
    
    property int gapsIn: 2
    property int gapsOut: 6
    property int gapsWorkspaces: 20
    property int borderSize: 2
    property int rounding: 13
    property int shadowRange: 12
    property real blurVibrancy: 0.5
    property int barMargins: 10
    
    // Logic to apply changes in real-time
    onGapsInChanged: apply("general:gaps_in", gapsIn)
    onGapsOutChanged: apply("general:gaps_out", gapsOut)
    onGapsWorkspacesChanged: apply("general:gaps_workspaces", gapsWorkspaces)
    onBorderSizeChanged: apply("general:border_size", borderSize)
    onRoundingChanged: apply("decoration:rounding", rounding)
    onShadowRangeChanged: apply("decoration:shadow:range", shadowRange)
    onBlurVibrancyChanged: apply("decoration:blur:vibrancy", blurVibrancy)
    
    function apply(key, value) {
        exec.command = ["hyprctl", "keyword", key, value.toString()];
        exec.running = true;
    }
    
    Process { id: exec }
}
