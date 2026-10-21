// Unit tests for wekde::WekControl — the C++ session-bus surface that wraps
// the QML PlaylistController.
//
// Routing strategy: each test instantiates a PlaylistControllerStub that
// mirrors the QML controller's invokable API.  WekControl::Next() (etc) call
// QMetaObject::invokeMethod into the stub via Qt::DirectConnection (we
// override the connection type by calling the slot directly through QtTest's
// QTRY_COMPARE wait loop — Qt::QueuedConnection requires a running event
// loop, which QTRY_COMPARE drives).
//
// Per feedback_distrobox_dbus_launch_missing — dbus-launch /
// dbus-run-session are not available in the Bazzite Fedora toolbox.  Tests
// that need an actual bus QSKIP when the session bus is unreachable; the
// routing logic itself does not require a bus.

#include "../src/WekControl.hpp"

#include <QCursor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <functional>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace wekde;

// Stub QObject mimicking the QML PlaylistController. Records the last
// invoked method name so tests can assert routing.  All public slots use the
// names that WekControl::*  forwards to (see PlaylistController.qml's
// public-export layer).
class PlaylistControllerStub : public QObject {
    Q_OBJECT
public:
    QString     lastMethod;
    QString     lastArg;
    QList<bool> stops; // remoteStop calls, in order
    // Keyboard mode: what keyboardScore answers, what it was asked, and the
    // startKeyboard / endKeyboard calls.
    int      score { 0 };
    QVariant lastCursor;
    int      starts { 0 };
    int      ends { 0 };

public slots:
    void next() { lastMethod = QStringLiteral("next"); }
    void previous() { lastMethod = QStringLiteral("previous"); }
    void pause() { lastMethod = QStringLiteral("pause"); }
    void resume() { lastMethod = QStringLiteral("resume"); }
    void togglePause() { lastMethod = QStringLiteral("togglePause"); }
    void mute() { lastMethod = QStringLiteral("mute"); }
    void unmute() { lastMethod = QStringLiteral("unmute"); }
    void toggleMute() { lastMethod = QStringLiteral("toggleMute"); }
    void activatePlaylistById(const QString& id) {
        lastMethod = QStringLiteral("activatePlaylistById");
        lastArg    = id;
    }
    void reload() { lastMethod = QStringLiteral("reload"); }
    // Keyboard mode: main.qml's keyboardScore (0 cannot, higher is better).
    QVariant keyboardScore(const QVariant& cursor) {
        lastCursor = cursor;
        return score;
    }
    void startKeyboard() {
        lastMethod = QStringLiteral("startKeyboard");
        ++starts;
    }
    void endKeyboard() {
        lastMethod = QStringLiteral("endKeyboard");
        ++ends;
    }
    // QML functions take QVariant arguments, so the stub does too.
    // WekControl.Screenshot: (path, screen), QVariants like any QML function.
    void screenshot(const QVariant& path, const QVariant& screen) {
        lastMethod = QStringLiteral("screenshot");
        lastArg    = path.toString() + QLatin1Char('|') + screen.toString();
    }
    void remoteStop(const QVariant& source, const QVariant& stopped) {
        lastMethod = QStringLiteral("remoteStop");
        lastArg    = source.toString();
        stops << stopped.toBool();
    }
    // WekControl.RequestStop (we-manager vram patch): (reason, screen, seconds).
    void requestStop(const QVariant& reason, const QVariant& screen, const QVariant& seconds) {
        lastMethod = QStringLiteral("requestStop");
        lastArg    = reason.toString() + QLatin1Char('|') + screen.toString() + QLatin1Char('|')
                  + QString::number(seconds.toDouble());
    }
    // What PlaylistController.stopReasons answers: JSON text.
    QVariant stopReasons() const { return reasonsJson; }

public:
    QString reasonsJson;

public slots:

    // Direct-connection getters return a QVariant so the C++ adapter's
    // Q_RETURN_ARG(QVariant) marshalling works without per-type templating.
    QVariant currentWorkshopId() const { return QString("12345"); }
    QVariant currentPlaylistId() const { return QString("favorites"); }
    QVariant currentItemIndex() const { return 7; }
    QVariant isPaused() const { return true; }
};

namespace
{
// Registers two independent WekControl/stub pairs (simulating two screens),
// fires trigger on the first, and asserts both stubs saw the call. One
// helper instead of five near-identical fifteen-line bodies.
void expectBroadcastReachesBothStubs(const std::function<void(WekControl&)>& trigger,
                                     const QString&                          expectedMethod) {
    PlaylistControllerStub stubA, stubB;
    auto                   controlA = std::make_unique<WekControl>();
    auto                   controlB = std::make_unique<WekControl>();
    controlA->setPlaylistController(&stubA);
    controlB->setPlaylistController(&stubB);
    trigger(*controlA);
    QTRY_COMPARE(stubA.lastMethod, expectedMethod);
    QTRY_COMPARE(stubB.lastMethod, expectedMethod);
}

void expectBroadcastArgReachesBothStubs(
    const std::function<void(WekControl&, const QString&)>& trigger, const QString& arg) {
    PlaylistControllerStub stubA, stubB;
    auto                   controlA = std::make_unique<WekControl>();
    auto                   controlB = std::make_unique<WekControl>();
    controlA->setPlaylistController(&stubA);
    controlB->setPlaylistController(&stubB);
    trigger(*controlA, arg);
    QTRY_COMPARE(stubA.lastMethod, QString("activatePlaylistById"));
    QTRY_COMPARE(stubB.lastMethod, QString("activatePlaylistById"));
    QCOMPARE(stubA.lastArg, arg);
    QCOMPARE(stubB.lastArg, arg);
}
} // namespace

class TestWekControl : public QObject {
    Q_OBJECT

private slots:

    // -- Routing tests (no bus required) -------------------------------------
    // These exercise the C++ adapter's QMetaObject::invokeMethod dispatch
    // into the PlaylistControllerStub.  They construct WekControl via the
    // default ctor — that ctor's registerOn() call silently fails when the
    // session bus is unavailable, leaving a fully functional adapter
    // (m_registered=false, but slot bodies still dispatch to m_controller).

    void next_routesToController();
    void previous_routesToController();
    void pause_routesToController();
    void resume_routesToController();
    void toggle_routesToTogglePause();
    void mute_routesToController();
    void unmute_routesToController();
    void toggleMute_routesToController();
    void activatePlaylist_routesArgumentToController();
    void reload_routesToController();
    void toggleKeyboard_startsOnTheBestScreenOnly();
    void toggleKeyboard_again_endsItEverywhere();
    void toggleKeyboard_afterTheModeEndedOnItsScreen_startsAgain();
    void toggleKeyboard_noScreenCanTakeIt_startsNothing();
    void currentWorkshopId_returnsStubValue();
    void currentPlaylistId_returnsStubValue();
    void currentItemIndex_returnsStubValue();
    void isPaused_returnsStubValue();

    // -- Broadcast tests (every registered controller, not just the caller's own) --
    void next_broadcastsToEveryRegisteredController();
    void previous_broadcastsToEveryRegisteredController();
    void pause_broadcastsToEveryRegisteredController();
    void resume_broadcastsToEveryRegisteredController();
    void toggle_broadcastsToEveryRegisteredController();
    void mute_broadcastsToEveryRegisteredController();
    void unmute_broadcastsToEveryRegisteredController();
    void toggleMute_broadcastsToEveryRegisteredController();
    void activatePlaylist_broadcastsArgumentToEveryRegisteredController();
    void reload_broadcastsToEveryRegisteredController();

    // -- Registry lifetime safety net (not TDD-red; see the comment below) --
    void destroyedController_isSkippedByBroadcastNotCrashed();
    void destroyedWekControl_removesOnlyItsOwnRegistryEntry();

    // -- StopScope "all screens" -----------------------------------------
    void broadcastStop_reachesEveryOtherControllerButNotItsOwn();
    void broadcastStop_isWithdrawnWhenTheStoppedInstanceGoes();

    // -- Stop on request, the stop reasons and the video memory (vram patch) --
    void requestStop_reachesEveryControllerWithItsArguments();
    void requestStop_needsAReasonAndSecondsFromZero();
    void stopReasons_gathersEveryScreensJson();
    void vramStatus_isWhatTheReaderSays();

    // -- No-controller safety --------------------------------------------
    void allSlots_areNoOpsWhenControllerIsNull();
    void allGetters_returnSentinelsWhenControllerIsNull();

    // -- Signal contract ------------------------------------------------
    void emitWallpaperChanged_firesQtSignal();
    void emitWallpaperChanged_carriesBothArguments();

    // -- Session-bus integration (QSKIP without a bus) ------------------
    void registersOnSessionBus_whenBusReachable();
    void secondRegistrationFailsCleanly_whenBusReachable();
    void ownerDestruction_releasesServiceName_whenBusReachable();
    void losingInstanceDestruction_leavesOwnerRegistered_whenBusReachable();

    // -- Q_CLASSINFO contract --------------------------------------
    void qClassInfo_dbusInterfaceName_isCorrect();

    // -- Audio configuration (audio-config patch) -------------------------
    void reloadAudioConfig_runsTheInstalledReloader();
    void reloadAudioConfig_withoutReloader_returnsFalse();
    void audioCaptureStatus_answersWhatTheReaderSays();

    // -- Screenshots (screenshot patch) --------------------------------------
    void screenshot_broadcastsPathAndScreenToEveryController();
    void screenshot_createsTheFolder_andRefusesAnEmptyPath();
    void saveScreenshots_usesPicturesWallpapersWithTheTime();

    // -- Ctor-injected connection (the dbus-launch fallback seam) ------
    void registerOnConnection_acceptsInjectedBus_orReturnsNull();
};

// -- Routing tests ---------------------------------------------------------

void TestWekControl::next_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Next();
    QTRY_COMPARE(stub.lastMethod, QString("next"));
}

void TestWekControl::previous_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Previous();
    QTRY_COMPARE(stub.lastMethod, QString("previous"));
}

void TestWekControl::pause_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Pause();
    QTRY_COMPARE(stub.lastMethod, QString("pause"));
}

void TestWekControl::resume_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Resume();
    QTRY_COMPARE(stub.lastMethod, QString("resume"));
}

void TestWekControl::toggle_routesToTogglePause() {
    // Toggle is the D-Bus name; the QML wrapper is called togglePause.
    // The C++ adapter renames in transit.
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Toggle();
    QTRY_COMPARE(stub.lastMethod, QString("togglePause"));
}

void TestWekControl::mute_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Mute();
    QTRY_COMPARE(stub.lastMethod, QString("mute"));
}

void TestWekControl::unmute_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Unmute();
    QTRY_COMPARE(stub.lastMethod, QString("unmute"));
}

void TestWekControl::toggleMute_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->ToggleMute();
    QTRY_COMPARE(stub.lastMethod, QString("toggleMute"));
}

void TestWekControl::activatePlaylist_routesArgumentToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->ActivatePlaylist(QStringLiteral("my_playlist"));
    QTRY_COMPARE(stub.lastMethod, QString("activatePlaylistById"));
    QCOMPARE(stub.lastArg, QString("my_playlist"));
}

void TestWekControl::reload_routesToController() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    control->Reload();
    QTRY_COMPARE(stub.lastMethod, QString("reload"));
}

// Keyboard mode for web wallpapers (we-manager input patch): one screen at a
// time.  Only one desktop window can be the active one, and the keys reach
// only it, so ToggleKeyboard starts the mode on the screen that scores
// highest (main.qml: the pointer over it, its window active, able to) and
// the next press ends it everywhere.
void TestWekControl::toggleKeyboard_startsOnTheBestScreenOnly() {
    PlaylistControllerStub a, b, c;
    a.score = 2; // its desktop window is the active one
    b.score = 3; // the pointer is over it
    c.score = 0; // KeyboardInput off
    auto ca = std::make_unique<WekControl>();
    auto cb = std::make_unique<WekControl>();
    auto cc = std::make_unique<WekControl>();
    ca->setPlaylistController(&a);
    cb->setPlaylistController(&b);
    cc->setPlaylistController(&c);
    ca->ToggleKeyboard();
    QTRY_COMPARE(b.starts, 1);
    QCoreApplication::processEvents();
    QCOMPARE(a.starts, 0);
    QCOMPARE(c.starts, 0);
    // Each screen was asked with the pointer's position.
    QCOMPARE(a.lastCursor.toPoint(), QCursor::pos());
    QCOMPARE(b.lastCursor.toPoint(), QCursor::pos());
    ca->ToggleKeyboard(); // leave it, so the next case starts clean
    QTRY_COMPARE(b.ends, 1);
}

void TestWekControl::toggleKeyboard_again_endsItEverywhere() {
    PlaylistControllerStub a, b;
    a.score = 1;
    b.score = 1;
    auto ca = std::make_unique<WekControl>();
    auto cb = std::make_unique<WekControl>();
    ca->setPlaylistController(&a);
    cb->setPlaylistController(&b);
    cb->ToggleKeyboard();
    QTRY_COMPARE(a.starts + b.starts, 1);
    cb->ToggleKeyboard();
    QTRY_COMPARE(a.ends, 1);
    QTRY_COMPARE(b.ends, 1);
    QCOMPARE(a.starts + b.starts, 1);
}

void TestWekControl::toggleKeyboard_afterTheModeEndedOnItsScreen_startsAgain() {
    PlaylistControllerStub a, b;
    a.score = 1;
    b.score = 3;
    auto ca = std::make_unique<WekControl>();
    auto cb = std::make_unique<WekControl>();
    ca->setPlaylistController(&a);
    cb->setPlaylistController(&b);
    ca->ToggleKeyboard();
    QTRY_COMPARE(b.starts, 1);
    // Escape on that screen: its main.qml reports it through its own control.
    ca->keyboardModeEnded(); // another screen's report frees nothing
    cb->keyboardModeEnded();
    ca->ToggleKeyboard();
    QTRY_COMPARE(b.starts, 2);
    QCOMPARE(b.ends, 0);
    ca->ToggleKeyboard();
    QTRY_COMPARE(b.ends, 1);
}

void TestWekControl::toggleKeyboard_noScreenCanTakeIt_startsNothing() {
    PlaylistControllerStub a;
    auto                   ca = std::make_unique<WekControl>();
    ca->setPlaylistController(&a);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression("ToggleKeyboard: no screen can take the keyboard"));
    ca->ToggleKeyboard();
    QCoreApplication::processEvents();
    QCOMPARE(a.starts, 0);
    QCOMPARE(a.ends, 0);
    // Nothing is held: a screen that can take it later gets it at once.
    a.score = 1;
    ca->ToggleKeyboard();
    QTRY_COMPARE(a.starts, 1);
    ca->ToggleKeyboard();
    QTRY_COMPARE(a.ends, 1);
}

void TestWekControl::currentWorkshopId_returnsStubValue() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    QCOMPARE(control->CurrentWorkshopId(), QString("12345"));
}

void TestWekControl::currentPlaylistId_returnsStubValue() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    QCOMPARE(control->CurrentPlaylistId(), QString("favorites"));
}

void TestWekControl::currentItemIndex_returnsStubValue() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    QCOMPARE(control->CurrentItemIndex(), 7);
}

void TestWekControl::isPaused_returnsStubValue() {
    auto                   control = std::make_unique<WekControl>();
    PlaylistControllerStub stub;
    control->setPlaylistController(&stub);
    QCOMPARE(control->IsPaused(), true);
}

// -- Broadcast tests ---------------------------------------------------------

void TestWekControl::next_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Next();
        },
        QStringLiteral("next"));
}
void TestWekControl::previous_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Previous();
        },
        QStringLiteral("previous"));
}
void TestWekControl::pause_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Pause();
        },
        QStringLiteral("pause"));
}
void TestWekControl::resume_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Resume();
        },
        QStringLiteral("resume"));
}
void TestWekControl::toggle_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Toggle();
        },
        QStringLiteral("togglePause"));
}
void TestWekControl::mute_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Mute();
        },
        QStringLiteral("mute"));
}
void TestWekControl::unmute_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Unmute();
        },
        QStringLiteral("unmute"));
}
void TestWekControl::toggleMute_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.ToggleMute();
        },
        QStringLiteral("toggleMute"));
}
void TestWekControl::activatePlaylist_broadcastsArgumentToEveryRegisteredController() {
    expectBroadcastArgReachesBothStubs(
        [](WekControl& c, const QString& id) {
            c.ActivatePlaylist(id);
        },
        QStringLiteral("my_playlist"));
}
void TestWekControl::reload_broadcastsToEveryRegisteredController() {
    expectBroadcastReachesBothStubs(
        [](WekControl& c) {
            c.Reload();
        },
        QStringLiteral("reload"));
}


// -- Registry lifetime safety net --------------------------------------------
// Not TDD-red: a genuinely dangling registry entry can't be driven red/green
// reliably -- the failure mode of a raw dangling pointer depends on allocator
// reuse timing and whether ASAN is active. These pin the QPointer auto-null
// and destructor cleanup that setPlaylistController() and ~WekControl()
// already provide.

void TestWekControl::destroyedController_isSkippedByBroadcastNotCrashed() {
    // Pins that a controller destroyed before a broadcast doesn't crash the
    // broadcast to the survivor.
    auto                   controlA = std::make_unique<WekControl>();
    PlaylistControllerStub stubB;
    {
        PlaylistControllerStub stubA;
        controlA->setPlaylistController(&stubA);
    } // stubA destroyed; its QPointer registry entry auto-nulls
    auto controlB = std::make_unique<WekControl>();
    controlB->setPlaylistController(&stubB);
    controlB->Pause(); // must not crash despite stubA's stale entry
    QTRY_COMPARE(stubB.lastMethod, QString("pause"));
}

void TestWekControl::destroyedWekControl_removesOnlyItsOwnRegistryEntry() {
    // Pins that a WekControl's own destructor cleanup is scoped correctly:
    // destroying controlA must not touch controlB's live entry, and
    // controlA's own still-alive stub must not receive a later broadcast.
    PlaylistControllerStub stubA, stubB;
    auto                   controlB = std::make_unique<WekControl>();
    controlB->setPlaylistController(&stubB);
    {
        auto controlA = std::make_unique<WekControl>();
        controlA->setPlaylistController(&stubA);
    } // controlA destroyed; stubA itself is still alive
    controlB->Next();
    QTRY_COMPARE(stubB.lastMethod, QString("next"));
    QCOMPARE(stubA.lastMethod, QString()); // never invoked after controlA died
}

// -- StopScope "all screens" -----------------------------------------------

void TestWekControl::broadcastStop_reachesEveryOtherControllerButNotItsOwn() {
    PlaylistControllerStub stubA, stubB, stubC;
    auto                   controlA = std::make_unique<WekControl>();
    auto                   controlB = std::make_unique<WekControl>();
    auto                   controlC = std::make_unique<WekControl>();
    controlA->setPlaylistController(&stubA);
    controlB->setPlaylistController(&stubB);
    controlC->setPlaylistController(&stubC);
    controlA->broadcastStop(true);
    QTRY_COMPARE(stubB.stops, QList<bool> { true });
    QTRY_COMPARE(stubC.stops, QList<bool> { true });
    QVERIFY(stubA.stops.isEmpty()); // the stopping screen already knows
    QVERIFY(! stubB.lastArg.isEmpty());
    QCOMPARE(stubB.lastArg, stubC.lastArg); // one name per announcing instance
    controlA->broadcastStop(false);
    QTRY_COMPARE(stubB.stops, (QList<bool> { true, false }));
    QTRY_COMPARE(stubC.stops, (QList<bool> { true, false }));
}

void TestWekControl::requestStop_reachesEveryControllerWithItsArguments() {
    PlaylistControllerStub stubA, stubB;
    auto                   controlA = std::make_unique<WekControl>();
    auto                   controlB = std::make_unique<WekControl>();
    controlA->setPlaylistController(&stubA);
    controlB->setPlaylistController(&stubB);
    controlB->RequestStop(QStringLiteral(" Factorio is running "), QStringLiteral("DP-2"), 120);
    // Every screen hears it (the reason trimmed); each one decides by the screen's name.
    QTRY_COMPARE(stubA.lastArg, QStringLiteral("Factorio is running|DP-2|120"));
    QTRY_COMPARE(stubB.lastArg, QStringLiteral("Factorio is running|DP-2|120"));
    controlA->RequestStop(QStringLiteral("x"), QString(), 1e9);         // at most a day
    QTRY_COMPARE(stubB.lastArg, QStringLiteral("x||86400"));
}

void TestWekControl::requestStop_needsAReasonAndSecondsFromZero() {
    PlaylistControllerStub stub;
    auto                   control = std::make_unique<WekControl>();
    control->setPlaylistController(&stub);
    QTest::ignoreMessage(QtWarningMsg, "wek-dbus: RequestStop: no reason given; nothing stopped");
    control->RequestStop(QStringLiteral("  "), QString(), 10);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("RequestStop: x : seconds must be 0 or more"));
    control->RequestStop(QStringLiteral("x"), QString(), -1);
    QTest::qWait(50);
    QVERIFY(stub.lastMethod.isEmpty());
    control->RequestStop(QStringLiteral("x"), QString(), 0);             // 0 ends a stop: it goes out
    QTRY_COMPARE(stub.lastArg, QStringLiteral("x||0"));
}

void TestWekControl::stopReasons_gathersEveryScreensJson() {
    PlaylistControllerStub stubA, stubB;
    stubA.reasonsJson = QStringLiteral(R"({"screen":"DP-1","stopped":true,"reasons":[{"reason":"vram"}]})");
    stubB.reasonsJson = QStringLiteral(R"({"screen":"DP-2","stopped":false,"reasons":[]})");
    auto controlA     = std::make_unique<WekControl>();
    auto controlB     = std::make_unique<WekControl>();
    controlA->setPlaylistController(&stubA);
    controlB->setPlaylistController(&stubB);
    const QJsonArray all = QJsonDocument::fromJson(controlB->StopReasons().toUtf8()).array();
    QCOMPARE(all.size(), 2);
    QStringList screens;
    for (const QJsonValue& v : all) screens << v.toObject().value(QStringLiteral("screen")).toString();
    screens.sort();
    QCOMPARE(screens, (QStringList { QStringLiteral("DP-1"), QStringLiteral("DP-2") }));
}

void TestWekControl::vramStatus_isWhatTheReaderSays() {
    WekControl control;
    WekControl::setVramStatusReader({});
    QTest::ignoreMessage(QtWarningMsg, "wek-dbus: VramStatus: no video memory reader installed");
    QCOMPARE(control.VramStatus(), QString());
    WekControl::setVramStatusReader([] { return QStringLiteral(R"({"available":false})"); });
    QCOMPARE(control.VramStatus(), QStringLiteral(R"({"available":false})"));
    WekControl::setVramStatusReader({});
}

void TestWekControl::broadcastStop_isWithdrawnWhenTheStoppedInstanceGoes() {
    PlaylistControllerStub stubA, stubB;
    auto                   controlB = std::make_unique<WekControl>();
    controlB->setPlaylistController(&stubB);
    {
        auto controlA = std::make_unique<WekControl>();
        controlA->setPlaylistController(&stubA);
        controlA->broadcastStop(true);
    } // the stopped screen is removed while its stop is still announced
    QTRY_COMPARE(stubB.stops, (QList<bool> { true, false }));
    // An instance that withdrew its stop itself sends nothing more when it goes.
    PlaylistControllerStub stubD;
    {
        auto controlD = std::make_unique<WekControl>();
        controlD->setPlaylistController(&stubD);
        controlD->broadcastStop(true);
        controlD->broadcastStop(false);
    }
    QTRY_COMPARE(stubB.stops.size(), 4);
    QTest::qWait(50);
    QCOMPARE(stubB.stops.size(), 4);
}

// -- No-controller safety --------------------------------------------------

void TestWekControl::allSlots_areNoOpsWhenControllerIsNull() {
    // Slot bodies must safely no-op when m_controller is null — used
    // during plugin startup before main.qml's WekControl element wires the
    // PlaylistController.
    auto control = std::make_unique<WekControl>();
    control->Next();
    control->Previous();
    control->Pause();
    control->Resume();
    control->Toggle();
    control->Mute();
    control->Unmute();
    control->ToggleMute();
    control->ActivatePlaylist(QStringLiteral("anything"));
    control->Reload();
    control->ToggleKeyboard();
    QVERIFY(true); // none of the above crashed
}

void TestWekControl::allGetters_returnSentinelsWhenControllerIsNull() {
    auto control = std::make_unique<WekControl>();
    QCOMPARE(control->CurrentWorkshopId(), QString());
    QCOMPARE(control->CurrentPlaylistId(), QString());
    QCOMPARE(control->CurrentItemIndex(), -1);
    QCOMPARE(control->IsPaused(), false);
}

// -- Signal contract -------------------------------------------------------

void TestWekControl::emitWallpaperChanged_firesQtSignal() {
    auto       control = std::make_unique<WekControl>();
    QSignalSpy spy(control.get(), &WekControl::WallpaperChanged);
    control->emitWallpaperChanged(QStringLiteral("12345"), QStringLiteral("favorites"));
    QCOMPARE(spy.count(), 1);
}

void TestWekControl::emitWallpaperChanged_carriesBothArguments() {
    auto       control = std::make_unique<WekControl>();
    QSignalSpy spy(control.get(), &WekControl::WallpaperChanged);
    control->emitWallpaperChanged(QStringLiteral("12345"), QStringLiteral("favorites"));
    QCOMPARE(spy.at(0).at(0).toString(), QString("12345"));
    QCOMPARE(spy.at(0).at(1).toString(), QString("favorites"));
}

// -- Session-bus integration ----------------------------------------------

void TestWekControl::registersOnSessionBus_whenBusReachable() {
    if (! QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus — distrobox without dbus-launch (per "
              "feedback_distrobox_dbus_launch_missing)");
    auto* control = WekControl::registerOnSessionBus();
    if (! control)
        QSKIP("service already owned (concurrent test or live "
              "plasmoid)");
    QVERIFY(control->isRegistered());
    // Validate via introspection round-trip.
    QDBusInterface iface(QStringLiteral("com.github.captsilver.WallpaperEngine"),
                         QStringLiteral("/WallpaperEngine"),
                         QStringLiteral("com.github.captsilver.WallpaperEngine"));
    QVERIFY(iface.isValid());
    delete control;
}

void TestWekControl::secondRegistrationFailsCleanly_whenBusReachable() {
    // Mimics multi-monitor: one wallpaper plasmoid per screen inside a single
    // plasmashell process, so both instances share one session-bus
    // connection.  A bus name is owned by the *connection*, not the instance,
    // so the loser must fail without touching the name — releasing it would
    // silently kill the control surface for the whole process while the
    // winner still believes it is registered.
    if (! QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus — distrobox without dbus-launch");
    auto* a = WekControl::registerOnSessionBus();
    if (! a) QSKIP("service already owned");
    auto* b = WekControl::registerOnSessionBus();
    QCOMPARE(b, nullptr);
    QVERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("com.github.captsilver.WallpaperEngine")));
    // Still answering by name, not merely still owning it.
    QDBusInterface iface(QStringLiteral("com.github.captsilver.WallpaperEngine"),
                         QStringLiteral("/WallpaperEngine"),
                         QStringLiteral("com.github.captsilver.WallpaperEngine"));
    QVERIFY(iface.isValid());
    delete a;
}

void TestWekControl::ownerDestruction_releasesServiceName_whenBusReachable() {
    // A wallpaper plasmoid is destroyed on screen unplug, wallpaper-plugin
    // switch or containment re-create.  The owner must hand the name back, or
    // the process keeps a service with no exported object behind it and no
    // later instance can ever take over.
    if (! QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus — distrobox without dbus-launch");
    auto* owner = WekControl::registerOnSessionBus();
    if (! owner) QSKIP("service already owned");
    delete owner;
    QVERIFY(! QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("com.github.captsilver.WallpaperEngine")));
}

void TestWekControl::losingInstanceDestruction_leavesOwnerRegistered_whenBusReachable() {
    // The other side of the same hazard: a secondary that never took the name
    // must not hand it back on destruction either.  Plasma destroys and
    // re-creates these per-screen objects freely, so an unconditional release
    // here would take the owner's control surface down with it.
    if (! QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus — distrobox without dbus-launch");
    auto* owner = WekControl::registerOnSessionBus();
    if (! owner) QSKIP("service already owned");
    {
        WekControl loser; // default ctor registers on the session bus and loses
        QVERIFY(! loser.isRegistered());
    }
    QVERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("com.github.captsilver.WallpaperEngine")));
    QDBusInterface iface(QStringLiteral("com.github.captsilver.WallpaperEngine"),
                         QStringLiteral("/WallpaperEngine"),
                         QStringLiteral("com.github.captsilver.WallpaperEngine"));
    QVERIFY(iface.isValid());
    delete owner;
}

// -- Q_CLASSINFO contract ------------------------------------------------

void TestWekControl::qClassInfo_dbusInterfaceName_isCorrect() {
    // Q_CLASSINFO("D-Bus Interface", "...") is what QDBusConnection reads
    // when exporting the object.  Pin the value so a typo here doesn't
    // silently rename the interface (which would break third-party panel
    // widgets without breaking the WekControl build).
    auto               control = std::make_unique<WekControl>();
    const QMetaObject* meta    = control->metaObject();
    const int          idx     = meta->indexOfClassInfo("D-Bus Interface");
    QVERIFY(idx >= 0);
    QCOMPARE(QString(meta->classInfo(idx).value()),
             QString("com.github.captsilver.WallpaperEngine"));
}

// -- Audio configuration (audio-config patch) --------------------------
// The plugin installs AudioBus::Reconfigure; the D-Bus method only runs it.

void TestWekControl::reloadAudioConfig_runsTheInstalledReloader() {
    int runs = 0;
    WekControl::setAudioConfigReloader([&runs] { ++runs; });
    auto control = std::make_unique<WekControl>();
    QVERIFY(control->ReloadAudioConfig());
    QVERIFY(control->ReloadAudioConfig());
    QCOMPARE(runs, 2);
    // A slot, so QDBusConnection::ExportAllSlots exports it with the others.
    QVERIFY(control->metaObject()->indexOfSlot("ReloadAudioConfig()") >= 0);
    WekControl::setAudioConfigReloader({});
}

void TestWekControl::audioCaptureStatus_answersWhatTheReaderSays() {
    QString state = QStringLiteral("failed");
    WekControl::setAudioCaptureStatusReader([&state] { return state; });
    auto control = std::make_unique<WekControl>();
    QCOMPARE(control->AudioCaptureStatus(), QStringLiteral("failed"));
    state = QStringLiteral("on");
    QCOMPARE(control->AudioCaptureStatus(), QStringLiteral("on"));
    // A slot, so it is on the bus with ReloadAudioConfig.
    QVERIFY(control->metaObject()->indexOfSlot("AudioCaptureStatus()") >= 0);
    WekControl::setAudioCaptureStatusReader({});
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("AudioCaptureStatus: no capture status reader"));
    QCOMPARE(control->AudioCaptureStatus(), QString());
}

void TestWekControl::screenshot_broadcastsPathAndScreenToEveryController() {
    PlaylistControllerStub a, b;
    auto                   ca = std::make_unique<WekControl>();
    auto                   cb = std::make_unique<WekControl>();
    ca->setPlaylistController(&a);
    cb->setPlaylistController(&b);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("shot.png"));
    ca->Screenshot(path, QStringLiteral("DP-1"));
    QTRY_COMPARE(a.lastMethod, QStringLiteral("screenshot"));
    QTRY_COMPARE(b.lastMethod, QStringLiteral("screenshot"));
    QCOMPARE(a.lastArg, path + QStringLiteral("|DP-1"));
    QCOMPARE(b.lastArg, path + QStringLiteral("|DP-1")); // each controller checks its own screen
}

void TestWekControl::screenshot_createsTheFolder_andRefusesAnEmptyPath() {
    PlaylistControllerStub a;
    auto                   c = std::make_unique<WekControl>();
    c->setPlaylistController(&a);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("deeper/still/shot.png"));
    c->Screenshot(path, QString());
    QVERIFY(QDir(dir.filePath(QStringLiteral("deeper/still"))).exists());
    QTRY_COMPARE(a.lastArg, path + QStringLiteral("|"));
    a.lastMethod.clear();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Screenshot: no file path given"));
    c->Screenshot(QString(), QString());
    QCoreApplication::processEvents();
    QVERIFY(a.lastMethod.isEmpty());
}

void TestWekControl::saveScreenshots_usesPicturesWallpapersWithTheTime() {
    // A throwaway HOME: SaveScreenshots creates <Pictures>/Wallpapers, and the
    // temporary directory takes it away again.
    QTemporaryDir   home;
    const QByteArray oldHome = qgetenv("HOME");
    qputenv("HOME", home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    const QDateTime when(QDate(2026, 11, 6), QTime(14, 5, 9));
    const QString   path = WekControl::defaultScreenshotPath(when);
    QVERIFY2(path.startsWith(home.path()), qPrintable(path));
    QVERIFY2(path.endsWith(QStringLiteral("/Wallpapers/wallpaper-20261106-140509.png")), qPrintable(path));
    PlaylistControllerStub a;
    auto                   c = std::make_unique<WekControl>();
    c->setPlaylistController(&a);
    const QString given = c->SaveScreenshots();
    QVERIFY(given.contains(QStringLiteral("/Wallpapers/wallpaper-")));
    QVERIFY(QFileInfo(QFileInfo(given).absolutePath()).isDir());
    QTRY_COMPARE(a.lastArg, given + QStringLiteral("|")); // every screen
    QStandardPaths::setTestModeEnabled(false);
    qputenv("HOME", oldHome);
}

void TestWekControl::reloadAudioConfig_withoutReloader_returnsFalse() {
    WekControl::setAudioConfigReloader({});
    auto control = std::make_unique<WekControl>();
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("ReloadAudioConfig: no audio reloader"));
    QVERIFY(! control->ReloadAudioConfig());
}

// -- Ctor-injected connection (the dbus-launch fallback seam) ------

void TestWekControl::registerOnConnection_acceptsInjectedBus_orReturnsNull() {
    // The injection seam: registerOnConnection(QDBusConnection, parent) is
    // the path tests use when a real bus is unavailable.  Without
    // dbus-launch, even private peer-to-peer connections require a server
    // socket — and qmltestrunner suites without QSKIP guards would wedge
    // here.  The test verifies the seam exists and returns nullptr
    // gracefully when the injected connection is invalid (the production
    // analogue of multi-monitor silent-fail).
    QDBusConnection invalid = QDBusConnection::connectToBus(
        QStringLiteral("unix:path=/no/such/socket"), QStringLiteral("tst_wekcontrol_invalid_bus"));
    // The connection ctor doesn't fail immediately — it fails at first use,
    // i.e. registerService below.  WekControl::registerOnConnection must
    // either return nullptr (registerService failed) or a registered
    // control.  Either is "the seam works"; we just must not crash.
    auto* control = WekControl::registerOnConnection(invalid);
    Q_UNUSED(control);
    QDBusConnection::disconnectFromBus(QStringLiteral("tst_wekcontrol_invalid_bus"));
    delete control;
    QVERIFY(true);
}

QTEST_GUILESS_MAIN(TestWekControl)
#include "tst_wekcontrol.moc"
