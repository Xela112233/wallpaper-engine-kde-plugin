// Regression test for issue #29: SafeWallpaperBridge's push*/setLoaded
// methods are plain public C++ methods — deliberately not Q_INVOKABLE, so
// QWebChannel can't marshal them to web-wallpaper JS — but a plain public
// method is *also* unreachable from QML, whose method dispatch only sees
// Q_PROPERTY/Q_INVOKABLE/slots. QML had nothing legal left to call, so every
// web wallpaper died at load with a TypeError.
//
// SafeWallpaperBridgeController plugs that gap: its own setters ARE
// Q_INVOKABLE and forward straight to the bridge's C++ methods. This test
// drives it through a real QQmlEngine — not the C++ API directly — because a
// change that made the controller's methods QML-unreachable again (wrong
// signature, missing Q_INVOKABLE, wrong property type) would compile clean
// and pass a C++-only test while still leaving QML with nothing to call.

#include "SafeWallpaperBridge.hpp"
#include "SafeWallpaperBridgeController.hpp"

#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using wekde::SafeWallpaperBridge;
using wekde::SafeWallpaperBridgeController;

// Stands in for MprisMonitor in the QML below: a C++ signal carrying a
// QImage, which QML hands on to the controller exactly as QtWebView.qml's
// onThumbnailChanged does.
class FakeCoverSource : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void send(bool has, const QVariantList& colors, const QImage& image) {
        emit thumbnailChanged(has, colors, image);
    }
signals:
    void thumbnailChanged(bool hasThumbnail, const QVariantList& colors, const QImage& image);
};

namespace
{
// MprisMonitor's 15-float order: primary, secondary, tertiary,
// black-or-white, most distant.
QVariantList colours(std::initializer_list<double> v) {
    QVariantList out;
    for (double d : v) out << d;
    return out;
}

QImage decodeDataUrl(const QString& url) {
    const QString prefix = QStringLiteral("data:image/png;base64,");
    if (! url.startsWith(prefix)) return {};
    return QImage::fromData(QByteArray::fromBase64(url.mid(prefix.size()).toLatin1()), "PNG");
}
} // namespace

class TestSafeWallpaperBridgeController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qmlRegisterType<SafeWallpaperBridge>("wekde.test", 1, 0, "SafeWallpaperBridge");
        qmlRegisterType<SafeWallpaperBridgeController>(
            "wekde.test", 1, 0, "SafeWallpaperBridgeController");
        qmlRegisterType<FakeCoverSource>("wekde.test", 1, 0, "FakeCoverSource");
    }

    // The QML side declares the bridge + controller exactly the way
    // QtWebView.qml does (SafeWallpaperBridgeController { bridge: webobj }),
    // then calls the controller's setters from a QML function — standing in
    // for the JS call sites in production QML. Spies are attached to the
    // bridge's real C++ signals in between construction and the call, so a
    // regression that routes around the controller (or around the bridge's
    // signal-firing setters) shows up as a spy count mismatch, not just a
    // final-state check.
    void controllerReachesBridgeThroughQml() {
        QQmlEngine    engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import wekde.test 1.0

            QtObject {
                id: root
                property QtObject bridge: SafeWallpaperBridge {}
                property QtObject ctl: SafeWallpaperBridgeController { bridge: root.bridge }

                function driveFromJs() {
                    ctl.setLoaded(true);
                    ctl.pushUserProperties({ a: 1 });
                    ctl.pushGeneralProperties({ fps: 24 });
                }
            }
        )QML",
                          QUrl());

        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY(root != nullptr);

        auto* bridge =
            qobject_cast<SafeWallpaperBridge*>(root->property("bridge").value<QObject*>());
        QVERIFY(bridge != nullptr);

        QSignalSpy initSpy(bridge, &SafeWallpaperBridge::sigInit);
        QSignalSpy userPropsSpy(bridge, &SafeWallpaperBridge::sigUserProperties);
        QSignalSpy generalPropsSpy(bridge, &SafeWallpaperBridge::sigGeneralProperties);

        QVERIFY(QMetaObject::invokeMethod(root.data(), "driveFromJs"));

        QCOMPARE(bridge->loaded(), true);
        QCOMPARE(bridge->userProperties().value("a").toInt(), 1);
        QCOMPARE(bridge->generalProperties().value("fps").toInt(), 24);
        QCOMPARE(initSpy.count(), 1);
        QCOMPARE(userPropsSpy.count(), 1);
        QCOMPARE(generalPropsSpy.count(), 1);
    }

    // The media forwards QtWebView.qml makes from MprisMonitor's handlers,
    // through a real QQmlEngine, including the QImage a C++ signal hands QML.
    void mediaPushesReachTheBridgeThroughQml() {
        QQmlEngine    engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import wekde.test 1.0

            QtObject {
                id: root
                property QtObject bridge: SafeWallpaperBridge {}
                property QtObject ctl: SafeWallpaperBridgeController { bridge: root.bridge }
                property QtObject cover: FakeCoverSource {
                    onThumbnailChanged: function(hasThumbnail, colors, image) {
                        root.ctl.pushMediaThumbnail(hasThumbnail, colors, image);
                    }
                }

                function driveFromJs() {
                    ctl.pushMediaStatus(true);
                    ctl.pushMediaPlayback(2);
                    ctl.pushMediaProperties("Song", "Band", "Album", "Album Band", "Rock, Pop");
                    ctl.pushMediaTimeline(12.5, 200);
                    ctl.pushPaused(true);
                }
                function clear() { ctl.clearMedia(); }
            }
        )QML",
                          QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> root(component.create());
        QVERIFY(root != nullptr);
        auto* bridge =
            qobject_cast<SafeWallpaperBridge*>(root->property("bridge").value<QObject*>());
        auto* cover = qobject_cast<FakeCoverSource*>(root->property("cover").value<QObject*>());
        QVERIFY(bridge != nullptr && cover != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root.data(), "driveFromJs"));
        QCOMPARE(bridge->mediaStatus(), true);
        QCOMPARE(bridge->mediaPlayback(), 2);
        const QVariantMap props = bridge->mediaProperties();
        QCOMPARE(props.value("title").toString(), QString("Song"));
        QCOMPARE(props.value("artist").toString(), QString("Band"));
        QCOMPARE(props.value("albumTitle").toString(), QString("Album"));
        QCOMPARE(props.value("albumArtist").toString(), QString("Album Band"));
        QCOMPARE(props.value("genres").toString(), QString("Rock, Pop"));
        QVERIFY(props.contains("subTitle"));
        QCOMPARE(props.value("subTitle").toString(), QString());
        QCOMPARE(props.value("contentType").toString(), QString("music"));
        QCOMPARE(bridge->mediaTimeline().value("position").toDouble(), 12.5);
        QCOMPARE(bridge->mediaTimeline().value("duration").toDouble(), 200.0);
        QCOMPARE(bridge->paused(), true);

        QImage red(600, 300, QImage::Format_RGB32);
        red.fill(Qt::red);
        cover->send(true, colours({ 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0 }), red);
        const QVariantMap thumb = bridge->mediaThumbnail();
        const QImage      back  = decodeDataUrl(thumb.value("thumbnail").toString());
        QCOMPARE(back.size(), QSize(256, 128));
        QCOMPARE(back.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(thumb.value("primaryColor").toString(), QString("rgb(255, 0, 0)"));

        // A track without a cover drops the last one.
        cover->send(false, {}, QImage());
        QVERIFY(bridge->mediaThumbnail().isEmpty());

        QVERIFY(QMetaObject::invokeMethod(root.data(), "clear"));
        QCOMPARE(bridge->mediaStatus(), false);
        QCOMPARE(bridge->mediaPlayback(), 0);
        QVERIFY(bridge->mediaProperties().isEmpty());
        QVERIFY(bridge->mediaTimeline().isEmpty());
        QCOMPARE(bridge->paused(), true);
    }

    // Wallpaper Engine's colour contract: highContrastColor is black or
    // white, textColor reads on the primary colour.
    void mediaThumbnailEvent_coloursFollowWallpaperEngine() {
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(Qt::darkBlue);
        // Primary dark blue, then green and grey; white reads on it, and so
        // does the most distant colour, yellow.
        auto ev = wekde::mediaThumbnailEvent(
            colours({ 0, 0, 0.5, 0, 1, 0, 0.5, 0.5, 0.5, 1, 1, 1, 1, 1, 0 }), img);
        QCOMPARE(ev.value("primaryColor").toString(), QString("rgb(0, 0, 128)"));
        QCOMPARE(ev.value("secondaryColor").toString(), QString("rgb(0, 255, 0)"));
        QCOMPARE(ev.value("tertiaryColor").toString(), QString("rgb(128, 128, 128)"));
        QCOMPARE(ev.value("highContrastColor").toString(), QString("rgb(255, 255, 255)"));
        QCOMPARE(ev.value("textColor").toString(), QString("rgb(255, 255, 0)"));
        // A most distant colour too close to the primary one for text: the
        // black or white one is the text colour instead.
        ev = wekde::mediaThumbnailEvent(
            colours({ 0, 0, 0.5, 0, 1, 0, 0.5, 0.5, 0.5, 1, 1, 1, 0.1, 0.1, 0.6 }), img);
        QCOMPARE(ev.value("textColor").toString(), QString("rgb(255, 255, 255)"));
        // A small cover keeps its size; a short colour list gives black.
        ev = wekde::mediaThumbnailEvent({}, img);
        QCOMPARE(decodeDataUrl(ev.value("thumbnail").toString()).size(), QSize(64, 64));
        QCOMPARE(ev.value("primaryColor").toString(), QString("rgb(0, 0, 0)"));
        // No image, no event.
        QVERIFY(wekde::mediaThumbnailEvent({}, QImage()).isEmpty());
    }

    // A null bridge must not crash the process — QtWebView.qml's controller
    // is bound to a static sibling so this never happens in production, but
    // a future caller (or a test harness) constructing the controller before
    // wiring `bridge` must get a loud no-op, not a segfault.
    void nullBridge_doesNotCrash_callsAreDropped() {
        SafeWallpaperBridgeController ctl;
        QVERIFY(ctl.bridge() == nullptr);
        ctl.setLoaded(true);
        ctl.pushUserProperties({ { "a", 1 } });
        ctl.pushGeneralProperties({ { "fps", 24 } });
        ctl.pushMediaStatus(true);
        ctl.pushMediaPlayback(1);
        ctl.pushMediaProperties("t", "a", "al", "aa", "g");
        ctl.pushMediaThumbnail(true, {}, QImage(4, 4, QImage::Format_RGB32));
        ctl.pushMediaTimeline(1, 2);
        ctl.clearMedia();
        ctl.pushPaused(true);
        // Nothing to assert beyond "didn't crash" — there is no bridge to
        // observe state on.
    }
};

QTEST_MAIN(TestSafeWallpaperBridgeController)
#include "tst_safewallpaperbridgecontroller.moc"
