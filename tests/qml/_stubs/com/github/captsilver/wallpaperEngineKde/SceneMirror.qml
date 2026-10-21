// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/plugin.cpp registers scenebackend::SceneMirror (src/backend_scene/qml_helper/SceneMirror.hpp)
// Last contract review: 2026-11-12

// Another window's scene frame (we-manager clone patch).  No OpenGL here:
// tests set hasFrame and frameAspect, as the real item does once the
// renderer's frame arrives, and emit failed(reason) themselves.  windowDrawn
// (test-only) says whether this item's window is being drawn: probeSync()
// counts a sync only then, as the real item's count goes up only in a window
// that draws.  tst_sharedframe.cpp covers the real item between two windows.
import QtQuick
Item {
    property string frameKey: ""
    property rect   sourceRect: Qt.rect(0, 0, 1, 1)
    property bool   live: true
    property bool   hasFrame: false
    property real   frameAspect: 0
    property string error: ""
    signal failed(string reason)
    function mapToSource(point) { return point; }
    property bool   windowDrawn: true
    property int    syncs: 0
    function probeSync() {
        const before = syncs;
        if (windowDrawn) syncs += 1;
        return before;
    }
}
