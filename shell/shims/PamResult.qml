import QtQuick

// Windows stand-in for Quickshell.Services.Pam's PamResult values (see PamContext.qml): PamResult.Success ...
QtObject {
    enum Result {
        Success,
        Failed,
        Error,
        MaxTries
    }
}
