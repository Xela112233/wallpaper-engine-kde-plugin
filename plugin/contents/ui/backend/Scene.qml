import QtQuick 2.5
import com.github.captsilver.wallpaperEngineKde 1.2
import ".."
import "../js/layout.mjs" as Layout

Item{
    id: sceneItem
    // Set once this backend has put a frame on screen; main.qml keeps the
    // previous backend visible until then.
    property bool frameShown: false
    // What the scene holds after its first frame (SceneViewer.vramMiB), 0
    // before: main.qml adds an outgoing scene's to the VRAM guard's estimate
    // for the length of a transition (transitions patch).
    property int vramMiB: 0
    anchors.fill: parent
    property alias source: player.source
    property string assets: "assets"
    property int displayMode: background.displayMode
    property string userPropsJson: background.userPropsJson
    // A mute fades out before the scene's sound is muted, and a pause before
    // the renderer pauses (we-manager mute patch: Common.createVolumeFade).
    // Made in Component.onCompleted, not in a binding here: the fade reads its
    // own properties as it starts, and inside a binding that would make it
    // again at every change of the mute.
    property var volumeFade: null
    // Both drive the load watchdog below.
    property bool _paused: false
    property bool _firstFrameSeen: false

    // displayMode no longer drives fillMode imperatively: the player binds its
    // size + fillMode to the pure Layout helpers below, so both follow
    // displayMode and nativeAspectRatio reactively (the old onDisplayModeChanged
    // handler + a 50ms displayModeFixTimer that fought a separate item-letterbox
    // are gone).

    // MPRIS media-control + metadata bridge. SceneScript JS in scene
    // wallpapers can subscribe to playback state, properties (title /
    // artist / album / genres / duration), timeline (position), and
    // thumbnail color extraction via mediaPlaybackChanged etc. on the
    // player object. SceneScript can also dispatch transport controls
    // via engine.openUserShortcut("bplaypause"/"bnext"/...).
    //
    // Web wallpapers get the same data through QtWebView.qml's own monitor
    // and the bridge's media properties (we-manager media integration).
    // Video wallpapers have no script surface to dispatch media keys from.
    //
    // Behind the MediaIntegration setting (we-manager). MprisMonitor has no
    // way back from engage(), so switching it off destroys the monitor, and
    // the scene script hears mediaStatusChanged(false), as when the player
    // quits. Its media shortcuts (engine.openUserShortcut) then do nothing.
    Loader {
        id: mprisLoader
        active: background.mediaIntegration
        onActiveChanged: if (!active) player.mediaStatusChanged(false)
        sourceComponent: MprisMonitor {
            id: mprisMonitor
            // The players to leave out (MediaBlocklist), set before engage()
            // and followed live: an ignored player is dropped and the next
            // one taken (we-manager media-blocklist patch).  Through
            // Common.setIfSupported: a library from before that patch has no
            // ignoredPlayers, and naming it here would fail this file.
            // Engage the D-Bus watch + position poll so signals fire for
            // scene-script subscribers. Video backends skip the cost
            // automatically (they never instantiate MprisMonitor).
            Component.onCompleted: {
                Common.setIfSupported(mprisMonitor, "MprisMonitor", "ignoredPlayers",
                                      Qt.binding(function() { return background.mediaBlocklist || ""; }));
                engage();
            }
            onPlaybackStateChanged: function(state) {
                player.mediaPlaybackChanged(state);
            }
            onPropertiesChanged: function(title, artist, albumTitle, albumArtist, genres, duration) {
                player.mediaPropertiesChanged(title, artist, albumTitle, albumArtist, genres, duration);
            }
            onThumbnailChanged: function(hasThumbnail, colors, image) {
                player.mediaThumbnailChanged(hasThumbnail, colors, image);
            }
            onTimelineChanged: function(position, duration, state) {
                player.mediaTimelineChanged(position, duration, state);
            }
            onMediaAvailableChanged: function(available) {
                player.mediaStatusChanged(available);
            }
        }
    }

    SceneViewer {
        id: player
        // Keep-Aspect-Ratio: size the renderer to the wallpaper's native aspect
        // and centre it, so the letterbox region is the parent `background`
        // Rectangle (BackgroundColor) showing through.  fillMode is STRETCH once
        // the native aspect is known (the item is already native-aspect, so the
        // renderer fills it exactly with NO opaque padding); the previous code
        // left it ASPECTFIT, which painted black bars whenever the item stayed
        // full-size.  Size + fillMode are bindings over the pure Layout helpers
        // (plugin/contents/ui/js/layout.mjs) so they are unit-testable and follow
        // displayMode/nativeAspectRatio reactively.
        anchors.centerIn: parent
        readonly property bool isAspect: sceneItem.displayMode === Common.DisplayMode.Aspect
        readonly property bool isCrop:   sceneItem.displayMode === Common.DisplayMode.Crop
        readonly property var  _box: Layout.letterboxSize(isAspect, nativeAspectRatio,
                                                          parent.width, parent.height)
        width:  _box.width
        height: _box.height
        fillMode: Layout.fillModeFor(isAspect, isCrop, nativeAspectRatio,
                                     { STRETCH: SceneViewer.STRETCH,
                                       ASPECTFIT: SceneViewer.ASPECTFIT,
                                       ASPECTCROP: SceneViewer.ASPECTCROP })

        // Diagnostic (kept per debug-logging policy): one-shot per load, lets a
        // real multi-monitor install confirm in journalctl that nativeAspectRatio
        // reaches QML (>0) on the ultrawide.  Grep: "WEK] letterbox".
        onNativeAspectRatioChanged: if (nativeAspectRatio > 0)
            console.log("[WEK] letterbox: nativeAspect=" + nativeAspectRatio
                + " parent=" + parent.width + "x" + parent.height
                + " item=" + width + "x" + height + " fillMode=" + fillMode);

        fps: background.fps
        // Swapchain present-mode policy + display refresh (millihertz).
        // Forwarded raw to SceneViewer's Q_PROPERTYs, which marshal them into
        // SceneWallpaper.  The refresh feeds frame pacing; the policy only
        // matters on surface builds (standalone viewer).
        presentMode: background.presentMode
        muted: sceneItem.volumeFade ? sceneItem.volumeFade.state.hardMuted : background.mute
        speed: background.speed
        assets: sceneItem.assets
        userProperties: sceneItem.userPropsJson
        hdrOutput: background.hdrOutput
        postprocessingOverride: background.postProcessing
        msaaMode: background.msaaMode
        cameraParallax: background.cameraParallax
        pixelArt: background.pixelArt
        renderScale: background.renderScale
        systemAudioCapture: background.systemAudioCapture
        // SceneScript's applyGeneralSettings({language}) (we-manager's language patch)
        language: background.wallpaperLanguage
        Component.onCompleted: {
            player.setAcceptMouse(true);
            player.setAcceptHover(true);
            // Set here, not declared above: a library from before the
            // priority patch has no processPriority (Common.setIfSupported).
            Common.setIfSupported(player, "SceneViewer", "processPriority",
                                  Qt.binding(function() { return background.processPriority; }));
            // Hardware video decoding for the scene's video layers
            // (mpv-options patch, MpvHwdec); a change reloads the scene.
            // Before the first frame: the scene loads only once the renderer
            // is up, so the first load already uses it.
            Common.setIfSupported(player, "SceneViewer", "videoHwdec",
                                  Qt.binding(function() { return background.mpvHwdec; }));
            // Texture resolution (texture-quality patch); a change reloads
            // the scene.  Set before the renderer is up, so the first load
            // already uses it.
            Common.setIfSupported(player, "SceneViewer", "sceneQuality",
                                  Qt.binding(function() { return { textures: background.textureQuality }; }));
            // Audio output (mute patch, AudioOutput): off, the scene opens no
            // sound device at all.
            Common.setIfSupported(player, "SceneViewer", "audioOutput",
                                  Qt.binding(function() { return background.audioOutput !== false; }));
        }

        Connections {
            target: player
            function onFirstFrame() {
                sceneItem.frameShown = true;
                background.sig_backendFirstFrame('scene');
                sceneItem._firstFrameSeen = true;
                loadWatchdog.stop();
            }
            // Named failure from the backend — the renderer already knows it
            // will never draw, so show the real reason instead of waiting out
            // the watchdog's generic "no frame" message.
            function onSceneLoadFailed(reason) {
                loadWatchdog.stop();
                sceneItem.showLoadFailure(reason);
            }
            // Route SceneScript engine.openUserShortcut(name) to MPRIS for
            // well-known media-control names (bplay/bnext/bprev and the
            // numeric aliases solar system uses). Unmapped names still fire
            // the `userShortcut` scene-bus event so wallpapers can handle
            // their own custom shortcuts in-script.
            function onUserShortcutRequested(name) {
                if (mprisLoader.item) mprisLoader.item.invokeShortcut(name);
            }
            // Transient overlay for video-decode failures. Deliberately NOT
            // routed through loadInfoShow: the rest of the scene keeps
            // rendering while one bad video texture surfaces a short toast.
            // A video wallpaper has only the video, so the Mpv backend uses
            // loadInfoShow there; here a scene has other content to preserve.
            function onVideoDecodeFailed(summary) {
                videoDecodeOverlay.show(summary);
            }
        }
        // What the scene holds after its first frame (texture-quality patch),
        // for VramStop's footprint estimate.  A library without vramMiB
        // never sends it.
        Connections {
            target: player
            ignoreUnknownSignals: true
            function onVramMiBChanged() {
                if (player.vramMiB > 0) background.vramEstimateMiB = player.vramMiB;
                sceneItem.vramMiB = player.vramMiB;
            }
        }
    }

    // Transient overlay for video-decode failures. 5s fade after show().
    // Lives outside the SceneViewer so anchors target the wrapper Item and
    // the overlay stays visible even if the player resizes mid-display.
    // `shown` drives visibility (Item.visible defaults true so a direct
    // `visible: false` binding can win over later JS assignment in some
    // qmltestrunner conditions; bind visible to the explicit flag instead).
    Item {
        id: videoDecodeOverlay
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        // Fixed-but-bounded width so the height binding can resolve without
        // looping (Text.implicitHeight is read by the height binding, so the
        // Text must be sized by width not anchors.fill).
        width: Math.min(parent.width * 0.5, 600)
        height: overlayLabel.implicitHeight + 24
        visible: shown
        opacity: shown ? 1.0 : 0
        // Public surface read by tst_backend_scene; production reads the
        // Text below which binds to lastSummary.  `shown` drives `visible`
        // via a binding so JS-side `show()` mutates only this flag.
        property bool   shown: false
        property string lastSummary: ""

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.7)
            radius: 6
        }
        Text {
            id: overlayLabel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            color: "white"
            wrapMode: Text.WordWrap
            text: videoDecodeOverlay.lastSummary
        }
        Behavior on opacity { NumberAnimation { duration: 250 } }
        Timer {
            id: overlayFadeTimer
            interval: 5000
            repeat: false
            onTriggered: videoDecodeOverlay.shown = false
        }
        function show(summary) {
            lastSummary = summary;
            shown = true;
            overlayFadeTimer.restart();
        }
    }

    // Backstop for every silent scene-load failure: a malformed scene.json,
    // no scene JSON in the pkg, an unmountable pkg dir and a failed GL-interop
    // init all bail with nothing but a log line, leaving the desktop on the
    // background colour with no way to tell a broken wallpaper from a broken
    // plugin. 20s because a first-time load compiles shaders and decompresses
    // textures — a slow cold start on a big scene must not trip this.
    Timer {
        id: loadWatchdog
        interval: 20000
        repeat: false
        running: true
        onTriggered: {
            // main.qml pauses the backend 300ms into every launch and plays it
            // back 5s later, and leaves it paused for as long as the desktop
            // stays occluded or locked. A paused scene has no frame for a
            // legitimate reason, so keep waiting instead of replacing a
            // perfectly good wallpaper with a failure pane.
            if (sceneItem._paused) {
                loadWatchdog.restart();
                return;
            }
            sceneItem.showLoadFailure(
                "The scene produced no frame within 20 seconds. It may be corrupt, "
                + "or use features this renderer does not support.");
        }
    }
    // Re-arm on every source change so switching wallpapers re-runs the watch.
    onSourceChanged: {
        _firstFrameSeen = false;
        loadWatchdog.restart();
    }

    function showLoadFailure(reason) {
        if (sceneItem.parent && typeof sceneItem.parent.loadInfoShow === "function")
            sceneItem.parent.loadInfoShow(reason);
        else
            console.log("[WEK] scene load failed: " + reason);
    }

    Component.onCompleted: {
        background.nowBackend = 'scene';
        sceneItem.volumeFade = Common.createVolumeFade(
            sceneItem,
            Qt.binding(function() { return background.volume; }),
            (volume) => { player.volume = volume / 100.0; },
            Qt.binding(function() { return background.mute; }));
    }
    function play() {
        _paused = false;
        // Give the renderer a fresh window to draw in: the load budget should
        // start when rendering actually resumes, not when the item was built.
        if (! _firstFrameSeen) loadWatchdog.restart();
        if (volumeFade) volumeFade.start();
        player.play();
    }
    // The sound fades out (Common.fadeOutMs), then the renderer pauses; a
    // play() before that cancels it.  pause(true) cuts at once (before a sleep).
    function pause(now) {
        _paused = true;
        if (volumeFade && !now) volumeFade.fadeOut(() => player.pause());
        else {
            if (volumeFade) volumeFade.stop();
            player.pause();
        }
    }
    
    // main.qml calls this when the backend becomes the outgoing layer of a
    // handoff: background.* then describes the next wallpaper, so pin the
    // options this one is showing instead of following them.
    function freezeOptions() {
        displayMode = displayMode;
        userPropsJson = userPropsJson;
        player.speed = player.speed;
        player.postprocessingOverride = player.postprocessingOverride;
        player.cameraParallax = player.cameraParallax;
        player.pixelArt = player.pixelArt;
        if ("sceneQuality" in player) player.sceneQuality = player.sceneQuality;
        if ("videoHwdec" in player) player.videoHwdec = player.videoHwdec;
        if ("audioOutput" in player) player.audioOutput = player.audioOutput;
        player.muted = player.muted;
        if (volumeFade) volumeFade.freeze();
    }
    // A mute that comes on while this is the old layer of a handoff, its
    // options frozen (main.qml's muteOutgoingNow, transitions patch): the
    // sound goes at once.
    function muteNow() {
        if (volumeFade) volumeFade.stop();
        player.muted = true;
    }
    function getMouseTarget() {
        return Qt.binding(function() { return player; })
    }
    // WekControl.Screenshot (main.qml): the frame the renderer draws next, at
    // its render size, converted to `path`'s format by SceneViewer.saveFrame.
    // A paused scene draws no frame (pause() stops the renderer's frame
    // timer, as main.qml does behind a maximized window), so then it is
    // `grab` (main.qml's grabScreenshot) of the player, which still shows
    // its last frame.  The grab also stands in when saveFrame reports a
    // failure, and on a plugin library from before the screenshot patch
    // (plasmashell keeps its library across an upgrade inside a session),
    // which has no saveFrame.
    property var _screenshotGrab: null
    function saveScreenshot(path, grab) {
        if (typeof player.saveFrame !== "function") {
            console.warn("[WEK] screenshot: SceneViewer.saveFrame is missing (the plugin library plasmashell "
                         + "runs is older than this QML; log out and in again); the frame on screen instead");
            return typeof grab === "function" ? grab(player, path) : false;
        }
        if (sceneItem._paused && typeof grab === "function") return grab(player, path);
        sceneItem._screenshotGrab = grab || null;
        return player.saveFrame(path);
    }
    Connections {
        target: player
        ignoreUnknownSignals: true
        function onFrameSaved(path, ok, error) {
            if (ok) return;
            console.warn("[WEK] screenshot: " + error + "; the frame on screen instead, for " + path);
            if (typeof sceneItem._screenshotGrab === "function") sceneItem._screenshotGrab(player, path);
        }
    }
}
