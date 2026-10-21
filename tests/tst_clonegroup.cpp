// CloneGroup (we-manager clone patch): the table the screens of one process
// write their entries to.  It keeps no rule (plugin/contents/ui/js/clone.mjs
// does, tests/js/clone.test.mjs); here: every instance sees every entry, an
// empty entry and a destroyed instance leave the table, and changed() reaches
// every instance once per turn of the event loop.
#include <QtTest>

#include "CloneGroup.hpp"

using wekde::CloneGroup;

class TstCloneGroup : public QObject {
    Q_OBJECT

private slots:
    void every_screen_sees_every_entry_in_the_order_they_were_made() {
        CloneGroup a, b;
        QVERIFY(a.id() != b.id());
        QCOMPARE(a.members().size(), 0);
        a.setMember({ { "key", "scene-1" }, { "role", "source" } });
        b.setMember({ { "key", "scene-1" }, { "role", "mirror" }, { "playing", true } });
        const QVariantList fromA = a.members();
        QCOMPARE(fromA, b.members());
        QCOMPARE(fromA.size(), 2);
        QCOMPARE(fromA[0].toMap().value("id").toInt(), a.id());
        QCOMPARE(fromA[0].toMap().value("role").toString(), QStringLiteral("source"));
        QCOMPARE(fromA[1].toMap().value("id").toInt(), b.id());
        QCOMPARE(fromA[1].toMap().value("playing").toBool(), true);
    }

    void an_empty_entry_and_a_screen_that_goes_leave_the_table() {
        CloneGroup a;
        a.setMember({ { "key", "scene-1" } });
        {
            CloneGroup b;
            b.setMember({ { "key", "scene-1" } });
            QCOMPARE(a.members().size(), 2);
            b.setMember({});
            QCOMPARE(a.members().size(), 1);
            b.setMember({ { "key", "scene-2" } });
            QCOMPARE(a.members().size(), 2);
        }
        QCOMPARE(a.members().size(), 1);
        QCOMPARE(a.members()[0].toMap().value("id").toInt(), a.id());
    }

    void a_change_reaches_every_screen_once_per_turn() {
        CloneGroup a, b;
        QCoreApplication::processEvents(); // what the tests before queued
        QSignalSpy sa(&a, &CloneGroup::changed), sb(&b, &CloneGroup::changed);
        a.setMember({ { "key", "scene-1" } });
        a.setMember({ { "key", "scene-1" }, { "playing", true } });
        b.setMember({ { "key", "scene-1" } });
        QCOMPARE(sa.count(), 0); // on the next turn, not inside the write
        QTRY_COMPARE(sb.count(), 1);
        QCOMPARE(sa.count(), 1);
        // The same entry again tells no one.
        b.setMember({ { "key", "scene-1" } });
        QTest::qWait(30);
        QCOMPARE(sa.count(), 1);
        QCOMPARE(sb.count(), 1);
    }

    void a_screen_that_goes_tells_the_ones_left() {
        CloneGroup a;
        auto* b = new CloneGroup;
        b->setMember({ { "key", "scene-1" }, { "role", "source" } });
        QCoreApplication::processEvents();
        QSignalSpy sa(&a, &CloneGroup::changed);
        delete b;
        QTRY_COMPARE(sa.count(), 1);
        QCOMPARE(a.members().size(), 0);
    }

    void a_handler_may_destroy_another_screen() {
        // changed() goes to a copy of the list: a screen destroyed by one
        // handler is skipped, not called.
        CloneGroup a;
        auto*      b = new CloneGroup;
        QPointer<CloneGroup> guard(b);
        connect(&a, &CloneGroup::changed, this, [&] { delete guard.data(); });
        connect(b, &CloneGroup::changed, this, [] { QFAIL("called after it was destroyed"); });
        a.setMember({ { "key", "scene-1" } });
        QTRY_VERIFY(guard.isNull());
        QTest::qWait(30);
    }

    void frame_names_are_never_given_twice() {
        CloneGroup    a, b;
        QSet<QString> seen;
        for (int i = 0; i < 50; i++) {
            seen.insert(a.newFrameKey());
            seen.insert(b.newFrameKey());
        }
        QCOMPARE(seen.size(), 100);
    }
};

QTEST_GUILESS_MAIN(TstCloneGroup)
#include "tst_clonegroup.moc"
