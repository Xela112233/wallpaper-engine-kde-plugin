#include "SafeWallpaperBridge.hpp"

namespace wekde
{

SafeWallpaperBridge::SafeWallpaperBridge(QObject* parent): QObject(parent) {}

void SafeWallpaperBridge::pushGeneralProperties(const QVariantMap& m) {
    m_general = m;
    emit generalPropertiesChanged();
    // Emit the JS-facing signal too so wallpapers connected to
    // sigGeneralProperties see the update without QML having to fire it
    // separately. Matches the legacy webobj behaviour (QtWebView.qml
    // fires webobj.sigGeneralProperties(webobj.generalProperties) after
    // every onFpsChanged) but folded into the bridge so the QML side
    // can't accidentally drop one of the two emits.
    emit sigGeneralProperties(m_general);
}

void SafeWallpaperBridge::pushUserProperties(const QVariantMap& m) {
    m_user = m;
    emit userPropertiesChanged();
    emit sigUserProperties(m_user);
}

void SafeWallpaperBridge::setLoaded(bool v) {
    if (m_loaded == v) return; // dedupe redundant transitions
    m_loaded = v;
    if (! v) {
        // The document went away — QtWebView clears loaded both when it
        // replaces the page and when a long pause discards the renderer.
        // Whatever loads next is a new page, so re-arm its init handshake.
        m_initFired = false;
    }
    emit loadedChanged();
    // Init fires once per document, not once per bridge. Repeat trues are
    // in-page navigations on a page that already handshook, and must not
    // re-run it.
    if (v && ! m_initFired) {
        m_initFired = true;
        emit sigInit();
    }
}

void SafeWallpaperBridge::setMediaStatus(bool enabled) {
    if (m_mediaStatus == enabled) return;
    m_mediaStatus = enabled;
    emit sigMediaStatus();
}

void SafeWallpaperBridge::setMediaProperties(const QVariantMap& properties) {
    if (m_mediaProperties == properties) return;
    m_mediaProperties = properties;
    emit sigMediaProperties();
}

void SafeWallpaperBridge::setMediaThumbnail(const QVariantMap& thumbnail) {
    if (m_mediaThumbnail == thumbnail) return;
    m_mediaThumbnail = thumbnail;
    emit sigMediaThumbnail();
}

void SafeWallpaperBridge::setMediaPlayback(int state) {
    if (m_mediaPlayback == state) return;
    m_mediaPlayback = state;
    emit sigMediaPlayback();
}

void SafeWallpaperBridge::setMediaTimeline(const QVariantMap& timeline) {
    if (m_mediaTimeline == timeline) return;
    m_mediaTimeline = timeline;
    emit sigMediaTimeline();
}

void SafeWallpaperBridge::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    emit sigPaused();
}

void SafeWallpaperBridge::clearMedia() {
    // Status first, so a page that hides its now-playing panel on
    // enabled=false does so before the playback state drops to stopped.
    setMediaStatus(false);
    setMediaPlayback(0);
    setMediaProperties({});
    setMediaThumbnail({});
    setMediaTimeline({});
}

} // namespace wekde
