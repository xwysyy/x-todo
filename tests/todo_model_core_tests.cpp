#include "todo_model_test_utils.h"

#include <utility>
#include <vector>

using namespace xtodo_test;

namespace {

void DefaultModelHasOneInboxList() {
    TodoModel model;
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"默认"));
    EXPECT_EQ(model.Count(), 0);
    EXPECT_EQ(model.ActiveCount(), 0);
    EXPECT_EQ(model.CompletedCount(), 0);
    EXPECT_EQ(model.TotalActiveCount(), 0);
    AssertInvariants(model);
}

void ClampTodoLevelClampsBothEdges() {
    EXPECT_EQ(ClampTodoLevel(-99), 0);
    EXPECT_EQ(ClampTodoLevel(0), 0);
    EXPECT_EQ(ClampTodoLevel(2), 2);
    EXPECT_EQ(ClampTodoLevel(kMaxTodoLevel), kMaxTodoLevel);
    EXPECT_EQ(ClampTodoLevel(kMaxTodoLevel + 99), kMaxTodoLevel);
}

void AddInsertAndSetTextClampIndexesAndNormalizeLevels() {
    TodoModel model;

    EXPECT_EQ(model.InsertActive(-10, L"front with high requested level", 99), 0);
    EXPECT_EQ(model.Items()[0].level, 0);

    EXPECT_EQ(model.AddActive(L"child requested too deep", 99), 1);
    EXPECT_EQ(model.AddActive(L"grandchild requested too deep", 99), 2);
    EXPECT_EQ(model.InsertActive(99, L"append clamps to active end", -1), 3);
    ExpectTexts(model, {
        L"front with high requested level",
        L"child requested too deep",
        L"grandchild requested too deep",
        L"append clamps to active end",
    });
    ExpectLevels(model, {0, 1, 2, 2});

    model.SetText(1, L"renamed\nwith newline");
    model.SetText(-1, L"ignored");
    model.SetText(99, L"ignored");
    ExpectTexts(model, {
        L"front with high requested level",
        L"renamed\nwith newline",
        L"grandchild requested too deep",
        L"append clamps to active end",
    });
    AssertInvariants(model);
}

void AddDoneUndoAndClearMaintainActiveCompletedPartition() {
    TodoModel model;
    EXPECT_EQ(model.AddActive(L"A"), 0);
    EXPECT_EQ(model.AddActive(L"B"), 1);
    EXPECT_EQ(model.AddActive(L"C"), 2);

    model.SetDone(1, true);
    ExpectTexts(model, {L"A", L"C", L"B"});
    ExpectDones(model, {false, false, true});
    EXPECT_EQ(model.ActiveCount(), 2);
    EXPECT_EQ(model.CompletedCount(), 1);
    AssertInvariants(model);

    model.SetDone(2, false);
    ExpectTexts(model, {L"A", L"C", L"B"});
    ExpectDones(model, {false, false, false});
    EXPECT_EQ(model.ActiveCount(), 3);
    EXPECT_EQ(model.CompletedCount(), 0);
    AssertInvariants(model);

    model.SetDone(0, true);
    model.SetDone(0, true); // second active row is now C; this creates two completed rows
    ExpectTexts(model, {L"B", L"C", L"A"});
    ExpectDones(model, {false, true, true});
    model.ClearCompleted();
    ExpectTexts(model, {L"B"});
    EXPECT_EQ(model.ActiveCount(), 1);
    EXPECT_EQ(model.CompletedCount(), 0);
    AssertInvariants(model);
}

void ReplaceAllNormalizesLegacySingleListData() {
    TodoModel model;
    std::vector<TodoItem> items = {
        TodoItem{L"done first", true, 99, true},
        TodoItem{L"active root", false, 3, true},
        TodoItem{L"active child", false, 3, false},
        TodoItem{L"done child", true, 2, false},
    };

    model.ReplaceAll(std::move(items), true);
    ExpectTexts(model, {L"active root", L"active child", L"done first", L"done child"});
    ExpectDones(model, {false, false, true, true});
    ExpectLevels(model, {0, 1, 0, 1});
    ExpectCollapsed(model, {true, false, true, false});
    EXPECT_EQ(model.ActiveCount(), 2);
    EXPECT_EQ(model.CompletedCount(), 2);
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    AssertInvariants(model);
}

void InvalidIndexesAndNoOpMutationsAreSafe() {
    TodoModel model;
    model.AddActive(L"Only");

    model.SetText(-1, L"bad");
    model.SetText(99, L"bad");
    model.Remove(-1);
    model.Remove(99);
    model.SetDone(-1, true);
    model.SetDone(99, true);
    EXPECT_FALSE(model.MoveActive(-1, 0));
    EXPECT_FALSE(model.MoveActive(99, 0));
    EXPECT_FALSE(model.IndentItemUnder(0, 0));
    EXPECT_FALSE(model.OutdentItem(0));
    EXPECT_FALSE(model.ToggleCollapsed(0));
    EXPECT_FALSE(model.SetCurrentListIndex(-1));
    EXPECT_FALSE(model.SetCurrentListIndex(99));
    EXPECT_FALSE(model.SetCurrentListId("missing"));

    ExpectTexts(model, {L"Only"});
    EXPECT_EQ(model.ActiveCount(), 1);
    AssertInvariants(model);
}

const TestCase kTests[] = {
    {"DefaultModelHasOneInboxList", DefaultModelHasOneInboxList},
    {"ClampTodoLevelClampsBothEdges", ClampTodoLevelClampsBothEdges},
    {"AddInsertAndSetTextClampIndexesAndNormalizeLevels", AddInsertAndSetTextClampIndexesAndNormalizeLevels},
    {"AddDoneUndoAndClearMaintainActiveCompletedPartition", AddDoneUndoAndClearMaintainActiveCompletedPartition},
    {"ReplaceAllNormalizesLegacySingleListData", ReplaceAllNormalizesLegacySingleListData},
    {"InvalidIndexesAndNoOpMutationsAreSafe", InvalidIndexesAndNoOpMutationsAreSafe},
};

} // namespace

int main() {
    return RunTests("todo_model_core", kTests);
}
