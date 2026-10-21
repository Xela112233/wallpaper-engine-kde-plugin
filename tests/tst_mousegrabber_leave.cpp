#include <QtTest>
#include <QCoreApplication>
#include <QEvent>
#include <QHoverEvent>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include "MouseGrabber.hpp"

// Fluid simulation (we-manager fluid-effect patch): the pointer hook passes a
// hover leave on to its target only when told to (hoverLeaveToTarget, which
// main.qml sets while the screen's FluidSimulation is on), and is otherwise
// the grabber it was.
//
// The real MouseGrabber in a Qt Quick window, placed as main.qml places it:
// in the desktop's item, under that item's other children, above the
// wallpaper's item, which is its target.  A fixed run of pointer events goes
// to the window (moves, the cursor leaving the window, moves again, a left
// and a right click, another leave), and everything the three items see is
// written down in order: the events the wallpaper's item and the desktop's
// item get, and for each event the grabber handles, whether it left it
// accepted.
//
// This file names the new property only as text, so it also builds against
// the grabber of the tree before the patch: `tst_mousegrabber_leave
// offIsTheGrabberOfBefore` there prints the same lines (TRANSCRIPT ...) as
// here, which is how the suite shows that off is what it was
// (tests/test_physics_fluid_simulation.py in we-manager).

namespace
{

QString nameOf(QEvent::Type type) {
    const char* key = QMetaEnum::fromType<QEvent::Type>().valueToKey(type);
    return key ? QString::fromLatin1(key) : QString::number(type);
}

bool told(QEvent::Type type) {
    switch (type) {
    case QEvent::HoverEnter:
    case QEvent::HoverMove:
    case QEvent::HoverLeave:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::MouseButtonDblClick: return true;
    default: return false;
    }
}

QString placeOf(QEvent* e) {
    QPointF p;
    if (e->type() == QEvent::HoverEnter || e->type() == QEvent::HoverMove || e->type() == QEvent::HoverLeave)
        p = static_cast<QHoverEvent*>(e)->position();
    else
        p = static_cast<QMouseEvent*>(e)->position();
    return QStringLiteral("%1,%2").arg(qRound(p.x())).arg(qRound(p.y()));
}

// An item that writes down the pointer events it gets and leaves them to
// QQuickItem.
class Witness : public QQuickItem {
public:
    Witness(QQuickItem* parent, QString name, QStringList* log): QQuickItem(parent), m_name(std::move(name)), m_log(log) {
        setAcceptHoverEvents(true);
    }

protected:
    bool event(QEvent* e) override {
        if (told(e->type())) m_log->append(m_name + QLatin1Char(' ') + nameOf(e->type()) + QLatin1Char(' ') + placeOf(e));
        return QQuickItem::event(e);
    }

private:
    QString      m_name;
    QStringList* m_log;
};

// The real grabber.  The overrides only write down what its own handlers
// (or, for the enter and the leave, QQuickItem's) did with the event.
class WatchedGrabber : public wekde::MouseGrabber {
public:
    WatchedGrabber(QQuickItem* parent, QStringList* log): wekde::MouseGrabber(parent), m_log(log) {}

protected:
    void note(QEvent* e) { m_log->append(QStringLiteral("grabber %1 accepted=%2").arg(nameOf(e->type())).arg(e->isAccepted())); }
    void hoverEnterEvent(QHoverEvent* e) override {
        wekde::MouseGrabber::hoverEnterEvent(e);
        note(e);
    }
    void hoverMoveEvent(QHoverEvent* e) override {
        wekde::MouseGrabber::hoverMoveEvent(e);
        note(e);
    }
    void hoverLeaveEvent(QHoverEvent* e) override {
        wekde::MouseGrabber::hoverLeaveEvent(e);
        note(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
        wekde::MouseGrabber::mousePressEvent(e);
        note(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        wekde::MouseGrabber::mouseMoveEvent(e);
        note(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        wekde::MouseGrabber::mouseReleaseEvent(e);
        note(e);
    }

private:
    QStringList* m_log;
};

enum class Leave
{
    Untouched, // the property is never written
    On,
    OnThenOff,
};

// The run of events, and what the three items saw.
QStringList run(Leave leave, bool withTarget = true) {
    QStringList  log;
    QQuickWindow window;
    window.resize(400, 300);
    auto* wallpaper = new Witness(window.contentItem(), QStringLiteral("wallpaper"), &log);
    wallpaper->setSize(QSizeF(400, 300));
    auto* desktop = new Witness(window.contentItem(), QStringLiteral("desktop"), &log); // a later sibling: above
    desktop->setSize(QSizeF(400, 300));
    auto* grabber = new WatchedGrabber(desktop, &log);
    grabber->setZ(-1);
    grabber->setSize(QSizeF(400, 300));
    if (withTarget) grabber->setTarget(wallpaper);
    if (leave != Leave::Untouched) {
        if (! grabber->setProperty("hoverLeaveToTarget", true)) return { QStringLiteral("no such property") };
        if (leave == Leave::OnThenOff) grabber->setProperty("hoverLeaveToTarget", false);
    }
    log.append(QStringLiteral("grabber takes buttons=%1 hover=%2 touch=%3")
                   .arg((int)grabber->acceptedMouseButtons())
                   .arg(grabber->acceptHoverEvents())
                   .arg(grabber->acceptTouchEvents()));
    window.show();
    if (! QTest::qWaitForWindowExposed(&window)) return { QStringLiteral("no window") };

    const auto leaveTheWindow = [&] {
        QEvent away(QEvent::Leave); // the cursor goes onto another window
        QCoreApplication::sendEvent(&window, &away);
        QCoreApplication::processEvents();
        log.append(QStringLiteral("-- the cursor left the window"));
    };
    for (const QPoint& p : { QPoint(50, 50), QPoint(120, 80), QPoint(200, 150) }) QTest::mouseMove(&window, p);
    QCoreApplication::processEvents();
    leaveTheWindow();
    for (const QPoint& p : { QPoint(300, 100), QPoint(310, 110) }) QTest::mouseMove(&window, p);
    QCoreApplication::processEvents();
    log.append(QStringLiteral("-- a left click"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(310, 110));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(310, 110));
    log.append(QStringLiteral("-- a right click"));
    QTest::mousePress(&window, Qt::RightButton, Qt::NoModifier, QPoint(310, 110));
    QTest::mouseRelease(&window, Qt::RightButton, Qt::NoModifier, QPoint(310, 110));
    QCoreApplication::processEvents();
    leaveTheWindow();
    return log;
}

int leavesAtTheWallpaper(const QStringList& log) {
    int n = 0;
    for (const QString& l : log) n += l.startsWith(QLatin1String("wallpaper HoverLeave")) ? 1 : 0;
    return n;
}

} // namespace

class TestMouseGrabberLeave : public QObject {
    Q_OBJECT

private slots:
    // Runs against the grabber of the tree before the patch too.
    void offIsTheGrabberOfBefore() {
        const QStringList log = run(Leave::Untouched);
        for (const QString& l : log) qInfo("TRANSCRIPT %s", qPrintable(l));
        QVERIFY(log.size() > 10);
        // The run is worth something: the wallpaper's item got moves from
        // the grabber, and Qt did tell the grabber of the leave.
        QVERIFY(log.contains(QStringLiteral("wallpaper HoverMove 200,150")));
        QVERIFY(log.contains(QStringLiteral("grabber HoverLeave accepted=0")));
        QCOMPARE(leavesAtTheWallpaper(log), 0);
        // And the same every time.
        QCOMPARE(run(Leave::Untouched), log);
    }

    void onTheLeaveReachesTheTargetAndNothingElseChanges() {
        const QStringList off = run(Leave::Untouched);
        const QStringList on  = run(Leave::On);
        QVERIFY2(on != QStringList { QStringLiteral("no such property") }, "MouseGrabber has no hoverLeaveToTarget");
        // Each time the cursor left the window, once, at the place it left
        // from, before the grabber's own handling of the leave.
        QCOMPARE(leavesAtTheWallpaper(on), 2);
        const int first = on.indexOf(QStringLiteral("wallpaper HoverLeave 200,150"));
        QVERIFY(first > 0);
        QCOMPARE(on.at(first + 1), QStringLiteral("grabber HoverLeave accepted=0"));
        QVERIFY(on.contains(QStringLiteral("wallpaper HoverLeave 310,110")));
        // Take the two lines away and it is the run with the property off.
        QStringList rest = on;
        rest.removeAll(QStringLiteral("wallpaper HoverLeave 200,150"));
        rest.removeAll(QStringLiteral("wallpaper HoverLeave 310,110"));
        QCOMPARE(rest, off);
    }

    void switchedOffAgainItIsTheGrabberOfBefore() {
        QCOMPARE(run(Leave::OnThenOff), run(Leave::Untouched));
    }

    void onWithoutATargetNothingIsSent() {
        const QStringList log = run(Leave::On, false);
        QVERIFY(log.contains(QStringLiteral("grabber HoverLeave accepted=0")));
        for (const QString& l : log) QVERIFY2(! l.startsWith(QLatin1String("wallpaper ")), qPrintable(l));
    }
};

QTEST_MAIN(TestMouseGrabberLeave)
#include "tst_mousegrabber_leave.moc"
