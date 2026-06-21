#include "StoreFormat.h"
#include "todo_model_test_utils.h"

#include <string>

using namespace xtodo_test;

namespace {

void ExpectDefaultUi(const UiState& ui) {
    EXPECT_FALSE(ui.completedExpanded);
    EXPECT_TRUE(ui.alwaysOnTop);
    EXPECT_EQ(ui.mountMode, std::string("normal"));
    EXPECT_EQ(ui.lang, std::string(""));
    EXPECT_EQ(ui.themeMode, std::string("builtin"));
    EXPECT_EQ(ui.themeId, std::string("paper"));
    EXPECT_EQ(ui.lightThemeId, std::string("paper"));
    EXPECT_EQ(ui.darkThemeId, std::string("graphite"));
    EXPECT_EQ(ui.capsuleStyle, std::string("slim"));
    EXPECT_EQ(ui.capsuleDockEdge, std::string("right"));
    EXPECT_NEAR(ui.capsuleDockT, 0.5, 0.000001);
    EXPECT_EQ(ui.capsuleMonitor, std::string(""));
}

void EscapeRoundTripKeepsOneLogicalLinePerItem() {
    const std::wstring raw = L"line1\nline2\tbackslash\\carriage\rreturn";
    const std::wstring escaped = StoreFormat::Escape(raw);
    EXPECT_EQ(escaped, std::wstring(L"line1\\nline2\\tbackslash\\\\carriagereturn"));
    EXPECT_EQ(StoreFormat::Unescape(escaped), std::wstring(L"line1\nline2\tbackslash\\carriagereturn"));
    EXPECT_EQ(StoreFormat::Unescape(L"unknown\\xescape"), std::wstring(L"unknownxescape"));
}

void SerializeRoundTripPreservesV4MultiListModelUiAndGeometry() {
    TodoModel model;
    EXPECT_TRUE(model.RenameList(0, L"Inbox\tMain"));
    model.AddActive(L"Parent", 0);
    model.AddActive(L"Child with slash\\and tab\t", 1);
    EXPECT_TRUE(model.ToggleCollapsed(0));
    model.AddActive(L"Done in inbox", 0);
    model.SetDone(2, true);
    model.SetCurrentCompletedExpanded(true);

    const int work = model.AddList(L"Work\nList");
    model.AddActive(L"Work root", 0);
    model.AddActive(L"Work child", 1);
    model.AddActive(L"Completed work", 0);
    model.SetDone(2, true);
    EXPECT_TRUE(model.ToggleCollapsed(0));
    EXPECT_EQ(model.CurrentListIndex(), work);

    WindowGeometry geom{ -12, 34, 456, 789, true };
    UiState ui;
    ui.completedExpanded = true;
    ui.alwaysOnTop = false;
    ui.mountMode = "capsule";
    ui.lang = "en";
    ui.themeMode = "follow_system";
    ui.themeId = "custom.current";
    ui.lightThemeId = "mint";
    ui.darkThemeId = "custom.night";
    ui.capsuleStyle = "dot";
    ui.capsuleDockEdge = "left";
    ui.capsuleDockT = 0.1234567;
    ui.capsuleMonitor = "\\\\.\\DISPLAY1";

    const std::wstring text = StoreFormat::SerializeText(model, geom, ui);
    EXPECT_TRUE(text.find(L"XTODO v4\n") == 0);
    EXPECT_TRUE(text.find(L"Child with slash\\\\and tab\\t") != std::wstring::npos);
    EXPECT_TRUE(text.find(L"Work\\nList") != std::wstring::npos);
    EXPECT_TRUE(text.find(L"ui current_list=list-1") != std::wstring::npos);

    TodoModel loaded;
    WindowGeometry loadedGeom;
    UiState loadedUi;
    EXPECT_TRUE(StoreFormat::ParseText(text, loaded, loadedGeom, loadedUi));

    EXPECT_EQ(loadedGeom.x, -12);
    EXPECT_EQ(loadedGeom.y, 34);
    EXPECT_EQ(loadedGeom.w, 456);
    EXPECT_EQ(loadedGeom.h, 789);
    EXPECT_TRUE(loadedGeom.valid);

    EXPECT_FALSE(loadedUi.alwaysOnTop);
    EXPECT_EQ(loadedUi.mountMode, std::string("capsule"));
    EXPECT_EQ(loadedUi.lang, std::string("en"));
    EXPECT_EQ(loadedUi.themeMode, std::string("follow_system"));
    EXPECT_EQ(loadedUi.themeId, std::string("custom.current"));
    EXPECT_EQ(loadedUi.lightThemeId, std::string("mint"));
    EXPECT_EQ(loadedUi.darkThemeId, std::string("custom.night"));
    EXPECT_EQ(loadedUi.capsuleStyle, std::string("dot"));
    EXPECT_EQ(loadedUi.capsuleDockEdge, std::string("left"));
    EXPECT_NEAR(loadedUi.capsuleDockT, 0.123457, 0.000001);
    EXPECT_EQ(loadedUi.capsuleMonitor, std::string("\\\\.\\DISPLAY1"));

    EXPECT_EQ(loaded.ListCount(), 2);
    EXPECT_EQ(loaded.CurrentListIndex(), 1);
    EXPECT_EQ(loaded.ListAt(0)->title, std::wstring(L"Inbox\tMain"));
    EXPECT_TRUE(loaded.ListAt(0)->completedExpanded);
    EXPECT_EQ(loaded.CurrentList().title, std::wstring(L"Work\nList"));
    ExpectTexts(loaded, {L"Work root", L"Work child", L"Completed work"});
    ExpectLevels(loaded, {0, 1, 0});
    ExpectDones(loaded, {false, false, true});
    ExpectCollapsed(loaded, {true, false, false});
    AssertInvariants(loaded);
}

void LegacyV1SingleListLoadsEscapedItemsAndCompletedExpanded() {
    const std::wstring text =
        L"XTODO v1\n"
        L"item 1 done\\titem\n"
        L"item 0 active\\nitem\n"
        L"ui completed_expanded=1\n";

    TodoModel model;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));

    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    ExpectTexts(model, {L"active\nitem", L"done\titem"});
    ExpectDones(model, {false, true});
    EXPECT_TRUE(model.CurrentList().completedExpanded);
    EXPECT_FALSE(geom.valid);
    AssertInvariants(model);
}

void V2V3AndV4MigrationsNormalizeLevelsAndCollapsedFlags() {
    {
        const std::wstring text =
            L"XTODO v2\n"
            L"item 0 first\n"
            L"item 1 done\n";
        TodoModel model;
        WindowGeometry geom;
        UiState ui;
        EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));
        ExpectTexts(model, {L"first", L"done"});
        ExpectLevels(model, {0, 0});
        ExpectDones(model, {false, true});
        AssertInvariants(model);
    }
    {
        const std::wstring text =
            L"XTODO v3\n"
            L"list inbox 0 Inbox\n"
            L"item 0 99 root\n"
            L"item 0 99 child\n"
            L"item 1 3 done child\n";
        TodoModel model;
        WindowGeometry geom;
        UiState ui;
        EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));
        ExpectTexts(model, {L"root", L"child", L"done child"});
        ExpectLevels(model, {0, 1, 0});
        ExpectDones(model, {false, false, true});
        AssertInvariants(model);
    }
    {
        const std::wstring text =
            L"XTODO v4\n"
            L"list inbox 0 Inbox\n"
            L"item 0 0 1 parent\n"
            L"item 0 1 1 child leaf\n"
            L"item 1 0 1 done parent\n"
            L"item 1 1 0 done child\n";
        TodoModel model;
        WindowGeometry geom;
        UiState ui;
        EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));
        ExpectTexts(model, {L"parent", L"child leaf", L"done parent", L"done child"});
        ExpectCollapsed(model, {true, false, true, false});
        AssertInvariants(model);
    }
}

void UiParsingUsesExactKeysAndValidatesValues() {
    const std::wstring text =
        L"XTODO v4\n"
        L"ui theme_id=custom.current\n"
        L"ui light_theme_id=custom.light\n"
        L"ui dark_theme_id=custom.dark\n"
        L"ui theme_id=非法\n"
        L"ui mount=taskbar\n"
        L"ui mount=invalid\n"
        L"ui lang=zh\n"
        L"ui lang=fr\n"
        L"ui capsule_style=dot\n"
        L"ui capsule_style=weird\n"
        L"ui capsule_dock_edge=left\n"
        L"ui capsule_dock_edge=top\n"
        L"ui capsule_dock_t=2.5\n"
        L"ui unknown=value\n"
        L"list inbox 0 Inbox\n";

    TodoModel model;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));

    EXPECT_EQ(ui.themeId, std::string("custom.current"));
    EXPECT_EQ(ui.lightThemeId, std::string("custom.light"));
    EXPECT_EQ(ui.darkThemeId, std::string("custom.dark"));
    EXPECT_EQ(ui.mountMode, std::string("normal"));
    EXPECT_EQ(ui.lang, std::string("zh"));
    EXPECT_EQ(ui.capsuleStyle, std::string("dot"));
    EXPECT_EQ(ui.capsuleDockEdge, std::string("left"));
    EXPECT_NEAR(ui.capsuleDockT, 1.0, 0.000001);
    AssertInvariants(model);
}

void MissingCurrentListFallsBackAndUiBoundsRemainStable() {
    const std::wstring text =
        L"XTODO v4\n"
        L"ui current_list=list-404\n"
        L"ui capsule_dock_t=-0.25\n"
        L"ui capsule_dock_t=nan\n"
        L"list inbox 0 Inbox\n"
        L"item 0 0 0 Inbox item\n"
        L"list list-7 1 Work\n"
        L"item 1 0 0 Done work\n";

    TodoModel model;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::ParseText(text, model, geom, ui));

    EXPECT_EQ(model.ListCount(), 2);
    EXPECT_EQ(model.CurrentListIndex(), 0);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    EXPECT_TRUE(model.ListAt(1)->completedExpanded);
    EXPECT_NEAR(ui.capsuleDockT, 0.0, 0.000001);
    ExpectTexts(model, {L"Inbox item"});
    AssertInvariants(model);
}

void EmptyTextLeavesSafeDefaults() {
    TodoModel model;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::ParseText(L"", model, geom, ui));

    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    EXPECT_EQ(model.Count(), 0);
    EXPECT_FALSE(geom.valid);
    ExpectDefaultUi(ui);
    AssertInvariants(model);
}

const TestCase kTests[] = {
    {"EscapeRoundTripKeepsOneLogicalLinePerItem", EscapeRoundTripKeepsOneLogicalLinePerItem},
    {"SerializeRoundTripPreservesV4MultiListModelUiAndGeometry", SerializeRoundTripPreservesV4MultiListModelUiAndGeometry},
    {"LegacyV1SingleListLoadsEscapedItemsAndCompletedExpanded", LegacyV1SingleListLoadsEscapedItemsAndCompletedExpanded},
    {"V2V3AndV4MigrationsNormalizeLevelsAndCollapsedFlags", V2V3AndV4MigrationsNormalizeLevelsAndCollapsedFlags},
    {"UiParsingUsesExactKeysAndValidatesValues", UiParsingUsesExactKeysAndValidatesValues},
    {"MissingCurrentListFallsBackAndUiBoundsRemainStable", MissingCurrentListFallsBackAndUiBoundsRemainStable},
    {"EmptyTextLeavesSafeDefaults", EmptyTextLeavesSafeDefaults},
};

} // namespace

int main() {
    return RunTests("store_format", kTests);
}
