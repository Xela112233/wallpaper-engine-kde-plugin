#pragma once
#include "SafeWallpaperBridge.hpp"
#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace wekde
{

// Wallpaper Engine's web media events, built from MprisMonitor's signals
// (we-manager media integration). Pure helpers, testable without QML.
//
// {title, artist, subTitle, albumTitle, albumArtist, genres, contentType}:
// MPRIS has no subtitle and no content type, so subTitle is empty and
// contentType is "music".
QVariantMap mediaPropertiesEvent(const QString& title, const QString& artist,
                                 const QString& albumTitle, const QString& albumArtist,
                                 const QString& genres);
// {thumbnail, primaryColor, secondaryColor, tertiaryColor, textColor,
// highContrastColor} from the cover and MprisMonitor's 15 colour floats
// (extractDominantColors: primary, secondary, tertiary, black-or-white,
// most distant). thumbnail is a data:image/png;base64 URL of the cover
// scaled down to at most 256x256; colours are CSS "rgb(r, g, b)" strings.
// highContrastColor is the black or white one, as Wallpaper Engine documents
// it; textColor is the most distant colour when it reads on the primary
// colour (WCAG contrast 4.5:1), else the black or white one. Empty for a
// null image or a PNG encoding failure.
QVariantMap mediaThumbnailEvent(const QVariantList& colors, const QImage& image);
// {position, duration}, in seconds.
QVariantMap mediaTimelineEvent(double position, double duration);

// QML-only forwarder onto SafeWallpaperBridge's plain-public setters.
//
// SafeWallpaperBridge's push*/setLoaded methods are deliberately NOT
// Q_INVOKABLE: QWebChannel marshals every Q_INVOKABLE method straight to the
// wallpaper's JS, so an invokable setter there is a setter the wallpaper
// itself can call. But a plain public C++ method is *also* invisible to
// QML — its property/method dispatch only reaches Q_PROPERTY, Q_INVOKABLE,
// and slots — so QML had nothing legal left to call and every web wallpaper
// died at load with a TypeError (issue #29).
//
// This object exists to plug exactly that gap and nothing more: its methods
// ARE Q_INVOKABLE (QML can call them), but it forwards straight to the
// bridge's existing C++ setters rather than duplicating their logic. It must
// NEVER be registered on a QWebChannel — doing so would hand the wallpaper
// back the same setters this whole split exists to keep out of its reach.
// QtWebView.qml wires it as a QML-only sibling of the channel-registered
// bridge, never inside registeredObjects.
class SafeWallpaperBridgeController : public QObject {
    Q_OBJECT
    Q_PROPERTY(wekde::SafeWallpaperBridge* bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)

public:
    explicit SafeWallpaperBridgeController(QObject* parent = nullptr);
    ~SafeWallpaperBridgeController() override = default;

    SafeWallpaperBridge* bridge() const { return m_bridge; }
    void                 setBridge(SafeWallpaperBridge* bridge);

    // QML entry points. Each is a thin forward to the bridge; a null bridge
    // logs and drops the call rather than crashing (QtWebView.qml always
    // binds a real bridge, but nothing stops a stray QML instantiation from
    // skipping it).
    Q_INVOKABLE void setLoaded(bool loaded);
    Q_INVOKABLE void pushUserProperties(const QVariantMap& properties);
    Q_INVOKABLE void pushGeneralProperties(const QVariantMap& properties);
    // Media integration: QtWebView.qml forwards MprisMonitor's signals here,
    // in the shapes Wallpaper Engine's web listeners take (helpers above).
    // A thumbnail without a cover clears the bridge's, so a page that
    // connects later is not handed the previous track's art.
    Q_INVOKABLE void pushMediaStatus(bool enabled);
    Q_INVOKABLE void pushMediaPlayback(int state);
    Q_INVOKABLE void pushMediaProperties(const QString& title, const QString& artist,
                                         const QString& albumTitle, const QString& albumArtist,
                                         const QString& genres);
    Q_INVOKABLE void pushMediaThumbnail(bool hasThumbnail, const QVariantList& colors,
                                        const QImage& image);
    Q_INVOKABLE void pushMediaTimeline(double position, double duration);
    Q_INVOKABLE void clearMedia();
    // Wallpaper Engine's wallpaperPropertyListener.setPaused.
    Q_INVOKABLE void pushPaused(bool paused);

signals:
    void bridgeChanged();

private:
    SafeWallpaperBridge* m_bridge = nullptr;
};

} // namespace wekde
