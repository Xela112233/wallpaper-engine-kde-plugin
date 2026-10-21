import QtQuick 2.5
import com.github.captsilver.wallpaperEngineKde 1.2
import ".."

Item{
    id: videoItem
    // Set once this backend has put a frame on screen; main.qml keeps the
    // previous backend visible until then.
    property bool frameShown: false
    anchors.fill: parent
    property alias source: player.source
    property int displayMode: background.displayMode
    property real videoRate: background.speed
    readonly property bool stats: background.mpvStats
    // No userPropsJson binding on purpose. A project.json property binds to a
    // shader uniform or a scene script, and a video has neither — mpv just
    // plays the file. The wallpaper settings hide those controls for video
    // wallpapers to match; wiring one here means going back and re-enabling
    // them.
    // A mute fades out before mpv drops the audio track (aid=no), and a
    // pause before mpv pauses (we-manager mute patch: Common.createVolumeFade).
    // Made in Component.onCompleted, not in a binding here: the fade reads its
    // own properties as it starts, and inside a binding that would make it
    // again at every change of the mute.
    property var volumeFade: null
    
    onDisplayModeChanged: {
        if(videoItem.displayMode == Common.DisplayMode.Crop) {
            player.setProperty("keepaspect", true);
            player.setProperty("panscan", 1.0);
        } else if(videoItem.displayMode == Common.DisplayMode.Aspect) {
            player.setProperty("keepaspect", true);
            player.setProperty("panscan", 0.0);
        } else if(videoItem.displayMode == Common.DisplayMode.Scale) {
            player.setProperty("keepaspect", false);
            player.setProperty("panscan", 0.0);
        }
    }

    // Force displayMode update on background.displayMode change
    Timer {
        id: displayModeFixTimer
        interval: 50
        repeat: false
        onTriggered: videoItem.displayModeChanged()
    }
    Connections {
        target: background
        function onDisplayModeChanged() {
            displayModeFixTimer.restart();
        }
    }
    // it's ok for toggle, true will always cause a signal at first
    onStatsChanged: {
        player.command(["script-binding","stats/display-stats-toggle"]);
    }

    onVideoRateChanged: player.setProperty('speed', videoRate);

    // logfile
    // source
    // mute
    // volume
    // fun:setProperty(name,value)
    Mpv {
        id: player
        anchors.fill: parent
        // The fade's hard mute (background.mute until the fade exists).
        mute: videoItem.volumeFade ? videoItem.volumeFade.state.hardMuted : background.mute
        volume: 0
        // Opt-in power saving: cheap scalers, the video's own frame rate, and
        // at most as many redraws per second as scenes get (Fps setting).
        powerSaving: background.mpvPowerSaving
        maxFps: background.mpvPowerSaving ? background.fps : 0
        // Opt-in frame blending at the display's refresh rate.  It needs
        // mpv's display sync, which power saving turns off (video-sync=audio).
        interpolation: background.mpvInterpolation && !background.mpvPowerSaving
        // mpv-options patch.  hwdec off decodes on the CPU (broken VA-API
        // drivers); loadInMemory caches the whole file from the next load;
        // videoSync picks the timing, below interpolation and power saving.
        // Bound here rather than declared: a plugin library from before the
        // patch (plasmashell keeps its library across an upgrade inside a
        // session) has none of the three, and a declaration would fail this
        // file and every video wallpaper (Common.setIfSupported).  Before
        // the first frame, so the first load already uses them.
        Component.onCompleted: {
            Common.setIfSupported(player, "Mpv", "hwdec", Qt.binding(function() { return background.mpvHwdec; }));
            Common.setIfSupported(player, "Mpv", "loadInMemory",
                                  Qt.binding(function() { return background.mpvLoadInMemory; }));
            Common.setIfSupported(player, "Mpv", "videoSync",
                                  Qt.binding(function() { return background.mpvVideoSync; }));
        }
        Connections {
            ignoreUnknownSignals: true
            function onFirstFrame() {
                videoItem.frameShown = true;
                background.sig_backendFirstFrame('mpv');
                loadWatchdog.stop();
            }
        }
    }

    // MpvBackend now emits sourceLoadFailed for both sync (loadfile reject)
    // and async (MPV_EVENT_END_FILE reason=ERROR) failures — handled by
    // the Connections block below, which routes the reason to InfoShow
    // immediately and stops the watchdog. The watchdog stays in place
    // as a true-silent-hang backstop: it only fires when mpv accepts
    // the load, never emits END_FILE, and never produces a frame — a
    // shape we still see for sources that mpv opens but stalls on.
    Connections {
        target: player
        ignoreUnknownSignals: true
        function onSourceLoadFailed(reason) {
            loadWatchdog.stop();
            if (videoItem.parent
                && typeof videoItem.parent.loadInfoShow === "function") {
                videoItem.parent.loadInfoShow(
                    "MPV could not load this video: " + reason);
            }
        }
    }
    Timer {
        id: loadWatchdog
        // A network stream can take much longer to open and fill mpv's
        // network cache (MpvObject's isNetworkSource names the same schemes);
        // its own failures still arrive at once through sourceLoadFailed.
        interval: /^(https?|rtmps?|rtsp|rtp|udp|tcp|srt|mms|ytdl):/i.test(String(videoItem.source)) ? 60000 : 15000
        repeat: false
        running: true
        onTriggered: {
            if (videoItem.parent
                && typeof videoItem.parent.loadInfoShow === "function") {
                videoItem.parent.loadInfoShow(
                    "MPV produced no frame within " + (interval / 1000) + "s.");
            }
        }
    }
    Component.onCompleted:{
        background.nowBackend = 'mpv';
        videoItem.volumeFade = Common.createVolumeFade(
            videoItem,
            Qt.binding(function() { return background.volume; }),
            (volume) => { player.volume = volume; },
            Qt.binding(function() { return background.mute; }));
        videoItem.displayModeChanged();
    }

    function play(){
        // A play during a pause's fade-out cancels the pause.
        player.play();
        if (volumeFade) volumeFade.start();
    }
    // main.qml calls this when the backend becomes the outgoing layer of a
    // handoff: background.* then describes the next wallpaper, so pin the
    // options this one is showing instead of following them.
    function freezeOptions() {
        displayMode = displayMode;
        videoRate = videoRate;
        player.mute = player.mute;
        player.powerSaving = player.powerSaving;
        player.maxFps = player.maxFps;
        player.interpolation = player.interpolation;
        for (const name of ["hwdec", "loadInMemory", "videoSync"])
            if (name in player) player[name] = player[name];
        if (volumeFade) volumeFade.freeze();
    }
    // A mute that comes on while this is the old layer of a handoff, its
    // options frozen (main.qml's muteOutgoingNow, transitions patch): the
    // sound goes at once.
    function muteNow() {
        if (volumeFade) volumeFade.stop();
        player.mute = true;
    }
    // Called by main.qml once this backend has faded out, some time before
    // destroying it: mpv drops its video output off the render thread, so
    // ~MpvRender no longer waits on the core there.
    function prepareDestroy() {
        player.stopAsync();
    }
    // The sound fades out (Common.fadeOutMs), then mpv pauses; pause(true)
    // cuts at once (before a sleep).
    function pause(now){
        if (volumeFade && !now) volumeFade.fadeOut(() => player.pause());
        else {
            if (volumeFade) volumeFade.stop();
            player.pause();
        }
    }
    function getMouseTarget() {
    }
}
