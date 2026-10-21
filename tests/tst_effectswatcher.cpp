// Unit tests for wekde::EffectsWatcher (we-manager unpause-overview patch):
// whether KWin shows Overview or Present Windows, read from KWin's
// org.kde.kwin.Effects activeEffects.  A fake org.kde.KWin on a private
// session bus (run under dbus-run-session) answers /Effects' Properties.Get
// from a list the test sets, can go away, and can stop answering.  With the
// name already taken (a real KWin on the bus) the bus cases skip: they must
// never ask a real compositor.
#include "EffectsWatcher.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest/QtTest>

using wekde::EffectsWatcher;

namespace
{
const QString kService = QStringLiteral("org.kde.KWin");

class FakeKWin : public QDBusVirtualObject {
public:
    QStringList active;
    bool        silent { false };   // hold every call without an answer
    int         gets { 0 };

    QString introspect(const QString&) const override {
        return QStringLiteral("<interface name=\"org.freedesktop.DBus.Properties\">"
                              "<method name=\"Get\"><arg direction=\"in\" type=\"s\"/>"
                              "<arg direction=\"in\" type=\"s\"/><arg direction=\"out\" type=\"v\"/></method>"
                              "</interface>");
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override {
        if (message.interface() != QStringLiteral("org.freedesktop.DBus.Properties")
            || message.member() != QStringLiteral("Get") || message.arguments().size() != 2)
            return false;
        if (message.arguments().at(0).toString() != QStringLiteral("org.kde.kwin.Effects")
            || message.arguments().at(1).toString() != QStringLiteral("activeEffects"))
            return false;
        ++gets;
        if (silent) return true;    // no reply: the caller's timeout runs out
        return connection.send(message.createReply(QVariant::fromValue(QDBusVariant(QVariant(active)))));
    }
};

// The fake on a connection of its own, owning org.kde.KWin.
class FakeKWinOnBus {
public:
    explicit FakeKWinOnBus(FakeKWin* fake)
        : m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-kwin"))) {
        if (! m_bus.isConnected()) return;
        m_object = m_bus.registerVirtualObject(QStringLiteral("/Effects"), fake);
        claim();
    }
    ~FakeKWinOnBus() {
        release();
        if (m_object) m_bus.unregisterObject(QStringLiteral("/Effects"));
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-kwin"));
    }
    bool ok() const { return m_object && m_owned; }
    void claim() { m_owned = m_bus.registerService(kService); }
    void release() {
        if (m_owned) m_bus.unregisterService(kService);
        m_owned = false;
    }

private:
    QDBusConnection m_bus;
    bool            m_object { false };
    bool            m_owned { false };
};
} // namespace

class TestEffectsWatcher : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void theListMeansOverviewOrPresentWindows();
    void notWatchingAsksNothing();
    void watchingFollowsTheList();
    void twoWatchersShareOnePoll();
    void kwinGoneIsUnavailableWithTheCallNamed_andBackAgain();
    void aSilentKwinGetsNoSecondCallWhileOneIsOut();
    void watchingOffStopsAsking();
    void aWatcherDeletedFromAnothersSignalIsNotTouched();
    void aDeadBusIsAFailedCall_andThePollGoesOn();
};

void TestEffectsWatcher::initTestCase() {
    QCOMPARE(EffectsWatcher::kPollMs, 500);
    EffectsWatcher::setBusForTest(QDBusConnection::sessionBus(), kService);
}

void TestEffectsWatcher::theListMeansOverviewOrPresentWindows() {
    QVERIFY(EffectsWatcher::listShowsOverview({ QStringLiteral("overview") }));
    QVERIFY(EffectsWatcher::listShowsOverview({ QStringLiteral("blur"), QStringLiteral("windowview") }));
    QVERIFY(! EffectsWatcher::listShowsOverview({ QStringLiteral("blur"), QStringLiteral("slide") }));
    QVERIFY(! EffectsWatcher::listShowsOverview({}));
    QVERIFY(! EffectsWatcher::listShowsOverview({ QStringLiteral("Overview") }));   // ids are lowercase
}

#define NEED_FAKE(onbus)                                                                                \
    if (! (onbus).ok())                                                                                 \
        QSKIP("org.kde.KWin is taken or there is no session bus: run under dbus-run-session, never "    \
              "against a real KWin")

void TestEffectsWatcher::notWatchingAsksNothing() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    EffectsWatcher w;
    QTest::qWait(3 * EffectsWatcher::kPollMs);
    QCOMPARE(fake.gets, 0);
    QCOMPARE(w.overviewActive(), false);
}

void TestEffectsWatcher::watchingFollowsTheList() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("blur") };
    EffectsWatcher w;
    QSignalSpy     spy(&w, &EffectsWatcher::overviewActiveChanged);
    w.setWatching(true);
    QTRY_VERIFY_WITH_TIMEOUT(fake.gets >= 1, 2000);   // asked at once, not after the first interval
    QCOMPARE(w.overviewActive(), false);
    QCOMPARE(w.available(), true);
    fake.active = { QStringLiteral("blur"), QStringLiteral("overview") };
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 2000);
    fake.active = { QStringLiteral("windowview") };
    QTest::qWait(2 * EffectsWatcher::kPollMs);
    QCOMPARE(w.overviewActive(), true);
    fake.active = {};
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), false, 2000);
    QCOMPARE(spy.count(), 2);
    w.setWatching(false);
}

void TestEffectsWatcher::twoWatchersShareOnePoll() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("overview") };
    EffectsWatcher a;
    a.setWatching(true);
    QTRY_COMPARE_WITH_TIMEOUT(a.overviewActive(), true, 2000);
    EffectsWatcher b;
    b.setWatching(true);
    QCOMPARE(b.overviewActive(), true);   // the poll that runs already answers for the new one
    const int before = fake.gets;
    QTest::qWait(4 * EffectsWatcher::kPollMs);
    // One poller: about four calls in four intervals, where two would make eight.
    QVERIFY2(fake.gets - before <= 5, qPrintable(QString::number(fake.gets - before) + " calls"));
    QVERIFY(fake.gets - before >= 2);
    a.setWatching(false);
    b.setWatching(false);
}

void TestEffectsWatcher::kwinGoneIsUnavailableWithTheCallNamed_andBackAgain() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("overview") };
    EffectsWatcher w;
    w.setWatching(true);
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 2000);
    onBus.release();                      // KWin restarts
    QTRY_COMPARE_WITH_TIMEOUT(w.available(), false, 3000);
    QCOMPARE(w.overviewActive(), false);
    QVERIFY2(w.reason().startsWith(QStringLiteral("org.kde.KWin /Effects org.kde.kwin.Effects.activeEffects failed (")),
             qPrintable(w.reason()));
    onBus.claim();
    QTRY_COMPARE_WITH_TIMEOUT(w.available(), true, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 2000);
    QCOMPARE(w.reason(), QString());
    w.setWatching(false);
}

void TestEffectsWatcher::aSilentKwinGetsNoSecondCallWhileOneIsOut() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.silent = true;
    EffectsWatcher w;
    w.setWatching(true);
    QTest::qWait(5 * EffectsWatcher::kPollMs);
    // Each call waits out kCallTimeoutMs (two intervals) before the next one.
    QVERIFY2(fake.gets <= 3, qPrintable(QString::number(fake.gets) + " calls in five intervals"));
    QTRY_COMPARE_WITH_TIMEOUT(w.available(), false, 2000);   // the timeout counts as a failure
    fake.silent = false;
    fake.active = { QStringLiteral("windowview") };
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 3000);
    QCOMPARE(w.available(), true);
    w.setWatching(false);
}

void TestEffectsWatcher::watchingOffStopsAsking() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("overview") };
    EffectsWatcher w;
    w.setWatching(true);
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 2000);
    w.setWatching(false);
    QCOMPARE(w.overviewActive(), false);   // not asked any more: no stale answer
    QTest::qWait(EffectsWatcher::kCallTimeoutMs);   // a call that was out comes back
    const int after = fake.gets;
    QTest::qWait(3 * EffectsWatcher::kPollMs);
    QCOMPARE(fake.gets, after);
    QCOMPARE(w.overviewActive(), false);
}

// A watcher deleted from another's change signal (its destructor takes it
// out of the poller's set) must not be handed the rest of that answer.  Each
// deletes the other, so whichever the poller reaches first does it; the
// other is skipped.  Run under ASan this is a heap-use-after-free if not.
void TestEffectsWatcher::aWatcherDeletedFromAnothersSignalIsNotTouched() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("blur") };
    auto*                    a = new EffectsWatcher;
    auto*                    b = new EffectsWatcher;
    QPointer<EffectsWatcher> pa(a), pb(b);
    connect(a, &EffectsWatcher::overviewActiveChanged, this, [&pb] { delete pb.data(); });
    connect(b, &EffectsWatcher::overviewActiveChanged, this, [&pa] { delete pa.data(); });
    a->setWatching(true);
    b->setWatching(true);
    QTRY_VERIFY_WITH_TIMEOUT(fake.gets >= 1, 2000);
    fake.active = { QStringLiteral("overview") };
    QTRY_VERIFY_WITH_TIMEOUT(pa.isNull() || pb.isNull(), 2000);
    QVERIFY(! (pa.isNull() && pb.isNull()));   // the first one deleted the other, and only that
    EffectsWatcher* left = pa.isNull() ? pb.data() : pa.data();
    QCOMPARE(left->overviewActive(), true);
    fake.active = {};
    QTRY_COMPARE_WITH_TIMEOUT(left->overviewActive(), false, 2000);
    delete left;
}

// A connection that is not connected gives a call that never finishes: it
// is a failed call, named, and the poll keeps asking (here a bus address
// nobody listens on, then the live bus again).
void TestEffectsWatcher::aDeadBusIsAFailedCall_andThePollGoesOn() {
    FakeKWin      fake;
    FakeKWinOnBus onBus(&fake);
    NEED_FAKE(onBus);
    fake.active = { QStringLiteral("overview") };
    struct Restore {
        ~Restore() { EffectsWatcher::setBusForTest(QDBusConnection::sessionBus(), kService); }
    } restore;
    const QDBusConnection dead =
        QDBusConnection::connectToBus(QStringLiteral("unix:path=/nonexistent/wek-test-bus"), QStringLiteral("dead"));
    QVERIFY(! dead.isConnected());
    EffectsWatcher::setBusForTest(dead, kService);
    EffectsWatcher w;
    const int before = EffectsWatcher::callsForTest();
    w.setWatching(true);
    QTRY_COMPARE_WITH_TIMEOUT(w.available(), false, 2000);
    QVERIFY2(w.reason().contains(QStringLiteral("the session bus is not connected")), qPrintable(w.reason()));
    QTRY_VERIFY_WITH_TIMEOUT(EffectsWatcher::callsForTest() - before >= 3, 3000);   // it keeps asking
    QCOMPARE(fake.gets, 0);
    EffectsWatcher::setBusForTest(QDBusConnection::sessionBus(), kService);
    QTRY_COMPARE_WITH_TIMEOUT(w.available(), true, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(w.overviewActive(), true, 2000);
    QVERIFY(fake.gets >= 1);
    w.setWatching(false);
    QDBusConnection::disconnectFromBus(QStringLiteral("dead"));
}

QTEST_GUILESS_MAIN(TestEffectsWatcher)
#include "tst_effectswatcher.moc"
