#include "todo_model_test_utils.h"

using namespace xtodo_test;

namespace {

void InsertAfterCurrentSubtreeCanCreateSiblingBeforeNextRoot() {
    TodoModel model;
    model.AddActive(L"Parent", 0);
    model.AddActive(L"Child", 1);
    model.AddActive(L"Grandchild", 2);
    model.AddActive(L"Next root", 0);

    EXPECT_EQ(model.SubtreeEnd(0), 3);
    const int inserted = model.InsertActive(model.SubtreeEnd(0), L"Inserted sibling", model.Items()[0].level);

    EXPECT_EQ(inserted, 3);
    ExpectTexts(model, {L"Parent", L"Child", L"Grandchild", L"Inserted sibling", L"Next root"});
    ExpectLevels(model, {0, 1, 2, 0, 0});
    EXPECT_EQ(model.SubtreeEnd(0), 3);
    AssertInvariants(model);
}

void SubtreeDoneUndoMovesWholeBlockAndRebasesLevels() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"A.1", 1);
    model.AddActive(L"A.2", 1);
    model.AddActive(L"B", 0);
    model.AddActive(L"C", 0);

    model.SetDone(0, true);
    ExpectTexts(model, {L"B", L"C", L"A", L"A.1", L"A.2"});
    ExpectDones(model, {false, false, true, true, true});
    ExpectLevels(model, {0, 0, 0, 1, 1});
    EXPECT_EQ(model.ActiveCount(), 2);
    EXPECT_EQ(model.CompletedCount(), 3);
    AssertInvariants(model);

    model.SetDone(model.ActiveCount(), false);
    ExpectTexts(model, {L"B", L"C", L"A", L"A.1", L"A.2"});
    ExpectDones(model, {false, false, false, false, false});
    ExpectLevels(model, {0, 0, 0, 1, 1});
    EXPECT_EQ(model.ActiveCount(), 5);
    EXPECT_EQ(model.CompletedCount(), 0);
    AssertInvariants(model);
}

void CompletingNestedSubtreeDoesNotConsumeFollowingSiblings() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"A.1", 1);
    model.AddActive(L"A.1.a", 2);
    model.AddActive(L"A.2", 1);
    model.AddActive(L"B", 0);

    model.SetDone(1, true);
    ExpectTexts(model, {L"A", L"A.2", L"B", L"A.1", L"A.1.a"});
    ExpectDones(model, {false, false, false, true, true});
    ExpectLevels(model, {0, 1, 0, 0, 1});
    EXPECT_EQ(model.SubtreeEnd(0), 2);
    AssertInvariants(model);
}

void RemoveDeletesActiveAndCompletedSubtrees() {
    TodoModel active;
    active.AddActive(L"A", 0);
    active.AddActive(L"A.1", 1);
    active.AddActive(L"A.2", 1);
    active.AddActive(L"B", 0);
    active.Remove(0);
    ExpectTexts(active, {L"B"});
    EXPECT_EQ(active.ActiveCount(), 1);
    AssertInvariants(active);

    TodoModel completed;
    completed.AddActive(L"A", 0);
    completed.AddActive(L"A.1", 1);
    completed.AddActive(L"A.2", 1);
    completed.AddActive(L"B", 0);
    completed.SetDone(0, true);
    completed.Remove(completed.ActiveCount());
    ExpectTexts(completed, {L"B"});
    EXPECT_EQ(completed.ActiveCount(), 1);
    EXPECT_EQ(completed.CompletedCount(), 0);
    AssertInvariants(completed);
}

void MoveActiveReordersWholeSubtreeAndRejectsDropsInsideItself() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"A.1", 1);
    model.AddActive(L"A.2", 1);
    model.AddActive(L"B", 0);
    model.AddActive(L"C", 0);

    EXPECT_FALSE(model.MoveActive(0, 0));
    EXPECT_FALSE(model.MoveActive(0, 2));
    EXPECT_FALSE(model.MoveActive(0, 3));
    ExpectTexts(model, {L"A", L"A.1", L"A.2", L"B", L"C"});

    EXPECT_TRUE(model.MoveActive(0, 4));
    ExpectTexts(model, {L"B", L"A", L"A.1", L"A.2", L"C"});
    ExpectLevels(model, {0, 0, 1, 1, 0});
    AssertInvariants(model);

    EXPECT_TRUE(model.MoveActive(1, model.ActiveCount()));
    ExpectTexts(model, {L"B", L"C", L"A", L"A.1", L"A.2"});
    ExpectLevels(model, {0, 0, 0, 1, 1});
    AssertInvariants(model);
}

void IndentOutdentAndCollapseRespectTreeBoundaries() {
    TodoModel model;
    model.AddActive(L"A");
    model.AddActive(L"B");
    model.AddActive(L"C");

    EXPECT_TRUE(model.IndentItemUnder(1, 0));
    EXPECT_TRUE(model.IndentItemUnder(2, 1));
    ExpectLevels(model, {0, 1, 1});
    EXPECT_TRUE(model.IndentItemUnder(2, 1));
    ExpectLevels(model, {0, 1, 2});
    EXPECT_TRUE(model.HasChildren(0));
    EXPECT_TRUE(model.HasChildren(1));
    EXPECT_FALSE(model.HasChildren(2));

    EXPECT_TRUE(model.ToggleCollapsed(0));
    EXPECT_TRUE(model.Items()[0].collapsed);
    EXPECT_FALSE(model.ToggleCollapsed(2));
    EXPECT_FALSE(model.Items()[2].collapsed);

    EXPECT_TRUE(model.OutdentItem(1));
    ExpectLevels(model, {0, 0, 1});
    EXPECT_FALSE(model.HasChildren(0));
    EXPECT_FALSE(model.Items()[0].collapsed);
    EXPECT_TRUE(model.HasChildren(1));
    AssertInvariants(model);

    EXPECT_TRUE(model.OutdentItem(2));
    ExpectLevels(model, {0, 0, 0});
    EXPECT_FALSE(model.HasChildren(1));
    AssertInvariants(model);
}

void CollapsedFlagsAreClearedWhenSubtreesDisappear() {
    TodoModel model;
    model.AddActive(L"A", 0);
    model.AddActive(L"A.1", 1);
    model.AddActive(L"B", 0);
    EXPECT_TRUE(model.ToggleCollapsed(0));
    EXPECT_TRUE(model.Items()[0].collapsed);

    model.Remove(1);
    ExpectTexts(model, {L"A", L"B"});
    ExpectCollapsed(model, {false, false});
    AssertInvariants(model);

    model.AddActive(L"B.1", 1);
    EXPECT_TRUE(model.ToggleCollapsed(1));
    model.SetDone(2, true);
    EXPECT_FALSE(model.Items()[1].collapsed);
    AssertInvariants(model);
}

const TestCase kTests[] = {
    {"InsertAfterCurrentSubtreeCanCreateSiblingBeforeNextRoot", InsertAfterCurrentSubtreeCanCreateSiblingBeforeNextRoot},
    {"SubtreeDoneUndoMovesWholeBlockAndRebasesLevels", SubtreeDoneUndoMovesWholeBlockAndRebasesLevels},
    {"CompletingNestedSubtreeDoesNotConsumeFollowingSiblings", CompletingNestedSubtreeDoesNotConsumeFollowingSiblings},
    {"RemoveDeletesActiveAndCompletedSubtrees", RemoveDeletesActiveAndCompletedSubtrees},
    {"MoveActiveReordersWholeSubtreeAndRejectsDropsInsideItself", MoveActiveReordersWholeSubtreeAndRejectsDropsInsideItself},
    {"IndentOutdentAndCollapseRespectTreeBoundaries", IndentOutdentAndCollapseRespectTreeBoundaries},
    {"CollapsedFlagsAreClearedWhenSubtreesDisappear", CollapsedFlagsAreClearedWhenSubtreesDisappear},
};

} // namespace

int main() {
    return RunTests("todo_model_tree", kTests);
}
