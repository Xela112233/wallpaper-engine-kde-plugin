// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Safe start after a crash (we-manager load-guard patch).
//
// Before a wallpaper item loads a backend it writes a guard: one small JSON
// file per item in ~/.config/wekde/loading/, naming the wallpaper it loads and
// the process that loads it (application name, pid, and the process's start
// time, so a pid used again by another process is not taken for it), and the
// boot it ran in (/proc/sys/kernel/random/boot_id).  The
// item removes the file once the wallpaper has drawn for a while, on a load
// failure, on a stop, and when the item goes away (~LoadGuard).  A guard
// whose process is no longer running names a wallpaper that was loading when
// that process ended without cleaning up: it crashed, or was killed.
//
// At start, an item about to load a wallpaper asks stopped(): the newest
// guard of a dead process of the same application (plasmashell, or the lock
// screen's kscreenlocker_greet) that names the same source and was written
// within the window it gives, in this boot.  Guards of a running process
// (another screen of this plasmashell loading right now) never count.  A
// guard from an earlier boot never counts either, however soon after the
// reboot the next start comes (a shutdown that did not tear the items
// down, then a quick autologin): it is deleted on the way, and so are dead
// processes' guards older than kKeepMs; until then we-manager's doctor
// lists them.
//
// Header-only and Qt Core only, so tests/tst_loadguard.cpp builds it alone.
// No fsync: a crash of the process keeps what it wrote to the kernel, and
// after a power loss the next start is minutes away, past any window the
// item uses; a flush on every wallpaper load would stall the shell's GUI
// thread on a busy disk.

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtDebug>

#include <atomic>
#include <cstdio>
#include <memory>
#include <utility>

namespace wekde
{

class LoadGuard {
public:
    // Dead processes' guards are deleted once they are this old.
    static constexpr qint64 kKeepMs = 7LL * 24 * 3600 * 1000;

    // `dir`: the folder of the guards; `name`: this item's file name there,
    // without ".json" (nextName() gives one no other item uses).
    LoadGuard(QString dir, QString name): m_dir(std::move(dir)), m_name(std::move(name)) {}
    ~LoadGuard() { clear(); }
    LoadGuard(const LoadGuard&)            = delete;
    LoadGuard& operator=(const LoadGuard&) = delete;

    QString path() const { return m_dir + QLatin1Char('/') + m_name + QStringLiteral(".json"); }

    // A file name for this process's next item: application, pid, a counter.
    static QString nextName() {
        static std::atomic<int> counter { 0 };
        return QStringLiteral("%1-%2-%3")
            .arg(appName())
            .arg(QCoreApplication::applicationPid())
            .arg(++counter);
    }

    static QString appName() {
        const QString a = QCoreApplication::applicationName();
        QString       out;
        for (const QChar c : a)
            out += (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.')) ? c
                                                                                             : QLatin1Char('_');
        return out.isEmpty() ? QStringLiteral("app") : out;
    }

    // Write this item's guard: the wallpaper it is about to load.  Written
    // to a temporary file and renamed, so a reader never sees half of it.
    bool mark(const QString& workshopId, const QString& source, qint64 nowMs) {
        if (! QDir().mkpath(m_dir)) {
            qWarning() << "LoadGuard: cannot create" << m_dir << "- safe start is off for this load";
            return false;
        }
        QJsonObject o;
        o[QStringLiteral("workshopid")] = workshopId;
        o[QStringLiteral("source")]     = source;
        o[QStringLiteral("time")]       = static_cast<double>(nowMs);
        o[QStringLiteral("app")]        = QCoreApplication::applicationName();
        o[QStringLiteral("pid")]        = static_cast<double>(QCoreApplication::applicationPid());
        o[QStringLiteral("start")]      = static_cast<double>(processStart(QCoreApplication::applicationPid()));
        o[QStringLiteral("boot")]       = bootId();
        const QByteArray bytes          = QJsonDocument(o).toJson(QJsonDocument::Compact);
        const QString    target         = path();
        const QString    tmp            = target + QStringLiteral(".tmp");
        QFile            f(tmp);
        if (! f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(bytes) != bytes.size()) {
            qWarning() << "LoadGuard: cannot write" << tmp << ":" << f.errorString()
                       << "- safe start is off for this load";
            f.close();
            QFile::remove(tmp);
            return false;
        }
        f.close();
        if (std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(target).constData()) != 0) {
            qWarning() << "LoadGuard: cannot rename" << tmp << "to" << target << "- safe start is off for this load";
            QFile::remove(tmp);
            return false;
        }
        m_marked = true;
        return true;
    }

    // Remove this item's guard (nothing happens when there is none).
    void clear() {
        if (! m_marked) return;
        m_marked = false;
        if (! QFile::remove(path()) && QFileInfo::exists(path()))
            qWarning() << "LoadGuard: cannot remove" << path()
                       << "- the next start may take this wallpaper for one that stopped Plasma";
    }
    bool marked() const { return m_marked; }

    // The newest guard in `dir` of a process of this application, in this
    // boot, that is no longer running, naming `source` and written at most
    // `withinMs` before `nowMs` (or as much after it, for a clock set back
    // since): its fields (workshopid, source, time, app, pid, boot, file),
    // else an empty map.
    static QVariantMap stopped(const QString& dir, const QString& source, qint64 withinMs, qint64 nowMs) {
        QVariantMap best;
        if (source.isEmpty()) return best;
        const QString app  = QCoreApplication::applicationName();
        const QString boot = bootId();
        const QDir    d(dir);
        const auto    files = d.entryInfoList(QStringList { QStringLiteral("*.json") }, QDir::Files);
        for (const QFileInfo& fi : files) {
            QFile f(fi.filePath());
            if (! f.open(QIODevice::ReadOnly)) continue;
            const QJsonObject o    = QJsonDocument::fromJson(f.read(64 * 1024)).object();
            f.close();
            const qint64      time = static_cast<qint64>(o.value(QStringLiteral("time")).toDouble(-1));
            const qint64      pid  = static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble(-1));
            if (time < 0 || pid <= 0) continue;                       // not a guard
            // Another boot: its process is gone, and its pid and start time
            // mean nothing in this one.
            const QString theirs = o.value(QStringLiteral("boot")).toString();
            if (! boot.isEmpty() && ! theirs.isEmpty() && theirs != boot) {
                QFile::remove(fi.filePath());
                continue;
            }
            if (running(pid, static_cast<qint64>(o.value(QStringLiteral("start")).toDouble(-1)))) continue;
            const qint64 age = nowMs - time;
            if (age > kKeepMs) {
                QFile::remove(fi.filePath());
                continue;
            }
            if (o.value(QStringLiteral("app")).toString() != app) continue;
            if (o.value(QStringLiteral("source")).toString() != source) continue;
            if (age > withinMs || age < -withinMs) continue;
            if (! best.isEmpty() && best.value(QStringLiteral("time")).toLongLong() >= time) continue;
            best = o.toVariantMap();
            best[QStringLiteral("time")] = time;
            best[QStringLiteral("pid")]  = pid;
            best[QStringLiteral("file")] = fi.filePath();
        }
        return best;
    }

    // This boot's id (/proc/sys/kernel/random/boot_id), "" when it cannot
    // be read; then guards are not told apart by boot.
    static QString bootId() {
        QFile f(QStringLiteral("/proc/sys/kernel/random/boot_id"));
        if (! f.open(QIODevice::ReadOnly)) return {};
        return QString::fromLatin1(f.readAll()).trimmed();
    }

    // Field 22 of /proc/PID/stat (the start time, in clock ticks after
    // boot), else -1.  A zombie counts as gone.
    static qint64 processStart(qint64 pid) {
        QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
        if (! f.open(QIODevice::ReadOnly)) return -1;
        const QByteArray s     = f.readAll();
        const int        close = s.lastIndexOf(')');
        if (close < 0) return -1;
        const QList<QByteArray> fields = s.mid(close + 2).split(' ');   // fields[0] is field 3, the state
        if (fields.size() < 20 || fields[0] == "Z" || fields[0] == "X") return -1;
        bool         ok = false;
        const qint64 v  = fields[19].toLongLong(&ok);
        return ok ? v : -1;
    }
    static bool running(qint64 pid, qint64 start) {
        if (pid <= 0) return false;
        const qint64 now = processStart(pid);
        return now >= 0 && now == start;
    }

private:
    QString m_dir;
    QString m_name;
    bool    m_marked { false };
};

} // namespace wekde
