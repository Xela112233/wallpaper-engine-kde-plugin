// SPDX-License-Identifier: GPL-2.0-only
// Unit tests for wekde::LoadGuard (safe start after a crash, we-manager
// load-guard patch): a guard is written before a load and removed after it,
// and only a guard that a process of this application left behind when it
// ended names a wallpaper to hold back.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <sys/wait.h>
#include <unistd.h>

#include "LoadGuard.hpp"

using namespace wekde;

namespace
{
const QString kSource = QStringLiteral("file:///lib/steamapps/workshop/content/431960/111/scene.pkg+scene");
constexpr qint64 kNow    = 1'800'000'000'000;
constexpr qint64 kWindow = 120'000;

// A pid no process has: a child that has exited and been collected.
qint64 deadPid() {
    const pid_t c = ::fork();
    if (c == 0) ::_exit(0);
    int status = 0;
    ::waitpid(c, &status, 0);
    return c;
}

QJsonObject readJson(const QString& path) {
    QFile f(path);
    if (! f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeGuard(const QString& dir, const QString& name, const QJsonObject& o) {
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".json"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson());
}

QJsonObject guard(qint64 pid, qint64 start, qint64 time, const QString& source = kSource,
                  const QString& app = QCoreApplication::applicationName(), const QString& boot = LoadGuard::bootId()) {
    QJsonObject o;
    o[QStringLiteral("workshopid")] = QStringLiteral("111");
    o[QStringLiteral("source")]     = source;
    o[QStringLiteral("time")]       = static_cast<double>(time);
    o[QStringLiteral("app")]        = app;
    o[QStringLiteral("pid")]        = static_cast<double>(pid);
    o[QStringLiteral("start")]      = static_cast<double>(start);
    o[QStringLiteral("boot")]       = boot;
    return o;
}
} // namespace

class TestLoadGuard : public QObject {
    Q_OBJECT

private slots:
    void markWritesTheWallpaperAndTheProcess();
    void clearAndTheDestructorRemoveIt();
    void aRunningProcessesGuardNeverCounts();
    void aDeadProcessesFreshGuardForTheSourceCounts();
    void onlyTheSameSourceAppAndWindowCount();
    void aReusedPidIsADeadProcess();
    void oldGuardsOfDeadProcessesAreDeleted();
    void namesDifferPerItem();
    void aGuardFromAnotherBootNeverCounts();
};

void TestLoadGuard::markWritesTheWallpaperAndTheProcess() {
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/loading");
    LoadGuard     g(dir, QStringLiteral("item"));
    QVERIFY(g.mark(QStringLiteral("111"), kSource, kNow));        // makes the folder
    const QJsonObject o = readJson(g.path());
    QCOMPARE(o.value(QStringLiteral("workshopid")).toString(), QStringLiteral("111"));
    QCOMPARE(o.value(QStringLiteral("source")).toString(), kSource);
    QCOMPARE(static_cast<qint64>(o.value(QStringLiteral("time")).toDouble()), kNow);
    QCOMPARE(o.value(QStringLiteral("app")).toString(), QCoreApplication::applicationName());
    QCOMPARE(static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble()), QCoreApplication::applicationPid());
    const qint64 start = static_cast<qint64>(o.value(QStringLiteral("start")).toDouble());
    QVERIFY(start > 0);
    QVERIFY(LoadGuard::running(QCoreApplication::applicationPid(), start));
    QVERIFY(! LoadGuard::bootId().isEmpty());
    QCOMPARE(o.value(QStringLiteral("boot")).toString(), LoadGuard::bootId());
    QVERIFY(! QFile::exists(g.path() + QStringLiteral(".tmp")));
    // The next load replaces it.
    QVERIFY(g.mark(QStringLiteral("222"), QStringLiteral("file:///other+video"), kNow + 5));
    QCOMPARE(readJson(g.path()).value(QStringLiteral("workshopid")).toString(), QStringLiteral("222"));
    QCOMPARE(QDir(dir).entryList(QDir::Files).size(), 1);
}

void TestLoadGuard::clearAndTheDestructorRemoveIt() {
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/loading");
    QString       path;
    {
        LoadGuard g(dir, QStringLiteral("a"));
        QVERIFY(g.mark(QStringLiteral("111"), kSource, kNow));
        path = g.path();
        g.clear();
        QVERIFY(! QFile::exists(path));
        QVERIFY(! g.marked());
        QVERIFY(g.mark(QStringLiteral("111"), kSource, kNow));
        QVERIFY(QFile::exists(path));
    }
    QVERIFY2(! QFile::exists(path), "an item that goes away (a clean exit) must not leave its guard");
    // A guard it never wrote is not touched: a dead process's file of the same name stays.
    writeGuard(dir, QStringLiteral("b"), guard(deadPid(), 1, kNow));
    {
        LoadGuard g(dir, QStringLiteral("b"));
        g.clear();
    }
    QVERIFY(QFile::exists(dir + QStringLiteral("/b.json")));
}

void TestLoadGuard::aRunningProcessesGuardNeverCounts() {
    QTemporaryDir tmp;
    const QString dir = tmp.path() + QStringLiteral("/loading");
    LoadGuard     g(dir, QStringLiteral("screen1"));
    QVERIFY(g.mark(QStringLiteral("111"), kSource, kNow));
    // Another screen of the same, running process is loading it right now.
    QVERIFY(LoadGuard::stopped(dir, kSource, kWindow, kNow + 1000).isEmpty());
}

void TestLoadGuard::aDeadProcessesFreshGuardForTheSourceCounts() {
    QTemporaryDir tmp;
    const QString dir  = tmp.path() + QStringLiteral("/loading");
    const qint64  dead = deadPid();
    QVERIFY(! LoadGuard::running(dead, 1));
    writeGuard(dir, QStringLiteral("old"), guard(dead, 1, kNow - 90'000));
    writeGuard(dir, QStringLiteral("new"), guard(dead, 1, kNow - 10'000));
    const QVariantMap m = LoadGuard::stopped(dir, kSource, kWindow, kNow);
    QCOMPARE(m.value(QStringLiteral("time")).toLongLong(), kNow - 10'000);      // the newest
    QCOMPARE(m.value(QStringLiteral("workshopid")).toString(), QStringLiteral("111"));
    QCOMPARE(m.value(QStringLiteral("pid")).toLongLong(), dead);
    QCOMPARE(m.value(QStringLiteral("file")).toString(), dir + QStringLiteral("/new.json"));
    // Asking does not remove it: every screen that shows the wallpaper asks.
    QVERIFY(! LoadGuard::stopped(dir, kSource, kWindow, kNow).isEmpty());
    // A clock set back a little since the crash still finds it.
    QVERIFY(! LoadGuard::stopped(dir, kSource, kWindow, kNow - 30'000).isEmpty());
}

void TestLoadGuard::onlyTheSameSourceAppAndWindowCount() {
    QTemporaryDir tmp;
    const QString dir  = tmp.path() + QStringLiteral("/loading");
    const qint64  dead = deadPid();
    writeGuard(dir, QStringLiteral("stale"), guard(dead, 1, kNow - kWindow - 1));
    writeGuard(dir, QStringLiteral("other"), guard(dead, 1, kNow, QStringLiteral("file:///x/222/index.html+web")));
    writeGuard(dir, QStringLiteral("greeter"), guard(dead, 1, kNow, kSource, QStringLiteral("kscreenlocker_greet")));
    writeGuard(dir, QStringLiteral("broken"), QJsonObject { { QStringLiteral("source"), kSource } });
    QVERIFY(LoadGuard::stopped(dir, kSource, kWindow, kNow).isEmpty());
    QVERIFY(LoadGuard::stopped(dir, QString(), kWindow, kNow).isEmpty());
    QVERIFY(! LoadGuard::stopped(dir, QStringLiteral("file:///x/222/index.html+web"), kWindow, kNow).isEmpty());
    QVERIFY(LoadGuard::stopped(tmp.path() + QStringLiteral("/none"), kSource, kWindow, kNow).isEmpty());
}

void TestLoadGuard::aReusedPidIsADeadProcess() {
    QTemporaryDir tmp;
    const QString dir   = tmp.path() + QStringLiteral("/loading");
    const qint64  pid   = QCoreApplication::applicationPid();
    const qint64  start = LoadGuard::processStart(pid);
    QVERIFY(start > 0);
    // Our pid, but another start time: the process that wrote it has ended.
    writeGuard(dir, QStringLiteral("before"), guard(pid, start + 1, kNow));
    QVERIFY(! LoadGuard::stopped(dir, kSource, kWindow, kNow).isEmpty());
}

void TestLoadGuard::oldGuardsOfDeadProcessesAreDeleted() {
    QTemporaryDir tmp;
    const QString dir  = tmp.path() + QStringLiteral("/loading");
    const qint64  dead = deadPid();
    writeGuard(dir, QStringLiteral("week"), guard(dead, 1, kNow - LoadGuard::kKeepMs - 1));
    writeGuard(dir, QStringLiteral("hour"), guard(dead, 1, kNow - 3'600'000));
    LoadGuard live(dir, QStringLiteral("live"));
    QVERIFY(live.mark(QStringLiteral("111"), kSource, kNow - LoadGuard::kKeepMs - 1));
    QVERIFY(LoadGuard::stopped(dir, kSource, kWindow, kNow).isEmpty());
    QVERIFY(! QFile::exists(dir + QStringLiteral("/week.json")));
    QVERIFY2(QFile::exists(dir + QStringLiteral("/hour.json")), "kept for the doctor until it is a week old");
    QVERIFY2(QFile::exists(live.path()), "a running process's guard is never deleted");
}

void TestLoadGuard::namesDifferPerItem() {
    const QString a = LoadGuard::nextName();
    const QString b = LoadGuard::nextName();
    QVERIFY(a != b);
    QVERIFY(a.contains(QString::number(QCoreApplication::applicationPid())));
    QVERIFY(! a.contains(QLatin1Char('/')));
}

// A shutdown that did not tear the items down, then a reboot and a login
// within the window: the guard's boot is not this one, so it never holds the
// wallpaper back, and it goes.  A guard of this boot still counts.
void TestLoadGuard::aGuardFromAnotherBootNeverCounts() {
    QTemporaryDir tmp;
    const QString dir  = tmp.path() + QStringLiteral("/loading");
    const qint64  dead = deadPid();
    writeGuard(dir, QStringLiteral("before-reboot"), guard(dead, 1, kNow - 30'000, kSource,
                                                         QCoreApplication::applicationName(),
                                                         QStringLiteral("00000000-0000-4000-8000-000000000000")));
    QVERIFY(LoadGuard::stopped(dir, kSource, kWindow, kNow).isEmpty());
    QVERIFY(! QFile::exists(dir + QStringLiteral("/before-reboot.json")));
    writeGuard(dir, QStringLiteral("this-boot"), guard(dead, 1, kNow - 30'000));
    QCOMPARE(LoadGuard::stopped(dir, kSource, kWindow, kNow).value(QStringLiteral("boot")).toString(),
             LoadGuard::bootId());
}

QTEST_GUILESS_MAIN(TestLoadGuard)
#include "tst_loadguard.moc"
