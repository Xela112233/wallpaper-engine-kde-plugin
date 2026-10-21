// Stub of the C++ WekControl class for QML test harnesses.
//
// Production: registers a session-bus service that exposes Next / Previous /
// Pause / etc. and emits WallpaperChanged on every transition.  In tests
// the bus surface is irrelevant -- the QML tests only care that the
// emitWallpaperChanged hook is callable and that setPlaylistController is
// a no-op (the routing is exercised in tst_wekcontrol.cpp).
//
// broadcastStop (StopScope "all screens") does go round: every stub in the
// engine shares WekControlRegistry.js, like the C++ process-wide registry,
// so two ScreenRigs reach each other's controllers.  The calls are direct
// here; the C++ queues them (tst_wekcontrol.cpp covers that side).
import QtQuick
import "WekControlRegistry.js" as Registry

QtObject {
    id: control
    // Recorded values so tests can assert the last emission.
    property string lastEmittedWorkshopId: ""
    property string lastEmittedPlaylistId: ""

    property var _controller: null
    property bool _stopAnnounced: false
    readonly property string _name: "wekcontrol-stub-" + Registry.nextId()

    function setPlaylistController(controller) {
        // Only the registry for broadcastStop: the other routes go through
        // QMetaObject in the real build, which QML stubs do not need.
        _controller = controller;
        if (Registry.controls.indexOf(control) < 0) Registry.controls.push(control);
    }

    function broadcastStop(stopped) {
        _stopAnnounced = stopped;
        for (const c of Registry.controls.slice())
            if (c !== control && c._controller) c._controller.remoteStop(_name, stopped);
    }

    Component.onDestruction: {
        if (_stopAnnounced) broadcastStop(false);
        const i = Registry.controls.indexOf(control);
        if (i >= 0) Registry.controls.splice(i, 1);
    }

    function emitWallpaperChanged(workshopId, playlistId) {
        lastEmittedWorkshopId = workshopId;
        lastEmittedPlaylistId = playlistId;
    }
}
