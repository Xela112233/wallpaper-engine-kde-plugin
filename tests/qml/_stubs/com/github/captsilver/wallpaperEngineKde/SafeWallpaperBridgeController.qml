// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/SafeWallpaperBridgeController.hpp + .cpp
// Last contract review: 2026-11-06

import QtQuick
// Test stub for the C++ SafeWallpaperBridgeController — the QML-only object
// that forwards setLoaded/push* onto a SafeWallpaperBridge sibling. In the
// real plugin these are Q_INVOKABLE (QML can call them, QWebChannel never
// sees this type at all); a plain QML stub can't reproduce that C++
// invokability distinction, so the forwarding is what this stub actually
// pins — production QML must call through here, not assign to the bridge's
// (read-only) properties directly.
QtObject {
    id: controller

    property var bridge: null

    function setLoaded(v) {
        if (bridge) bridge.setLoaded(v);
    }
    function pushUserProperties(m) {
        if (bridge) bridge.pushUserProperties(m);
    }
    function pushGeneralProperties(m) {
        if (bridge) bridge.pushGeneralProperties(m);
    }
    // Media integration: the C++ builds Wallpaper Engine's event shapes
    // (mediaPropertiesEvent, mediaTimelineEvent); the cover's PNG encoding
    // has no QML counterpart, so the stub's thumbnail records what it was
    // handed instead of a data: URL.
    function pushMediaStatus(v)   { if (bridge) bridge.setMediaStatus(v); }
    function pushMediaPlayback(v) { if (bridge) bridge.setMediaPlayback(v); }
    function pushMediaProperties(title, artist, albumTitle, albumArtist, genres) {
        if (bridge) bridge.setMediaProperties({
            title: title, artist: artist, subTitle: "", albumTitle: albumTitle,
            albumArtist: albumArtist, genres: genres, contentType: "music" });
    }
    function pushMediaThumbnail(hasThumbnail, colors, image) {
        if (bridge) bridge.setMediaThumbnail(hasThumbnail
            ? { thumbnail: "stub:" + JSON.stringify(image), colors: colors } : {});
    }
    function pushMediaTimeline(position, duration) {
        if (bridge) bridge.setMediaTimeline({ position: position, duration: duration });
    }
    function clearMedia()   { if (bridge) bridge.clearMedia(); }
    function pushPaused(v)  { if (bridge) bridge.setPaused(v); }
}
