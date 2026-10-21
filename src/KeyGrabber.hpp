#pragma once
#include <QPointer>
#include <QQuickItem>

class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;

namespace wekde
{

// Keyboard mode for web wallpapers (we-manager patch).  Sits beside the
// MouseGrabber on the desktop's AppletsLayout (main.qml doHookMouse).  While
// `active`, it holds the keyboard focus of the desktop window and hands every
// key (and input-method text) to `target`, the web page's input item; Escape
// is kept and asks to leave (exitRequested).  The target is told it has the
// focus (a FocusIn event) so the page treats the keys as its own, and is told
// it lost it on leaving, when the focus goes back to the item that had it
// (or to the item this grabber sits on, when that was the page).  A click on
// the page while active, which moves the focus into it, keeps the mode: the
// focus comes back here at once; a click elsewhere ends it.
//
// Whether plasmashell's desktop window gets the keyboard at all is the
// compositor's choice: on Wayland the window has to be active (clicked, or
// raised by KWin) for any key to arrive.
class KeyGrabber : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QQuickItem* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit KeyGrabber(QQuickItem* parent = nullptr);
    ~KeyGrabber() override;

    QQuickItem* target() const { return m_target; }
    bool        active() const { return m_active; }
    void        setTarget(QQuickItem*);
    void        setActive(bool);

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

signals:
    void targetChanged();
    void activeChanged();
    // Escape, or the focus moved to another item (a click on an icon or a
    // widget): main.qml leaves keyboard mode.
    void exitRequested();

protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void inputMethodEvent(QInputMethodEvent*) override;
    void focusOutEvent(QFocusEvent*) override;

private:
    void forwardKey(QKeyEvent*);
    void tellTarget(bool focused);
    // Whether `item` is the page: the target, the WebEngineView holding it,
    // or an item inside that view.
    bool inPage(QQuickItem* item) const;
    // Queued from focusOutEvent when the page took the focus.
    void reclaimFocus();

    QPointer<QQuickItem> m_target;
    QPointer<QQuickItem> m_previousFocus;
    bool                 m_active { false };
};

} // namespace wekde
