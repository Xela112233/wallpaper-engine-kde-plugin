// Test stand-in for Plasma's `wallpaper` context property. main.qml resolves its
// unqualified `wallpaper.*` references to the `id: wallpaper` ScreenRig gives this
// instance, via the Qt.createComponent creation-context chain (same mechanism
// production uses for Scene.qml -> `background`).
//
// `configuration` is a `var` (plain JS object), NOT a QtObject: QML forbids
// QML-declared property names beginning with an uppercase letter, but the real
// Plasma config keys (matching plugin/contents/config/main.xml) are PascalCase.
// A JS object permits uppercase keys. Reactivity is preserved at the whole-object
// level: reassigning `configuration` re-fires every `wallpaper.configuration.X`
// binding in main.qml (source->applySource, PerOptChanged, displayMode, ...). Use
// ScreenRig.setConfig(partial) to mutate (it reassigns); a direct single-key write
// is read-correct but does NOT notify. Defaults are "playable": MouseInput false
// (avoids the 2s hookTimer tree-dump), a known BackgroundColor, scene DisplayMode.
import QtQuick
Item {
    id: root

    // `wallpaper.parent.screenGeometry` is read by WindowModel; ScreenRig (this
    // item's visual parent) supplies it. `accentColor` makes
    // hasOwnProperty('accentColor') true so main.qml's first-frame handler path is
    // exercised; QML exposes the auto `accentColorChanged()` as invokable.
    property color accentColor: "transparent"

    property var configuration: ({
        "SteamLibraryPath":    "",
        "AutoPickSource":      "",       // we-manager's mark of an automatic change (stop-action patch)
        "WallpaperWorkShopId": "",
        "WallpaperSource":     "",
        "BackgroundColor":     "#0a0a0a",
        "AccentFromSchemeColor": true,   // matches main.xml default
        "DisplayMode":         0,        // Common.DisplayMode.Aspect
        "PauseMode":           0,
        "PauseFilterByScreen": false,
        "FocusAction":         2,        // Pause (main.xml default)
        "MaximizedAction":     2,
        "FullscreenAction":    2,
        "PauseAction":         0,        // Pause on battery
        "StopScope":           0,        // This screen
        "PauseOnBatPower":     false,
        "PauseBatPercent":     0,
        "PauseDebugLogging":   false,
        "ScreenSaverPolicy":   1,        // Pause (default)
        "SortMode":            0,
        "VideoBackend":        0,
        "MuteAudio":           false,
        "RandomizeWallpaper":  false,
        "NoRandomWhilePaused": false,
        "UserPaused":          false,
        "MpvStats":            false,
        "MpvPowerSaving":      false,
        "MpvInterpolation":    false,
        "VerboseLogging":      false,
        "MouseInput":          false,
        "KeyboardInput":       false,    // matches main.xml default (we-manager input patch)
        "AnimatedPreview":     false,
        "Fps":                 30,
        "RenderScale":         100,
        "PresentMode":         0,        // Auto (matches main.xml default)
        "SwitchTimer":         15,
        "ResumeTime":          1,
        "Volume":              50,
        "Speed":               1.0,
        "FilterStr":           "",
        "WallpaperLanguage":   "",
        "PerOptChanged":       0,
        "CustomConf":          "",
        "HdrOutput":           false,
        "SceneOptions":        true,     // marker key, never read
        "PostProcessing":      "",
        "SystemAudioCapture":  true,      // matches main.xml default
        "MediaIntegration":    true,      // matches main.xml default
        "VideoFolderPath":     "",
        "PostProcessingLevels": true,    // marker key, never read
        "ActivePlaylistId":    "",
        "CurrentItemIndex":    0,
        "PlaylistsReloadSeq":  0,
        "CacheQuotaMB":        500,
        "PlaylistNotifyOnAdvance": false,
        "MsaaMode":            0,
        "ProcessPriority":     0
    })
}
