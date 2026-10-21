// Stop on low video memory (we-manager vram patch): main.qml makes this with
// Qt.createComponent, so that a plugin library without VramSampler (one from
// before the patch, kept by plasmashell across an RPM upgrade) costs only
// VramStop and not the whole wallpaper.  See src/VramSampler.hpp.
import QtQuick
import com.github.captsilver.wallpaperEngineKde

VramSampler {}
