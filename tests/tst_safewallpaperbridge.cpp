// Tests for SafeWallpaperBridge — the C++ QWebChannel host object that
// replaces the inline QtObject `webobj`. Pins the contract:
//   * Q_PROPERTY surface is READ-only (no JS-side WRITE through QWebChannel).
//   * QML-only setters update the mirror + fire NOTIFY.
//   * Three signals: sigGeneralProperties / sigUserProperties / sigAudio.
//   * An init signal: sigInit (fired by QML on LoadSucceededStatus, once per
//     document; replaces the page-injected wpeQml.loaded = true).
//   * No Q_INVOKABLE methods (web JS receives signals only).
//   * Media integration and pause state (we-manager): READ-only properties
//     whose NOTIFY is an argument-less sig* signal, set only through plain
//     methods, each emitting only on a change.
//
// Real-world JS-write attempts are exercised in the QML integration test
// (tests/qml/tst_backend_qtwebview.qml) by re-targeting the inline-test
// bridge through QWebChannel — that's a Quick test, this file is the unit
// contract.

#include "SafeWallpaperBridge.hpp"

#include <QCoreApplication>
#include <QList>
#include <QMetaProperty>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using wekde::SafeWallpaperBridge;

class TestSafeWallpaperBridge : public QObject {
    Q_OBJECT

private slots:
    void defaults_areEmptyAndUnloaded() {
        SafeWallpaperBridge b;
        QVERIFY(b.generalProperties().isEmpty());
        QVERIFY(b.userProperties().isEmpty());
        QCOMPARE(b.loaded(), false);
    }

    void pushGeneralProperties_updatesMirrorAndFiresNotify() {
        SafeWallpaperBridge b;
        QSignalSpy          notifySpy(&b, &SafeWallpaperBridge::generalPropertiesChanged);
        QSignalSpy          sigSpy(&b, &SafeWallpaperBridge::sigGeneralProperties);

        QVariantMap m;
        m["fps"] = 30;
        b.pushGeneralProperties(m);

        QCOMPARE(b.generalProperties().value("fps").toInt(), 30);
        QCOMPARE(notifySpy.count(), 1);
        // sigGeneralProperties is emitted alongside NOTIFY so QML consumers
        // and JS consumers see the same event. (Production fires the sig
        // signal explicitly in QtWebView.qml after each push; pin that here
        // to make the bridge self-consistent.)
        QCOMPARE(sigSpy.count(), 1);
        QCOMPARE(sigSpy.at(0).at(0).toMap().value("fps").toInt(), 30);
    }

    void pushUserProperties_updatesMirrorAndFiresNotify() {
        SafeWallpaperBridge b;
        QSignalSpy          notifySpy(&b, &SafeWallpaperBridge::userPropertiesChanged);
        QSignalSpy          sigSpy(&b, &SafeWallpaperBridge::sigUserProperties);

        QVariantMap m;
        QVariantMap slider;
        slider["value"] = 75;
        slider["type"]  = "slider";
        m["sliderProp"] = slider;
        b.pushUserProperties(m);

        QCOMPARE(b.userProperties().value("sliderProp").toMap().value("value").toInt(), 75);
        QCOMPARE(notifySpy.count(), 1);
        QCOMPARE(sigSpy.count(), 1);
    }

    void setLoaded_dedupesRepeatTrue_andReArmsInitAfterUnload() {
        SafeWallpaperBridge b;
        QSignalSpy          notifySpy(&b, &SafeWallpaperBridge::loadedChanged);
        QSignalSpy          initSpy(&b, &SafeWallpaperBridge::sigInit);

        // First true: fires both.
        b.setLoaded(true);
        QCOMPARE(b.loaded(), true);
        QCOMPARE(notifySpy.count(), 1);
        QCOMPARE(initSpy.count(), 1);

        // Idempotent: setting true again does NOT re-fire (defends the
        // production handshake against double-init, since LoadSucceededStatus
        // can fire multiple times for in-page navs).
        b.setLoaded(true);
        QCOMPARE(notifySpy.count(), 1);
        QCOMPARE(initSpy.count(), 1);

        // false means the document is gone — QtWebView says so when it
        // replaces the page and when a discard kills the renderer. Whatever
        // loads afterwards is a fresh page with a fresh JS context, so it gets
        // its own handshake: the previous document's connections died with it.
        b.setLoaded(false);
        QCOMPARE(notifySpy.count(), 2);
        QCOMPARE(initSpy.count(), 1);
        b.setLoaded(true);
        QCOMPARE(notifySpy.count(), 3);
        QCOMPARE(initSpy.count(), 2);
    }

    void sigAudio_emitsQListDoubleNotQVariantList() {
        // Audio uses QList<double> (zero-copy) per WebAudioBridge convention
        // (see WebAudioBridge::audioBuffer at WebAudioBridge.hpp). Pin the
        // type so a future refactor can't silently re-introduce per-tick
        // QVariantList heap boxes.
        SafeWallpaperBridge b;
        QSignalSpy          spy(&b, &SafeWallpaperBridge::sigAudio);

        QList<double> samples;
        for (int i = 0; i < 128; ++i) samples.append(double(i) / 128.0);
        emit b.sigAudio(samples);

        QCOMPARE(spy.count(), 1);
        // Compile-time pin: signal arg must round-trip via QList<double>.
        static_assert(
            std::is_same_v<decltype(std::declval<SafeWallpaperBridge>().sigAudio(samples)), void>);
    }

    void media_defaults_areStoppedAndEmpty() {
        SafeWallpaperBridge b;
        QCOMPARE(b.mediaStatus(), false);
        QCOMPARE(b.mediaPlayback(), 0);
        QVERIFY(b.mediaProperties().isEmpty());
        QVERIFY(b.mediaThumbnail().isEmpty());
        QVERIFY(b.mediaTimeline().isEmpty());
        QCOMPARE(b.paused(), false);
    }

    // Each setter emits its NOTIFY once per change and never for the same
    // value: the 1 Hz position poll repeats a paused track's position, and a
    // repeat must not cost the page another channel message.
    void media_settersEmitOnlyOnChange() {
        SafeWallpaperBridge b;
        QSignalSpy          status(&b, &SafeWallpaperBridge::sigMediaStatus);
        QSignalSpy          props(&b, &SafeWallpaperBridge::sigMediaProperties);
        QSignalSpy          thumb(&b, &SafeWallpaperBridge::sigMediaThumbnail);
        QSignalSpy          playback(&b, &SafeWallpaperBridge::sigMediaPlayback);
        QSignalSpy          timeline(&b, &SafeWallpaperBridge::sigMediaTimeline);
        QSignalSpy          paused(&b, &SafeWallpaperBridge::sigPaused);

        b.setMediaStatus(true);
        b.setMediaStatus(true);
        b.setMediaProperties({ { "title", "T" } });
        b.setMediaProperties({ { "title", "T" } });
        b.setMediaThumbnail({ { "thumbnail", "data:," } });
        b.setMediaPlayback(1);
        b.setMediaPlayback(1);
        b.setMediaTimeline({ { "position", 1.0 }, { "duration", 9.0 } });
        b.setMediaTimeline({ { "position", 1.0 }, { "duration", 9.0 } });
        b.setMediaTimeline({ { "position", 2.0 }, { "duration", 9.0 } });
        b.setPaused(true);
        b.setPaused(true);

        QCOMPARE(status.count(), 1);
        QCOMPARE(props.count(), 1);
        QCOMPARE(thumb.count(), 1);
        QCOMPARE(playback.count(), 1);
        QCOMPARE(timeline.count(), 2);
        QCOMPARE(paused.count(), 1);
        QCOMPARE(b.mediaProperties().value("title").toString(), QString("T"));
        QCOMPARE(b.mediaTimeline().value("position").toDouble(), 2.0);
        QCOMPARE(b.mediaPlayback(), 1);
        QCOMPARE(b.paused(), true);

        // The player went away: nothing of its track is left for a page
        // that connects later, and each field says so once.
        b.clearMedia();
        QCOMPARE(b.mediaStatus(), false);
        QCOMPARE(b.mediaPlayback(), 0);
        QVERIFY(b.mediaProperties().isEmpty());
        QVERIFY(b.mediaThumbnail().isEmpty());
        QVERIFY(b.mediaTimeline().isEmpty());
        QCOMPARE(status.count(), 2);
        QCOMPARE(props.count(), 2);
        QCOMPARE(thumb.count(), 2);
        QCOMPARE(playback.count(), 2);
        QCOMPARE(timeline.count(), 3);
        // Pausing belongs to the view, not to the player.
        QCOMPARE(b.paused(), true);
        b.clearMedia();
        QCOMPARE(status.count(), 2);
        QCOMPARE(timeline.count(), 3);
    }

    // The media properties' NOTIFY signals are the sig* names the injected
    // page script connects to, and carry no argument: QWebChannel sends a
    // NOTIFY's arguments next to the property values, so an argument would
    // send each cover twice.
    void media_notifySignalsAreArgumentLessSigSignals() {
        SafeWallpaperBridge b;
        const QMetaObject*  mo = b.metaObject();
        const QList<QPair<QByteArray, QByteArray>> expected {
            { "mediaStatus", "sigMediaStatus" },       { "mediaProperties", "sigMediaProperties" },
            { "mediaThumbnail", "sigMediaThumbnail" }, { "mediaPlayback", "sigMediaPlayback" },
            { "mediaTimeline", "sigMediaTimeline" },   { "paused", "sigPaused" },
        };
        for (const auto& [prop, sig] : expected) {
            const int i = mo->indexOfProperty(prop.constData());
            QVERIFY2(i >= 0, prop.constData());
            const QMetaProperty p = mo->property(i);
            QVERIFY2(p.hasNotifySignal(), prop.constData());
            QCOMPARE(p.notifySignal().name(), sig);
            QCOMPARE(p.notifySignal().parameterCount(), 0);
        }
    }

    // Regression: every Q_PROPERTY on the bridge must be READ-only. A WRITE
    // clause here is JS-reachable — QWebChannel's publisher dispatches an
    // inbound "setProperty" over the wire straight through QMetaProperty::write,
    // so a WRITE gives web-wallpaper JS a setter with no Q_INVOKABLE required
    // to grep for. (This is what shipped briefly: generalProperties/
    // userProperties/loaded gained WRITE clauses so *QML* could reach the
    // setters, which also reopened the JS-writable hole this class exists to
    // close.)
    void metaObject_allPropertiesAreReadOnly() {
        SafeWallpaperBridge b;
        const QMetaObject*  mo = b.metaObject();
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            QVERIFY2(! p.isWritable(),
                     qPrintable(QString("property '%1' is writable — web JS reaches it over "
                                        "QWebChannel")
                                    .arg(p.name())));
        }
    }

    // Regression: the bridge must not expose a QObject* property. The channel
    // marshals whatever QObject a reachable property points at, so if the
    // bridge ever grew a pointer back to SafeWallpaperBridgeController (or
    // anything else), the page could walk through it to invoke the
    // controller's Q_INVOKABLE setters — the exact hole this split is meant
    // to close.
    void metaObject_hasNoQObjectPointerProperty() {
        SafeWallpaperBridge b;
        const QMetaObject*  mo = b.metaObject();
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            QVERIFY2(! QByteArray(p.typeName()).contains('*'),
                     qPrintable(QString("property '%1' is a pointer type ('%2') — the "
                                        "QWebChannel-exposed bridge must not be reachable to "
                                        "any other QObject")
                                    .arg(p.name(), p.typeName())));
        }
    }

    // Defensive: confirm the meta-object has NO Q_INVOKABLE methods. A
    // future contributor adding one would silently widen the JS surface —
    // catch that at test-time, not in a security review.
    void metaObject_hasNoInvokableMethods() {
        SafeWallpaperBridge b;
        const QMetaObject*  mo             = b.metaObject();
        int                 invokableCount = 0;
        // Walk only the SafeWallpaperBridge slice (skip QObject base).
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() == QMetaMethod::Method) {
                // Q_INVOKABLE shows up as MethodType::Method; signals show
                // as MethodType::Signal; slots as MethodType::Slot.
                ++invokableCount;
                qWarning() << "SafeWallpaperBridge has Q_INVOKABLE method:" << m.methodSignature();
            }
        }
        QCOMPARE(invokableCount, 0);
    }
};

QTEST_MAIN(TestSafeWallpaperBridge)
#include "tst_safewallpaperbridge.moc"
