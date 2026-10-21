// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/MouseGrabber.hpp + src/MouseGrabber.cpp
// Last contract review: 2026-09-21

import QtQuick
Item {
    property var target: null
    property bool forceCapture: false
    // Fluid simulation (fluid-effect patch).  The real one is a bool that
    // starts false; here it has no value until main.qml writes one, so a
    // test sees whether it was written at all.
    property var hoverLeaveToTarget
    function sendEvent(obj, event) {}
}
