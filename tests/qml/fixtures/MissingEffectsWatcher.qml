// Stands for OverviewWatch.qml on a plugin library from before the
// unpause-overview patch: it names a type the library does not have, so
// Qt.createComponent fails as it does there (tst_windowmodel.qml).
import QtQuick
import com.github.captsilver.wallpaperEngineKde

NoSuchEffectsWatcher {}
