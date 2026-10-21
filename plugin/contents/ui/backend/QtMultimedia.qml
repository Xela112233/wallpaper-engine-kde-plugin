import QtQuick 2.5
import QtMultimedia
import ".."

Item{
    id: videoItem
    // Set once this backend has put a frame on screen; main.qml keeps the
    // previous backend visible until then.
    property bool frameShown: false
    anchors.fill: parent
    property alias source: player.source
    property int displayMode: background.displayMode
    // Latch for the first-frame announcement below; a new clip gets a new
    // announcement, a pause/resume cycle does not.
    property bool announcedFirstFrame: false
    onSourceChanged: announcedFirstFrame = false
    // A mute fades out before the output is muted, and a pause before the
    // player pauses (we-manager mute patch: Common.createVolumeFade).
    // Made in Component.onCompleted, not in a binding here: the fade reads its
    // own properties as it starts, and inside a binding that would make it
    // again at every change of the mute.
    property var volumeFade: null

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            videoView.fillMode = VideoOutput.Stretch;
        else if(displayMode == Common.DisplayMode.Aspect)
            videoView.fillMode = VideoOutput.PreserveAspectFit;
        else if(displayMode == Common.DisplayMode.Crop)
            videoView.fillMode = VideoOutput.PreserveAspectCrop;
    }

    // Force fillMode update on background.displayMode change
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

    VideoOutput {
        id: videoView
        //fillMode: wallpaper.configuration.FillMode
        anchors.fill: parent
    }
    AudioOutput {
        id: audioOut
        volume: 0.0
        muted: videoItem.volumeFade ? videoItem.volumeFade.state.hardMuted : background.mute
    }
    MediaPlayer {
        id: player
        loops: MediaPlayer.Infinite
        playbackRate: background.speed
        videoOutput: videoView
        // Audio output off (we-manager mute patch): no output at all, so the
        // player keeps no audio stream open (a muted AudioOutput keeps its
        // sink, at volume 0); it comes back when the switch is on again.
        audioOutput: background.audioOutput !== false ? audioOut : null
    }
    // Surface load errors to InfoShow so users see a real reason instead
    // of a black screen. Use a Connections block — assigning
    // onErrorOccurred directly on MediaPlayer fails on Qt 6 because the
    // signal isn't exposed as a settable property at the QML type level.
    Connections {
        target: player
        ignoreUnknownSignals: true
        function onErrorOccurred(error, errorString) {
            console.warn("[WEK QtMultimedia] error", error, errorString);
            if (videoItem.parent
                && typeof videoItem.parent.loadInfoShow === "function") {
                videoItem.parent.loadInfoShow(
                    "Video failed to load: " + (errorString || ("error " + error)));
            }
        }
        // The other three backends emit a real first-frame signal; main.qml
        // needs the announcement to stamp the version it just displayed
        // (that stamp is what clears the "Updated" badge). QtMultimedia has
        // nothing frame-level to hook, so playback entering PlayingState is
        // the closest honest equivalent — by then the decoder has handed the
        // VideoOutput something to show.
        function onPlaybackStateChanged() {
            if (player.playbackState !== MediaPlayer.PlayingState) return;
            if (videoItem.announcedFirstFrame) return;
            videoItem.announcedFirstFrame = true;
            videoItem.frameShown = true;
            background.sig_backendFirstFrame('QtMultimedia');
        }
    }
    Component.onCompleted:{
        background.nowBackend = "QtMultimedia";
        videoItem.volumeFade = Common.createVolumeFade(
            videoItem,
            Qt.binding(function() { return background.volume; }),
            (volume) => { audioOut.volume = volume / 100.0; },
            Qt.binding(function() { return background.mute; }));
        videoItem.displayModeChanged();
    }

    function play(){
        // A play during a pause's fade-out cancels the pause.
        player.play();
        if (volumeFade) volumeFade.start();
    }
    // The sound fades out (Common.fadeOutMs), then the player pauses;
    // pause(true) cuts at once (before a sleep).
    function pause(now){
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
        player.playbackRate = player.playbackRate;
        audioOut.muted = audioOut.muted;
        if (volumeFade) volumeFade.freeze();
    }
    function getMouseTarget() {
    }
}
