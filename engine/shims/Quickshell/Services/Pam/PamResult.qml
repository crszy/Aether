pragma Singleton
import QtQml

QtObject {
    enum Enum {
        Success = 0,
        Failed = 1,
        Error = 2,
        MaxTries = 3
    }

    function toString(value) {
        switch (value) {
        case PamResult.Success: return "Success";
        case PamResult.Failed: return "Failed";
        case PamResult.MaxTries: return "MaxTries";
        default: return "Error";
        }
    }
}
