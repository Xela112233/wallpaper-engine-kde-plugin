#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace wekde
{

// One renderer for screens that show the same scene (we-manager clone patch,
// main.xml ShareRenderer): what the screens of this process tell each other.
//
// Every screen's wallpaper is its own main.qml, and they share no QML object
// (a QML singleton is per engine).  Each screen whose ShareRenderer is on
// makes one CloneGroup (in CloneLink.qml) and writes its own entry, `member`:
// what it shows, whether it renders, whether it plays.  members() is every
// screen's entry, and changed() tells every screen that one of them changed.
// That is all this class does: it keeps no rule.  Which screen renders is
// decided in QML from the entries (plugin/contents/ui/js/clone.mjs, elect),
// the same on every screen because the entries are the same.
//
// GUI thread only.  A process with no CloneGroup has no table.
class CloneGroup : public QObject {
    Q_OBJECT
    // This screen's number in the table, unique in the process.
    Q_PROPERTY(int id READ id CONSTANT)
    // This screen's entry.  An empty map (the default) takes it out.
    Q_PROPERTY(QVariantMap member READ member WRITE setMember NOTIFY memberChanged)

public:
    explicit CloneGroup(QObject* parent = nullptr);
    ~CloneGroup() override;

    int         id() const { return m_id; }
    QVariantMap member() const { return m_member; }
    void        setMember(const QVariantMap&);

    // Every screen's entry that is not empty, each with its "id", in the
    // order the screens made their CloneGroup.
    Q_INVOKABLE QVariantList members() const;
    // A name no other frame of this process has (SceneViewer.shareKey and
    // SceneMirror.frameKey).
    Q_INVOKABLE QString newFrameKey();

signals:
    void memberChanged();
    // An entry changed, this screen's or another's, or a screen went.  Sent
    // to every CloneGroup on the next turn of the event loop, once for all
    // the changes of a turn.
    void changed();

private:
    static void notifyAll();

    int         m_id;
    QVariantMap m_member;
};

} // namespace wekde
