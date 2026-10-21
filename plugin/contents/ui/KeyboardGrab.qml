import QtQuick
import com.github.captsilver.wallpaperEngineKde 1.2

// Keyboard mode's grabber (we-manager input patch), made by main.qml's
// makeKeyHooker on the desktop's AppletsLayout.  A file of its own so that
// a plugin library without KeyGrabber (plasmashell still running the one
// from before an upgrade) fails only this file, not main.qml.
KeyGrabber {
}
