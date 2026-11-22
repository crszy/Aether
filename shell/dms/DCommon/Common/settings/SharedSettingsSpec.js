.pragma library
.import "./SpecUtil.js" as Util
.import "../Shape.js" as Shape

function lockWidgetDefaults() {
    const common = {
        displayPreferences: ["all"],
        syncPositionAcrossScreens: true
    };
    const entry = (id, widgetType, enabled, config) => ({
                id: id,
                widgetType: widgetType,
                enabled: enabled,
                config: Object.assign({}, common, config)
            });
    const tint = {
        colorMode: "default",
        customColor: "#ffffff"
    };
    return [entry("lock_clock", "desktopClock", true, Object.assign({
            style: "expressive",
            transparency: 0,
            weight: 0,
            twoTone: true,
            showDate: false,
            showDigitalSeconds: false,
            showAnalogNumbers: false,
            showAnalogSeconds: true,
            autoPosition: true
        }, tint)), entry("lock_date", "lockDate", false, Object.assign({
            format: "short"
        }, tint)), entry("lock_auth", "lockAuth", true, {
            style: "expressive",
            profileVisibility: "always",
            passwordVisibility: "always"
        }), entry("lock_notifications", "lockNotifications", true, {
            mode: 2
        }), entry("lock_status", "lockStatus", true, {
            showMediaPlayer: true,
            showWeather: true,
            background: false
        }), entry("lock_power", "lockPower", false, {
            shape: "round"
        })];
}

var GREETER_WIDGET_TYPES = ["desktopClock", "lockDate", "lockAuth", "lockStatus", "lockPower", "greeterSession"];

function greeterSessionDefault() {
    return {
        id: "greeter_session",
        widgetType: "greeterSession",
        enabled: true,
        config: {
            displayPreferences: ["all"],
            syncPositionAcrossScreens: true
        }
    };
}

// The greeter shows the lock widgets it can render plus its own session picker.
function greeterWidgetsFromLock(lockInstances, previous) {
    const mirrored = (lockInstances || []).filter(inst => GREETER_WIDGET_TYPES.includes(inst.widgetType)).map(inst => JSON.parse(JSON.stringify(inst)));
    const own = (previous || []).filter(inst => inst.widgetType === "greeterSession").map(inst => JSON.parse(JSON.stringify(inst)));
    return mirrored.concat(own.length ? own : [greeterSessionDefault()]);
}

function greeterWidgetDefaults() {
    return greeterWidgetsFromLock(lockWidgetDefaults(), []);
}

var SPEC = {
    lockScreenWidgetInstances: {
        def: lockWidgetDefaults()
    },
    greeterWidgetInstances: {
        def: greeterWidgetDefaults()
    },
    displayNameMode: {
        def: "system"
    },
    currentThemeName: {
        def: "purple",
        onChange: "applyStoredTheme"
    },
    customThemeFile: {
        def: ""
    },
    registryThemeVariants: {
        def: {}
    },
    radiusStrength: {
        def: 50,
        coerce: Shape.normalizeStrength,
        onChange: "updateCompositorLayout"
    },
    radiusMode: {
        def: "scale",
        onChange: "updateCompositorLayout"
    },
    fixedRadius: {
        def: 16,
        coerce: Shape.normalizeFixedRadius,
        onChange: "updateCompositorLayout"
    },
    clockFormat: {
        def: "auto"
    },
    showSeconds: {
        def: false
    },
    padHours12Hour: {
        def: false
    },
    showAmPm: {
        def: true
    },
    useFahrenheit: {
        def: false
    },
    animationDuration: {
        def: 250
    },
    enableRippleEffects: {
        def: true
    },
    popoutElevationEnabled: {
        def: true
    },
    tooltipSurfaceColor: {
        def: "default"
    },
    tooltipSurfaceCustomColor: {
        def: "#6750A4"
    },
    scrollbarsEnabled: {
        def: true
    },
    blurBorderEnabled: {
        def: false
    },
    blurBorderColor: {
        def: "outline"
    },
    blurBorderCustomColor: {
        def: "#ffffff"
    },
    blurBorderOpacity: {
        def: 0.35,
        coerce: Util.percentToUnit
    },
    wallpaperFillMode: {
        def: "Fill"
    },
    wallpaperBackgroundColorMode: {
        def: "black"
    },
    wallpaperBackgroundCustomColor: {
        def: "#000000"
    },
    lockDateFormat: {
        def: ""
    },
    greeterRememberLastSession: {
        def: true
    },
    greeterRememberLastUser: {
        def: true
    },
    greeterEnableFprint: {
        def: false,
        onChange: "markGreeterSyncPending"
    },
    greeterEnableU2f: {
        def: false,
        onChange: "markGreeterSyncPending"
    },
    useAutoLocation: {
        def: false
    },
    weatherEnabled: {
        def: true
    },
    fontFamily: {
        def: "Google Sans Flex"
    },
    monoFontFamily: {
        def: "Fira Code"
    },
    displayFontFamily: {
        def: "DM Serif Display"
    },
    fontWeight: {
        def: 400
    },
    fontScale: {
        def: 1.0
    },
    textRenderType: {
        def: 0
    },
    textRenderQuality: {
        def: 0
    },
    lockScreenShowPowerActions: {
        def: false
    },
    lockScreenShowProfileImage: {
        def: true
    },
    lockScreenShowWeather: {
        def: true
    },
    lockScreenWallpaperPath: {
        def: ""
    },
    lockScreenWallpaperFillMode: {
        def: ""
    },
    lockScreenFontFamily: {
        def: ""
    },
    powerActionConfirm: {
        def: true
    },
    powerActionHoldDuration: {
        def: 0.5
    },
    powerMenuActions: {
        def: ["reboot", "logout", "poweroff", "lock", "suspend", "restart"]
    },
    powerMenuDefaultAction: {
        def: "logout"
    },
    powerMenuGridLayout: {
        def: false
    }
};
