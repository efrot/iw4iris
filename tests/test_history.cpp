#include "iris/history.hpp"
#include "test.hpp"

using namespace iris;

TEST_CASE("history: undo and redo")
{
    History<int> h;
    h.Reset(0);
    CHECK(!h.CanUndo());
    h.Commit(1, "one");
    h.Commit(2, "two");
    CHECK(h.UndoLabel() == "two");
    CHECK(*h.Undo() == 1);
    CHECK(*h.Undo() == 0);
    CHECK(h.Undo() == nullptr);
    CHECK(h.RedoLabel() == "one");
    CHECK(*h.Redo() == 1);
    h.Commit(5, "five");  // drops the redo branch
    CHECK(!h.CanRedo());
    CHECK(*h.Undo() == 1);
}

TEST_CASE("history: merged steps")
{
    History<int> h;
    h.Reset(0);
    h.Commit(1, "drag", true);
    h.Commit(2, "drag", true);
    h.Commit(3, "drag", true);
    CHECK(h.Size() == 2);
    CHECK(*h.Current() == 3);
    CHECK(*h.Undo() == 0);
}

TEST_CASE("history: limit drops the oldest steps")
{
    History<int> h(4);
    h.Reset(0);
    for (int i = 1; i <= 10; ++i)
        h.Commit(i, "step");
    CHECK(h.Size() == 4);
    CHECK(*h.Current() == 10);
    int undos = 0;
    while (h.Undo())
        ++undos;
    CHECK(undos == 3);
    CHECK(*h.Current() == 7);
}
