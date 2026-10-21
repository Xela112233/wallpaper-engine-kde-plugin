#include <QtTest>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include "KeyGrabber.hpp"
#include "MouseGrabber.hpp"

// Keyboard mode for web wallpapers (we-manager patch): KeyGrabber hands the
// desktop's keys to the page's input item while active.

class TestableKeyGrabber : public wekde::KeyGrabber {
public:
    using wekde::KeyGrabber::inputMethodEvent;
    using wekde::KeyGrabber::KeyGrabber;
    using wekde::KeyGrabber::keyPressEvent;
    using wekde::KeyGrabber::keyReleaseEvent;
};

// Records the key, input-method and focus events sent to the item it is
// installed on, and consumes them.
class KeyCapture : public QObject {
public:
    QList<QEvent::Type> types;
    QList<int>          keys;
    QStringList         texts;

    void clear() {
        types.clear();
        keys.clear();
        texts.clear();
    }

    bool eventFilter(QObject*, QEvent* ev) override {
        switch (ev->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            auto* ke = static_cast<QKeyEvent*>(ev);
            types << ev->type();
            keys << ke->key();
            texts << ke->text();
            return true;
        }
        case QEvent::InputMethod: {
            types << ev->type();
            keys << 0;
            texts << static_cast<QInputMethodEvent*>(ev)->commitString();
            return true;
        }
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            types << ev->type();
            keys << 0;
            texts << QString();
            return true;
        default: return false;
        }
    }
};

static QKeyEvent key(QEvent::Type type, int k, const QString& text = QString()) {
    return QKeyEvent(type, k, Qt::NoModifier, text);
}

class TestKeyGrabber : public QObject {
    Q_OBJECT
private slots:
    void inactive_ignoresKeys();
    void active_forwardsPressAndRelease();
    void escape_requestsExit_andIsNotForwarded();
    void activation_tellsTargetItHasTheFocus();
    void retarget_whileActive_movesTheFocus();
    void inputMethodText_isForwarded();
    void inWindow_takesTheFocus_andGivesItBack();
    void inWindow_focusTakenByAnotherItem_requestsExit();
    void inWindow_clickOnThePage_keepsTheMode();
    void inWindow_escapeAfterAPriorClickOnThePage_givesTheDesktopTheKeys();
    void destroyedTarget_noCrash();
};

void TestKeyGrabber::inactive_ignoresKeys() {
    TestableKeyGrabber g;
    QQuickItem         target;
    KeyCapture         cap;
    target.installEventFilter(&cap);
    g.setTarget(&target);
    auto ev = key(QEvent::KeyPress, Qt::Key_A, "a");
    ev.accept();
    g.keyPressEvent(&ev);
    QVERIFY(! ev.isAccepted());
    QCOMPARE(cap.types.size(), 0);
}

void TestKeyGrabber::active_forwardsPressAndRelease() {
    TestableKeyGrabber g;
    QQuickItem         target;
    KeyCapture         cap;
    target.installEventFilter(&cap);
    g.setTarget(&target);
    g.setActive(true);
    cap.clear(); // the FocusIn of the activation
    auto down = key(QEvent::KeyPress, Qt::Key_A, "a");
    down.ignore();
    g.keyPressEvent(&down);
    auto up = key(QEvent::KeyRelease, Qt::Key_A, "a");
    g.keyReleaseEvent(&up);
    QCOMPARE(cap.types, (QList<QEvent::Type> { QEvent::KeyPress, QEvent::KeyRelease }));
    QCOMPARE(cap.keys.at(0), int(Qt::Key_A));
    QCOMPARE(cap.texts.at(0), QString("a"));
    // The desktop does not act on keys the wallpaper took.
    QVERIFY(down.isAccepted());
    QVERIFY(up.isAccepted());
}

void TestKeyGrabber::escape_requestsExit_andIsNotForwarded() {
    TestableKeyGrabber g;
    QQuickItem         target;
    KeyCapture         cap;
    target.installEventFilter(&cap);
    g.setTarget(&target);
    g.setActive(true);
    cap.clear(); // the FocusIn of the activation
    QSignalSpy exit(&g, &wekde::KeyGrabber::exitRequested);
    auto       esc = key(QEvent::KeyPress, Qt::Key_Escape);
    g.keyPressEvent(&esc);
    auto escUp = key(QEvent::KeyRelease, Qt::Key_Escape);
    g.keyReleaseEvent(&escUp);
    QCOMPARE(exit.count(), 1);
    QCOMPARE(cap.types.size(), 0);
    QVERIFY(esc.isAccepted());
}

void TestKeyGrabber::activation_tellsTargetItHasTheFocus() {
    wekde::KeyGrabber g;
    QQuickItem        target;
    KeyCapture        cap;
    target.installEventFilter(&cap);
    g.setTarget(&target);
    QSignalSpy active(&g, &wekde::KeyGrabber::activeChanged);
    g.setActive(true);
    g.setActive(true); // no change, no second FocusIn
    g.setActive(false);
    QCOMPARE(cap.types, (QList<QEvent::Type> { QEvent::FocusIn, QEvent::FocusOut }));
    QCOMPARE(active.count(), 2);
}

void TestKeyGrabber::retarget_whileActive_movesTheFocus() {
    wekde::KeyGrabber g;
    QQuickItem        a, b;
    KeyCapture        capA, capB;
    a.installEventFilter(&capA);
    b.installEventFilter(&capB);
    g.setTarget(&a);
    g.setActive(true);
    g.setTarget(&b); // a new wallpaper page while in keyboard mode
    QCOMPARE(capA.types, (QList<QEvent::Type> { QEvent::FocusIn, QEvent::FocusOut }));
    QCOMPARE(capB.types, (QList<QEvent::Type> { QEvent::FocusIn }));
}

void TestKeyGrabber::inputMethodText_isForwarded() {
    TestableKeyGrabber g;
    QQuickItem         target;
    KeyCapture         cap;
    target.installEventFilter(&cap);
    g.setTarget(&target);
    QVERIFY(g.flags() & QQuickItem::ItemAcceptsInputMethod);
    g.setActive(true);
    cap.clear(); // the FocusIn of the activation
    QInputMethodEvent im(QString(), {});
    im.setCommitString(QStringLiteral("é"));
    g.inputMethodEvent(&im);
    QCOMPARE(cap.types, (QList<QEvent::Type> { QEvent::InputMethod }));
    QCOMPARE(cap.texts.at(0), QStringLiteral("é"));
}

void TestKeyGrabber::inWindow_takesTheFocus_andGivesItBack() {
    QQuickWindow      w;
    QQuickItem        icons(w.contentItem()); // Folder View had the focus
    wekde::KeyGrabber g(w.contentItem());
    QQuickItem        target(w.contentItem());
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    icons.forceActiveFocus();
    QVERIFY(icons.hasActiveFocus());
    QSignalSpy exit(&g, &wekde::KeyGrabber::exitRequested);
    g.setTarget(&target);
    g.setActive(true);
    QVERIFY(g.hasActiveFocus());
    g.setActive(false);
    QVERIFY(icons.hasActiveFocus());
    QCOMPARE(exit.count(), 0);
}

void TestKeyGrabber::inWindow_focusTakenByAnotherItem_requestsExit() {
    QQuickWindow      w;
    QQuickItem        icons(w.contentItem());
    wekde::KeyGrabber g(w.contentItem());
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    g.setActive(true);
    QVERIFY(g.hasActiveFocus());
    QSignalSpy exit(&g, &wekde::KeyGrabber::exitRequested);
    icons.forceActiveFocus(Qt::MouseFocusReason); // a click on an icon
    QCOMPARE(exit.count(), 1);
}

// Qt WebEngine's input item (RenderWidgetHostViewQtDelegateItem) takes the
// focus on a press when its WebEngineView's activeFocusOnPress is true, which
// QtWebView.qml getMouseTarget() sets for the mouse hook.
class FocusOnPressItem : public QQuickItem {
public:
    explicit FocusOnPressItem(QQuickItem* parent): QQuickItem(parent) { setAcceptedMouseButtons(Qt::AllButtons); }
    int keys { 0 };

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (parentItem() && parentItem()->property("activeFocusOnPress").toBool()) forceActiveFocus();
        e->accept();
    }
    void keyPressEvent(QKeyEvent* e) override {
        ++keys;
        e->accept();
    }
};

// The desktop as main.qml builds it: the web wallpaper (a WebEngineView and
// its input item) under the AppletsLayout, with the mouse and key hooks on it.
static QQuickItem* focusScope(QQuickItem* parent) {
    auto* item = new QQuickItem;
    item->setFlag(QQuickItem::ItemIsFocusScope);
    item->setParentItem(parent);
    return item;
}

struct Desktop {
    QQuickWindow        w;
    QQuickItem*         wallpaper { new QQuickItem(w.contentItem()) };
    QQuickItem*         web { new QQuickItem(wallpaper) };
    FocusOnPressItem*   page { new FocusOnPressItem(web) };
    QQuickItem*         applets { focusScope(w.contentItem()) };
    wekde::MouseGrabber* mouse { new wekde::MouseGrabber(applets) };
    wekde::KeyGrabber*  keys { new wekde::KeyGrabber(applets) };
    Desktop() {
        w.resize(400, 300);
        for (QQuickItem* i : { wallpaper, web, static_cast<QQuickItem*>(page), applets, static_cast<QQuickItem*>(mouse) })
            i->setSize(QSizeF(400, 300));
        web->setProperty("activeFocusOnPress", true);
        mouse->setTarget(page);
        keys->setTarget(page);
    }
    void click() {
        const QPointF p(200, 150);
        QMouseEvent press(QEvent::MouseButtonPress, p, w.mapToGlobal(p), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(mouse, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, p, w.mapToGlobal(p), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(mouse, &release);
    }
    void type(int k, const QString& text = QString()) {
        QKeyEvent ev(QEvent::KeyPress, k, Qt::NoModifier, text);
        QCoreApplication::sendEvent(w.activeFocusItem(), &ev);
    }
};

void TestKeyGrabber::inWindow_clickOnThePage_keepsTheMode() {
    Desktop d;
    d.w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d.w));
    QSignalSpy exit(d.keys, &wekde::KeyGrabber::exitRequested);
    d.keys->setActive(true);
    QVERIFY(d.keys->hasActiveFocus());
    d.click(); // a click on a text field or a game on the page
    QCOMPARE(exit.count(), 0);
    QTRY_VERIFY(d.keys->hasActiveFocus());
    QVERIFY(d.keys->active());
    // Keys still reach the page through the grabber, and Escape still leaves.
    d.type(Qt::Key_A, QStringLiteral("a"));
    QCOMPARE(d.page->keys, 1);
    d.type(Qt::Key_Escape);
    QCOMPARE(exit.count(), 1);
}

void TestKeyGrabber::inWindow_escapeAfterAPriorClickOnThePage_givesTheDesktopTheKeys() {
    Desktop d;
    d.w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d.w));
    d.click(); // Mouse input on: the page took the focus before keyboard mode
    QCOMPARE(d.w.activeFocusItem(), d.page);
    QSignalSpy exit(d.keys, &wekde::KeyGrabber::exitRequested);
    d.keys->setActive(true);
    QVERIFY(d.keys->hasActiveFocus());
    d.type(Qt::Key_Escape);
    QCOMPARE(exit.count(), 1);
    d.keys->setActive(false); // main.qml: keyboardMode = false
    QVERIFY(d.w.activeFocusItem() != d.page);
    QVERIFY(d.applets->hasActiveFocus());
    const int before = d.page->keys;
    d.type(Qt::Key_A, QStringLiteral("a"));
    QCOMPARE(d.page->keys, before);
}

void TestKeyGrabber::destroyedTarget_noCrash() {
    TestableKeyGrabber g;
    auto*              target = new QQuickItem;
    g.setTarget(target);
    g.setActive(true);
    delete target;
    QCOMPARE(g.target(), nullptr);
    auto ev = key(QEvent::KeyPress, Qt::Key_A, "a");
    g.keyPressEvent(&ev);
    g.setActive(false);
}

QTEST_MAIN(TestKeyGrabber)
#include "tst_keygrabber.moc"
