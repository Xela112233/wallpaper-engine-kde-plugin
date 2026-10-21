// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/VramSampler.hpp + src/VramSampler.cpp (we-manager vram patch)
// Last contract review: 2026-11-11

// The process-wide video memory monitor's facade.  Tests set stopWanted,
// tight, freeMiB and device themselves; every change emits changed(), the
// one signal the C++ type has.
import QtQuick
QtObject {
    id: stub
    property bool   enabled: false
    property bool   available: true
    property string reason: ""
    property string device: "Stub GPU"
    property bool   stopWanted: false
    property bool   tight: false
    property real   freeMiB: 4096
    property real   heapMiB: 8192
    signal changed()
    onStopWantedChanged: changed()
    onTightChanged: changed()
    onEnabledChanged: changed()
    function statusJson() {
        return JSON.stringify({ available: available, reason: reason, device: device, stopWanted: stopWanted,
                                tight: tight, freeMiB: freeMiB, heapMiB: heapMiB });
    }
}
