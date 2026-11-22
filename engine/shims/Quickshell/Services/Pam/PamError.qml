pragma Singleton
import QtQml

QtObject {
    enum Enum {
        StartFailed = 1,
        TryAuthFailed = 2,
        InternalError = 3
    }

    function toString(value) {
        switch (value) {
        case PamError.StartFailed: return "StartFailed";
        case PamError.TryAuthFailed: return "TryAuthFailed";
        default: return "InternalError";
        }
    }
}
