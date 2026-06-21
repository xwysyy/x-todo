#include "todo_model_test_utils.h"

#include <random>
#include <string>

using namespace xtodo_test;

namespace {

void NewlyCompletedSubtreeGoesBeforeOlderCompletedAndRestoresAsBlock() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"A.1", 1);
    model.AddActive(L"B", 0);
    model.AddActive(L"C", 0);

    model.SetDone(2, true); // complete B first
    ExpectTexts(model, {L"A", L"A.1", L"C", L"B"});
    model.SetDone(0, true); // complete A subtree after B is already completed
    ExpectTexts(model, {L"C", L"A", L"A.1", L"B"});
    ExpectDones(model, {false, true, true, true});
    ExpectLevels(model, {0, 0, 1, 0});

    model.SetDone(1, false); // restore A subtree
    ExpectTexts(model, {L"C", L"A", L"A.1", L"B"});
    ExpectDones(model, {false, false, false, true});
    ExpectLevels(model, {0, 0, 1, 0});
    AssertInvariants(model);
}

void InsertUsingSubtreeEndDoesNotAttachNewItemToNestedParent() {
    TodoModel model;
    model.AddActive(L"Root", 0);
    model.AddActive(L"Child", 1);
    model.AddActive(L"Grandchild", 2);
    model.AddActive(L"Unrelated", 0);

    const int insertAt = model.SubtreeEnd(0);
    model.InsertActive(insertAt, L"Sibling root", model.Items()[0].level);

    ExpectTexts(model, {L"Root", L"Child", L"Grandchild", L"Sibling root", L"Unrelated"});
    ExpectLevels(model, {0, 1, 2, 0, 0});
    EXPECT_EQ(model.SubtreeEnd(0), 3);
    AssertInvariants(model);
}

void MovingIndentedBlockToFrontNormalizesRootLevel() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"B", 0);
    model.AddActive(L"B.1", 1);
    model.AddActive(L"B.1.a", 2);
    model.AddActive(L"C", 0);

    EXPECT_TRUE(model.MoveActive(1, 0));
    ExpectTexts(model, {L"B", L"B.1", L"B.1.a", L"A", L"C"});
    ExpectLevels(model, {0, 1, 2, 0, 0});
    AssertInvariants(model);
}

void RemoveListAfterCurrentKeepsCurrentSelectionStable() {
    TodoModel model;
    EXPECT_TRUE(model.RenameList(0, L"A"));
    const int b = model.AddList(L"B");
    model.AddList(L"C");
    EXPECT_TRUE(model.SetCurrentListIndex(b));

    EXPECT_TRUE(model.RemoveList(2));
    EXPECT_EQ(model.ListCount(), 2);
    EXPECT_EQ(model.CurrentListIndex(), b);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"B"));
    AssertInvariants(model);
}

void ReplaceListsUsesFirstGeneratedIdAndDoesNotCollideAfterImport() {
    TodoList imported;
    imported.id = "list-42";
    imported.title = L"Imported";
    imported.items = { TodoItem{L"item", false, 0, false} };

    TodoList missingId;
    missingId.title = L"Missing id";

    TodoModel model;
    model.ReplaceLists({missingId, imported}, "");
    EXPECT_EQ(model.ListAt(0)->id, std::string("list-1"));
    EXPECT_EQ(model.ListAt(1)->id, std::string("list-42"));

    model.AddList(L"New");
    EXPECT_EQ(model.CurrentList().id, std::string("list-43"));
    AssertInvariants(model);
}

void DeterministicMutationSequenceMaintainsInvariantsAfterEveryStep() {
    TodoModel model;
    AssertInvariants(model);

    model.AddActive(L"A");
    AssertInvariants(model);
    model.AddActive(L"B");
    AssertInvariants(model);
    EXPECT_TRUE(model.IndentItemUnder(1, 0));
    AssertInvariants(model);
    EXPECT_TRUE(model.ToggleCollapsed(0));
    AssertInvariants(model);
    model.InsertActive(model.SubtreeEnd(0), L"C", 0);
    AssertInvariants(model);
    EXPECT_TRUE(model.MoveActive(0, model.ActiveCount()));
    AssertInvariants(model);
    model.SetDone(1, true);
    AssertInvariants(model);
    model.SetDone(model.ActiveCount(), false);
    AssertInvariants(model);
    model.Remove(0);
    AssertInvariants(model);
    model.ClearCompleted();
    AssertInvariants(model);

    TodoList custom;
    custom.id = "list-3";
    custom.title = L"Imported";
    custom.items = {
        TodoItem{L"imported done", true, 3, true},
        TodoItem{L"imported active", false, 3, true},
    };
    model.ReplaceLists({custom}, "list-3");
    AssertInvariants(model);

    EXPECT_TRUE(model.RenameList(0, L"Imported renamed"));
    AssertInvariants(model);
    model.AddList(L"Scratch");
    AssertInvariants(model);
    EXPECT_TRUE(model.RemoveList(0));
    AssertInvariants(model);
}

void DeterministicFuzzMutationsPreserveModelInvariants() {
    TodoModel model;
    std::mt19937 rng(0x58544f44u); // "XTOD"

    for (int step = 0; step < 1000; ++step) {
        const int op = static_cast<int>(rng() % 12);
        const int count = model.Count();
        const int active = model.ActiveCount();

        switch (op) {
            case 0:
                if (active < 30) model.AddActive(L"item " + std::to_wstring(step), static_cast<int>(rng() % 6) - 1);
                break;
            case 1:
                if (active < 30) model.InsertActive(static_cast<int>(rng() % 40) - 5, L"insert " + std::to_wstring(step), static_cast<int>(rng() % 6) - 1);
                break;
            case 2:
                if (count > 0) model.SetDone(static_cast<int>(rng() % count), (rng() % 2) == 0);
                break;
            case 3:
                if (count > 0 && count > 5) model.Remove(static_cast<int>(rng() % count));
                break;
            case 4:
                if (active > 0) model.MoveActive(static_cast<int>(rng() % active), static_cast<int>(rng() % (active + 1)));
                break;
            case 5:
                if (active > 1) {
                    int index = 1 + static_cast<int>(rng() % (active - 1));
                    int parent = static_cast<int>(rng() % index);
                    model.IndentItemUnder(index, parent);
                }
                break;
            case 6:
                if (active > 0) model.OutdentItem(static_cast<int>(rng() % active));
                break;
            case 7:
                if (count > 0) model.ToggleCollapsed(static_cast<int>(rng() % count));
                break;
            case 8:
                if (model.ListCount() < 5) model.AddList(L"List " + std::to_wstring(step));
                break;
            case 9:
                if (model.ListCount() > 1) model.RemoveList(static_cast<int>(rng() % model.ListCount()));
                break;
            case 10:
                if (model.ListCount() > 0) model.SetCurrentListIndex(static_cast<int>(rng() % model.ListCount()));
                break;
            case 11:
                if (model.ListCount() > 0) model.SetCurrentCompletedExpanded((rng() % 2) == 0);
                break;
        }
        AssertInvariants(model);
    }
}

const TestCase kTests[] = {
    {"NewlyCompletedSubtreeGoesBeforeOlderCompletedAndRestoresAsBlock", NewlyCompletedSubtreeGoesBeforeOlderCompletedAndRestoresAsBlock},
    {"InsertUsingSubtreeEndDoesNotAttachNewItemToNestedParent", InsertUsingSubtreeEndDoesNotAttachNewItemToNestedParent},
    {"MovingIndentedBlockToFrontNormalizesRootLevel", MovingIndentedBlockToFrontNormalizesRootLevel},
    {"RemoveListAfterCurrentKeepsCurrentSelectionStable", RemoveListAfterCurrentKeepsCurrentSelectionStable},
    {"ReplaceListsUsesFirstGeneratedIdAndDoesNotCollideAfterImport", ReplaceListsUsesFirstGeneratedIdAndDoesNotCollideAfterImport},
    {"DeterministicMutationSequenceMaintainsInvariantsAfterEveryStep", DeterministicMutationSequenceMaintainsInvariantsAfterEveryStep},
    {"DeterministicFuzzMutationsPreserveModelInvariants", DeterministicFuzzMutationsPreserveModelInvariants},
};

} // namespace

int main() {
    return RunTests("todo_model_regression", kTests);
}
