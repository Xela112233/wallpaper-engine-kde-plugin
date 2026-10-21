#include "CloneGroup.hpp"

#include <QCoreApplication>
#include <QList>
#include <QPointer>
#include <QTimer>

using namespace wekde;

namespace
{
// The process's table: the CloneGroups alive, in the order they were made.
struct Table {
    QList<CloneGroup*> all;
    int                nextId { 1 };
    int                nextFrame { 1 };
    bool               notifyQueued { false };
};
Table& table() {
    // Never destroyed: a CloneGroup that outlives the statics at exit still
    // finds it.
    static Table* t = new Table;
    return *t;
}
} // namespace

CloneGroup::CloneGroup(QObject* parent): QObject(parent), m_id(table().nextId++) {
    table().all.append(this);
}

CloneGroup::~CloneGroup() {
    table().all.removeOne(this);
    // The screens left elect again if this one rendered.
    if (! m_member.isEmpty()) notifyAll();
}

void CloneGroup::setMember(const QVariantMap& member) {
    if (member == m_member) return;
    m_member = member;
    Q_EMIT memberChanged();
    notifyAll();
}

QVariantList CloneGroup::members() const {
    QVariantList out;
    for (const CloneGroup* g : std::as_const(table().all)) {
        if (g->m_member.isEmpty()) continue;
        QVariantMap entry = g->m_member;
        entry.insert(QStringLiteral("id"), g->m_id);
        out.append(entry);
    }
    return out;
}

QString CloneGroup::newFrameKey() {
    return QStringLiteral("wek-frame-%1-%2").arg(m_id).arg(table().nextFrame++);
}

void CloneGroup::notifyAll() {
    Table& t = table();
    if (t.notifyQueued || QCoreApplication::instance() == nullptr) return;
    t.notifyQueued = true;
    QTimer::singleShot(0, QCoreApplication::instance(), [] {
        Table& now       = table();
        now.notifyQueued = false;
        // A handler may load or unload a backend, and a screen may go
        // meanwhile: go by guarded pointers over a copy.
        QList<QPointer<CloneGroup>> targets;
        targets.reserve(now.all.size());
        for (CloneGroup* g : std::as_const(now.all)) targets.append(g);
        for (const QPointer<CloneGroup>& g : std::as_const(targets))
            if (g) Q_EMIT g->changed();
    });
}
