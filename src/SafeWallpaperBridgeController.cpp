#include "SafeWallpaperBridgeController.hpp"

#include <QBuffer>
#include <QDebug>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace wekde
{

namespace
{
// One of MprisMonitor's colours (index 0-4 in its 15-float list), each
// channel 0-1. A short list gives black, as SceneBackend does.
struct Rgb {
    double r { 0 }, g { 0 }, b { 0 };
};

Rgb colourAt(const QVariantList& colors, int idx) {
    if (idx * 3 + 2 >= colors.size()) return {};
    auto ch = [&](int i) { return std::clamp(colors.at(idx * 3 + i).toDouble(), 0.0, 1.0); };
    return { ch(0), ch(1), ch(2) };
}

QString cssColour(const Rgb& c) {
    return QStringLiteral("rgb(%1, %2, %3)")
        .arg(qRound(c.r * 255))
        .arg(qRound(c.g * 255))
        .arg(qRound(c.b * 255));
}

// WCAG 2 relative luminance and contrast ratio.
double luminance(const Rgb& c) {
    auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

double contrast(const Rgb& a, const Rgb& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

constexpr int kThumbnailMax = 256;
} // namespace

QVariantMap mediaPropertiesEvent(const QString& title, const QString& artist,
                                 const QString& albumTitle, const QString& albumArtist,
                                 const QString& genres) {
    return {
        { QStringLiteral("title"), title },
        { QStringLiteral("artist"), artist },
        { QStringLiteral("subTitle"), QString() },
        { QStringLiteral("albumTitle"), albumTitle },
        { QStringLiteral("albumArtist"), albumArtist },
        { QStringLiteral("genres"), genres },
        { QStringLiteral("contentType"), QStringLiteral("music") },
    };
}

QVariantMap mediaThumbnailEvent(const QVariantList& colors, const QImage& image) {
    if (image.isNull()) return {};
    // Wallpaper Engine hands pages a low-resolution cover; a 3000 px one
    // would cost the channel megabytes per track change.
    QImage small = image;
    if (small.width() > kThumbnailMax || small.height() > kThumbnailMax)
        small = image.scaled(kThumbnailMax, kThumbnailMax, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    QByteArray png;
    QBuffer    buffer(&png);
    if (! buffer.open(QIODevice::WriteOnly) || ! small.save(&buffer, "PNG")) {
        qWarning() << "mediaThumbnailEvent: QImage::save(QBuffer, \"PNG\") failed for a"
                   << small.width() << "x" << small.height() << "cover; the page gets no thumbnail";
        return {};
    }
    const Rgb primary = colourAt(colors, 0);
    const Rgb blackOrWhite = colourAt(colors, 3);
    const Rgb distant = colourAt(colors, 4);
    const Rgb text = contrast(distant, primary) >= 4.5 ? distant : blackOrWhite;
    return {
        { QStringLiteral("thumbnail"),
          QString(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64())) },
        { QStringLiteral("primaryColor"), cssColour(primary) },
        { QStringLiteral("secondaryColor"), cssColour(colourAt(colors, 1)) },
        { QStringLiteral("tertiaryColor"), cssColour(colourAt(colors, 2)) },
        { QStringLiteral("textColor"), cssColour(text) },
        { QStringLiteral("highContrastColor"), cssColour(blackOrWhite) },
    };
}

QVariantMap mediaTimelineEvent(double position, double duration) {
    return {
        { QStringLiteral("position"), position },
        { QStringLiteral("duration"), duration },
    };
}

SafeWallpaperBridgeController::SafeWallpaperBridgeController(QObject* parent): QObject(parent) {}

void SafeWallpaperBridgeController::setBridge(SafeWallpaperBridge* bridge) {
    if (m_bridge == bridge) return;
    m_bridge = bridge;
    emit bridgeChanged();
}

void SafeWallpaperBridgeController::setLoaded(bool loaded) {
    if (! m_bridge) {
        qWarning() << "SafeWallpaperBridgeController::setLoaded: no bridge bound, dropping call";
        return;
    }
    m_bridge->setLoaded(loaded);
}

void SafeWallpaperBridgeController::pushUserProperties(const QVariantMap& properties) {
    if (! m_bridge) {
        qWarning() << "SafeWallpaperBridgeController::pushUserProperties: no bridge bound, "
                      "dropping call";
        return;
    }
    m_bridge->pushUserProperties(properties);
}

void SafeWallpaperBridgeController::pushGeneralProperties(const QVariantMap& properties) {
    if (! m_bridge) {
        qWarning() << "SafeWallpaperBridgeController::pushGeneralProperties: no bridge bound, "
                      "dropping call";
        return;
    }
    m_bridge->pushGeneralProperties(properties);
}

// The media forwards drop silently without a bridge, like the others but
// without the warning: MprisMonitor reports once a second while a track
// plays, and a log line per tick would flood the journal.
void SafeWallpaperBridgeController::pushMediaStatus(bool enabled) {
    if (m_bridge) m_bridge->setMediaStatus(enabled);
}

void SafeWallpaperBridgeController::pushMediaPlayback(int state) {
    if (m_bridge) m_bridge->setMediaPlayback(state);
}

void SafeWallpaperBridgeController::pushMediaProperties(const QString& title, const QString& artist,
                                                        const QString& albumTitle,
                                                        const QString& albumArtist,
                                                        const QString& genres) {
    if (m_bridge)
        m_bridge->setMediaProperties(
            mediaPropertiesEvent(title, artist, albumTitle, albumArtist, genres));
}

void SafeWallpaperBridgeController::pushMediaThumbnail(bool hasThumbnail, const QVariantList& colors,
                                                       const QImage& image) {
    if (! m_bridge) return;
    m_bridge->setMediaThumbnail(hasThumbnail ? mediaThumbnailEvent(colors, image) : QVariantMap());
}

void SafeWallpaperBridgeController::pushMediaTimeline(double position, double duration) {
    if (m_bridge) m_bridge->setMediaTimeline(mediaTimelineEvent(position, duration));
}

void SafeWallpaperBridgeController::clearMedia() {
    if (m_bridge) m_bridge->clearMedia();
}

void SafeWallpaperBridgeController::pushPaused(bool paused) {
    if (m_bridge) m_bridge->setPaused(paused);
}

} // namespace wekde
