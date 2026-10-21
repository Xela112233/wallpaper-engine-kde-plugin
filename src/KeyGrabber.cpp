#include "KeyGrabber.hpp"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QMetaObject>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <memory>

using namespace wekde;

KeyGrabber::KeyGrabber(QQuickItem* parent): QQuickItem(parent) {
    // So input methods (compose keys, non-Latin text) send their text here
    // too, for the page.
    setFlag(ItemAcceptsInputMethod, true);
}

KeyGrabber::~KeyGrabber() {
    if (m_active) tellTarget(false);
}

void KeyGrabber::setTarget(QQuickItem* item) {
    if (item == m_target) return;
    if (m_active) tellTarget(false);
    m_target = item;
    if (m_active) tellTarget(true);
    Q_EMIT targetChanged();
}

void KeyGrabber::setActive(bool value) {
    if (value == m_active) return;
    if (value) {
        QQuickItem* had = window() ? window()->activeFocusItem() : nullptr;
        m_previousFocus = had != this ? had : nullptr;
        m_active        = true;
        forceActiveFocus(Qt::OtherFocusReason);
        // Keys only come while the desktop window is the active one; asking
        // is all a client can do (KWin decides).
        if (window()) window()->requestActivate();
        tellTarget(true);
    } else {
        // Cleared first, so the focus leaving below is not taken for the user
        // clicking elsewhere.
        m_active = false;
        tellTarget(false);
        // The keys go back to the desktop, also when the page holds the focus
        // now (a click on it): to the item that had them before, unless that
        // was the page (a click before the mode started), then to the desktop's
        // icon area this grabber sits on.
        QQuickItem* now = window() ? window()->activeFocusItem() : nullptr;
        if (now == this || inPage(now)) {
            // Not kept as the focus scope's choice either, so the icon area
            // does not hand the keys straight back here.
            setFocus(false);
            if (m_previousFocus && ! inPage(m_previousFocus))
                m_previousFocus->forceActiveFocus(Qt::OtherFocusReason);
            else if (parentItem())
                parentItem()->forceActiveFocus(Qt::OtherFocusReason);
        }
        m_previousFocus = nullptr;
    }
    Q_EMIT activeChanged();
}

bool KeyGrabber::inPage(QQuickItem* item) const {
    if (! item || ! m_target) return false;
    // The page's side of the scene: the highest item above the target (its
    // WebEngineView, the wallpaper around it) that does not hold this
    // grabber, which sits with the desktop's icons and widgets.
    QQuickItem* page = m_target;
    while (page->parentItem() && ! page->parentItem()->isAncestorOf(this)) page = page->parentItem();
    return item == page || page->isAncestorOf(item);
}

void KeyGrabber::reclaimFocus() {
    if (! m_active || hasActiveFocus()) return;
    forceActiveFocus(Qt::OtherFocusReason);
    // Taking the focus back told the page it lost it; it still has the keys.
    tellTarget(true);
}

void KeyGrabber::tellTarget(bool focused) {
    if (! m_target) return;
    QFocusEvent ev(focused ? QEvent::FocusIn : QEvent::FocusOut, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(m_target, &ev);
}

void KeyGrabber::forwardKey(QKeyEvent* event) {
    if (! m_target) return;
    std::unique_ptr<QKeyEvent> copy(event->clone());
    copy->setAccepted(false);
    QCoreApplication::sendEvent(m_target, copy.get());
}

void KeyGrabber::keyPressEvent(QKeyEvent* event) {
    if (! m_active) {
        event->ignore();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        Q_EMIT exitRequested();
        return;
    }
    forwardKey(event);
    // Accepted either way: in keyboard mode the keys are the wallpaper's, not
    // the desktop's (Folder View's icon navigation, its shortcuts).
    event->accept();
}

void KeyGrabber::keyReleaseEvent(QKeyEvent* event) {
    if (! m_active) {
        event->ignore();
        return;
    }
    if (event->key() != Qt::Key_Escape) forwardKey(event);
    event->accept();
}

void KeyGrabber::inputMethodEvent(QInputMethodEvent* event) {
    if (! m_active || ! m_target) {
        QQuickItem::inputMethodEvent(event);
        return;
    }
    std::unique_ptr<QInputMethodEvent> copy(event->clone());
    QCoreApplication::sendEvent(m_target, copy.get());
    event->accept();
}

QVariant KeyGrabber::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (m_active && m_target) return m_target->inputMethodQuery(query);
    return QQuickItem::inputMethodQuery(query);
}

void KeyGrabber::focusOutEvent(QFocusEvent* event) {
    QQuickItem::focusOutEvent(event);
    // The desktop window losing the focus (another window raised, a popup)
    // keeps the mode: the keys come back here when it is active again.
    if (! m_active || event->reason() == Qt::ActiveWindowFocusReason ||
        event->reason() == Qt::PopupFocusReason)
        return;
    // A click on the page itself (MouseGrabber hands it the press, and Qt
    // WebEngine's input item takes the focus on a press) keeps the mode: the
    // focus comes back here once Qt has finished moving it.
    if (inPage(window() ? window()->activeFocusItem() : nullptr)) {
        QMetaObject::invokeMethod(this, &KeyGrabber::reclaimFocus, Qt::QueuedConnection);
        return;
    }
    // The focus going to another item (a click on an icon or a widget) ends it.
    Q_EMIT exitRequested();
}
