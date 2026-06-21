#include "todo_model_test_utils.h"

using namespace xtodo_test;

namespace {

void ReplaceListsNormalizesEveryListSelectsCurrentAndPreservesNextId() {
    TodoList generatedId;
    generatedId.title = L"";
    generatedId.items = {
        TodoItem{L"done", true, 3, true},
        TodoItem{L"active", false, 2, false},
    };

    TodoList existing;
    existing.id = "list-5";
    existing.title = L"Work";
    existing.completedExpanded = true;
    existing.items = {
        TodoItem{L"work active", false, 0, false},
        TodoItem{L"work done", true, 0, false},
    };

    TodoModel model;
    model.ReplaceLists({generatedId, existing}, "list-5");

    EXPECT_EQ(model.ListCount(), 2);
    EXPECT_EQ(model.ListAt(0)->id, std::string("list-1"));
    EXPECT_EQ(model.ListAt(0)->title, std::wstring(L"默认"));
    EXPECT_EQ(model.ListAt(0)->activeCount, 1);
    EXPECT_EQ(model.CurrentListIndex(), 1);
    EXPECT_EQ(model.CurrentList().id, std::string("list-5"));
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    AssertInvariants(model);

    const int added = model.AddList(L"Later");
    EXPECT_EQ(added, 2);
    EXPECT_EQ(model.CurrentList().id, std::string("list-6"));
    AssertInvariants(model);
}

void ReplaceListsWithEmptyInputFallsBackToInbox() {
    TodoModel model;
    model.AddList(L"Work");
    model.AddActive(L"Work item");

    model.ReplaceLists({}, "missing");
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"默认"));
    EXPECT_EQ(model.Count(), 0);
    AssertInvariants(model);
}

void MultipleListsKeepItemsAndActiveCountsIsolated() {
    TodoModel model;
    model.AddActive(L"Inbox item");
    const int workIndex = model.AddList(L"Work");
    model.AddActive(L"Work item");
    model.AddActive(L"Second work item");
    model.SetDone(0, true);

    EXPECT_EQ(model.CurrentListIndex(), workIndex);
    EXPECT_EQ(model.ActiveCount(), 1);
    EXPECT_EQ(model.CompletedCount(), 1);
    EXPECT_EQ(model.TotalActiveCount(), 2);

    EXPECT_TRUE(model.SetCurrentListIndex(0));
    ExpectTexts(model, {L"Inbox item"});
    EXPECT_EQ(model.ActiveCount(), 1);
    EXPECT_EQ(model.CompletedCount(), 0);

    EXPECT_TRUE(model.SetCurrentListIndex(workIndex));
    ExpectTexts(model, {L"Second work item", L"Work item"});
    ExpectDones(model, {false, true});
    AssertInvariants(model);
}

void CurrentListSelectionByIndexAndIdIsValidated() {
    TodoModel model;
    const int work = model.AddList(L"Work");
    const std::string workId = model.CurrentList().id;
    const int home = model.AddList(L"Home");
    const std::string homeId = model.CurrentList().id;

    EXPECT_FALSE(model.SetCurrentListIndex(-1));
    EXPECT_FALSE(model.SetCurrentListIndex(99));
    EXPECT_EQ(model.CurrentListIndex(), home);

    EXPECT_TRUE(model.SetCurrentListId(workId));
    EXPECT_EQ(model.CurrentListIndex(), work);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"Work"));

    EXPECT_TRUE(model.SetCurrentListId(homeId));
    EXPECT_EQ(model.CurrentListIndex(), home);
    EXPECT_FALSE(model.SetCurrentListId("list-999"));
    EXPECT_EQ(model.CurrentListIndex(), home);
    EXPECT_EQ(model.ListAt(-1), nullptr);
    EXPECT_EQ(model.ListAt(99), nullptr);
    AssertInvariants(model);
}

void RenameListRejectsInvalidOrEmptyTitleAndPreservesExistingTitle() {
    TodoModel model;

    EXPECT_FALSE(model.RenameList(-1, L"Bad"));
    EXPECT_FALSE(model.RenameList(99, L"Bad"));
    EXPECT_TRUE(model.RenameList(0, L"Inbox renamed"));
    EXPECT_EQ(model.ListAt(0)->title, std::wstring(L"Inbox renamed"));

    EXPECT_FALSE(model.RenameList(0, L""));
    EXPECT_EQ(model.ListAt(0)->title, std::wstring(L"Inbox renamed"));
    AssertInvariants(model);
}

void RemoveListRejectsInvalidIndexesAndLastRemainingList() {
    TodoModel model;

    EXPECT_FALSE(model.RemoveList(-1));
    EXPECT_FALSE(model.RemoveList(99));
    EXPECT_FALSE(model.RemoveList(0));
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    AssertInvariants(model);
}

void RemoveListAdjustsCurrentIndexAndPreservesOtherLists() {
    TodoModel model;
    EXPECT_TRUE(model.RenameList(0, L"Inbox"));
    model.AddActive(L"Inbox item");

    const int work = model.AddList(L"Work");
    model.AddActive(L"Work item");
    model.AddActive(L"Done work");
    model.SetDone(1, true);

    const int home = model.AddList(L"Home");
    model.AddActive(L"Home item");
    EXPECT_EQ(model.CurrentListIndex(), home);

    EXPECT_TRUE(model.SetCurrentListIndex(work));
    EXPECT_TRUE(model.RemoveList(0));
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"Work"));
    ExpectTexts(model, {L"Work item", L"Done work"});
    ExpectDones(model, {false, true});
    EXPECT_EQ(model.ActiveCount(), 1);
    EXPECT_EQ(model.CompletedCount(), 1);

    EXPECT_TRUE(model.RemoveList(1));
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"Work"));
    AssertInvariants(model);
}

void RemoveCurrentLastListSelectsPreviousList() {
    TodoModel model;
    EXPECT_TRUE(model.RenameList(0, L"A"));
    model.AddList(L"B");
    model.AddList(L"C");

    EXPECT_EQ(model.CurrentListIndex(), 2);
    EXPECT_TRUE(model.RemoveList(2));
    EXPECT_EQ(model.ListCount(), 2);
    EXPECT_EQ(model.CurrentListIndex(), 1);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"B"));

    EXPECT_TRUE(model.RemoveList(1));
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"A"));
    AssertInvariants(model);
}

void CompletedExpansionStateIsPerList() {
    TodoModel model;
    model.SetCurrentCompletedExpanded(true);
    const int work = model.AddList(L"Work");
    EXPECT_FALSE(model.CurrentList().completedExpanded);
    model.SetCurrentCompletedExpanded(true);

    EXPECT_TRUE(model.SetCurrentListIndex(0));
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    EXPECT_TRUE(model.SetCurrentListIndex(work));
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    model.SetCurrentCompletedExpanded(false);
    EXPECT_FALSE(model.CurrentList().completedExpanded);
    EXPECT_TRUE(model.SetCurrentListIndex(0));
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    AssertInvariants(model);
}

const TestCase kTests[] = {
    {"ReplaceListsNormalizesEveryListSelectsCurrentAndPreservesNextId", ReplaceListsNormalizesEveryListSelectsCurrentAndPreservesNextId},
    {"ReplaceListsWithEmptyInputFallsBackToInbox", ReplaceListsWithEmptyInputFallsBackToInbox},
    {"MultipleListsKeepItemsAndActiveCountsIsolated", MultipleListsKeepItemsAndActiveCountsIsolated},
    {"CurrentListSelectionByIndexAndIdIsValidated", CurrentListSelectionByIndexAndIdIsValidated},
    {"RenameListRejectsInvalidOrEmptyTitleAndPreservesExistingTitle", RenameListRejectsInvalidOrEmptyTitleAndPreservesExistingTitle},
    {"RemoveListRejectsInvalidIndexesAndLastRemainingList", RemoveListRejectsInvalidIndexesAndLastRemainingList},
    {"RemoveListAdjustsCurrentIndexAndPreservesOtherLists", RemoveListAdjustsCurrentIndexAndPreservesOtherLists},
    {"RemoveCurrentLastListSelectsPreviousList", RemoveCurrentLastListSelectsPreviousList},
    {"CompletedExpansionStateIsPerList", CompletedExpansionStateIsPerList},
};

} // namespace

int main() {
    return RunTests("todo_model_list", kTests);
}
