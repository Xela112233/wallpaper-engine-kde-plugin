// Test stub — see tests/qml/_stubs/README.md for contract.
// Real source: src/TTYSwitchMonitor.hpp + src/TTYSwitchMonitor.cpp
// Last contract review: 2026-11-11

import QtQuick
QtObject {
    // main.qml uses `onTtySwitch: { if (sleep) ... else ... }` — the
    // signal carries a `sleep` arg.
    signal ttySwitch(bool sleep)
    // we-manager resume-reload patch: a resume from sleep (kind suspend | aborted |
    // hibernation | sleep; how logind | clock; the time asleep in ms).
    signal resumedFromSleep(string kind, string how, real asleepMs)
}
