// Unpause during Overview (we-manager unpause-overview patch): WindowModel.qml
// makes this with Qt.createComponent, so that a plugin library without
// EffectsWatcher (one from before the patch, kept by plasmashell across an
// RPM upgrade) costs only the Overview unpause, not every window pause.  See
// src/EffectsWatcher.hpp.
import QtQuick
import com.github.captsilver.wallpaperEngineKde

EffectsWatcher {}
