.pragma library
.import "../MaterialWallpaper.js" as MaterialWallpaper

var SPEC = {
    isLightMode: {
        def: false
    },
    materialWallpapers: {
        def: {},
        coerce: MaterialWallpaper.normalizeStore
    },
    materialWallpaperProfiles: {
        def: {},
        coerce: MaterialWallpaper.normalizeProfiles
    },
    wallpaperPath: {
        def: ""
    },
    perMonitorWallpaper: {
        def: false
    },
    perModeWallpaper: {
        def: false
    },
    monitorWallpapers: {
        def: {}
    },
    monitorWallpaperFillModes: {
        def: {}
    },
    weatherLocation: {
        def: "New York, NY"
    },
    weatherCoordinates: {
        def: "40.7128,-74.0060"
    },
    desktopWidgetInstancePositions: {
        def: {}
    },
    lockScreenAutoPositions: {
        def: {}
    },
    greeterAutoPositions: {
        def: {}
    }
};
