#include "StoreFormat.h"
#include "todo_model_test_utils.h"

#include <string>

using namespace xtodo_test;

namespace {

void ExpectDefaultUi(const UiState& ui) {
    EXPECT_TRUE(ui.alwaysOnTop);
    EXPECT_EQ(ui.mountMode, std::string("normal"));
    EXPECT_EQ(ui.lang, std::string(""));
    EXPECT_EQ(ui.themeMode, std::string("builtin"));
    EXPECT_EQ(ui.themeId, std::string("paper"));
    EXPECT_EQ(ui.lightThemeId, std::string("paper"));
    EXPECT_EQ(ui.darkThemeId, std::string("paper"));
    EXPECT_EQ(ui.capsuleStyle, std::string("slim"));
    EXPECT_EQ(ui.capsuleDockEdge, std::string("right"));
    EXPECT_NEAR(ui.capsuleDockT, 0.5, 0.000001);
    EXPECT_EQ(ui.capsuleMonitor, std::string(""));
    EXPECT_EQ(ui.activeView, std::string("list"));
    EXPECT_EQ(ui.calendarDay, std::string(""));
    EXPECT_TRUE(ui.calendarView == CalendarViewMode::Day);
    EXPECT_TRUE(ui.backupDir.empty());
    EXPECT_EQ(ui.backupLastEpoch, 0);
}

void SerializeRoundTripPreservesMultiListModelUiAndGeometry() {
    TodoModel model;
    CalendarModel calendar;
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
    ui.activeView = "calendar";
    ui.calendarDay = "2026-06-22";
    ui.calendarView = CalendarViewMode::Week;
    ui.backupDir = L"C:\\Backups\\X-TODO 数据";
    ui.backupLastEpoch = 1782180000;

    const int blockA = calendar.AddBlock("2026-06-22", 9 * 60 + 7, 10 * 60 + 3, L"Plan\tA");
    const int blockB = calendar.AddBlock("2026-06-23", 18 * 60, 19 * 60 + 30, L"Tomorrow\\B");
    EXPECT_TRUE(blockA > 0);
    EXPECT_TRUE(blockB > blockA);

    const std::string text = StoreFormat::Serialize(model, calendar, geom, ui);
    EXPECT_TRUE(text.find("\"currentList\": \"list-1\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"activeView\": \"calendar\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"calendarDay\": \"2026-06-22\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"calendarView\": \"week\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"backupLastEpoch\": 1782180000") != std::string::npos);
    EXPECT_TRUE(text.find("X-TODO 数据") != std::string::npos);

    TodoModel loaded;
    CalendarModel loadedCalendar;
    WindowGeometry loadedGeom;
    UiState loadedUi;
    EXPECT_TRUE(StoreFormat::Parse(text, loaded, loadedCalendar, loadedGeom, loadedUi));

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
    EXPECT_EQ(loadedUi.activeView, std::string("calendar"));
    EXPECT_EQ(loadedUi.calendarDay, std::string("2026-06-22"));
    EXPECT_TRUE(loadedUi.calendarView == CalendarViewMode::Week);
    EXPECT_EQ(loadedUi.backupDir, std::wstring(L"C:\\Backups\\X-TODO 数据"));
    EXPECT_EQ(loadedUi.backupLastEpoch, 1782180000);

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

    const auto today = loadedCalendar.BlocksForDay("2026-06-22");
    const auto tomorrow = loadedCalendar.BlocksForDay("2026-06-23");
    EXPECT_EQ(today.size(), static_cast<size_t>(1));
    EXPECT_EQ(tomorrow.size(), static_cast<size_t>(1));
    EXPECT_EQ(today[0]->startMinute, 9 * 60 + 7);
    EXPECT_EQ(today[0]->endMinute, 10 * 60 + 3);
    EXPECT_EQ(today[0]->title, std::wstring(L"Plan\tA"));
    EXPECT_EQ(tomorrow[0]->title, std::wstring(L"Tomorrow\\B"));
}

void UnicodeTitlesAndItemsRoundTripWithoutEscaping() {
    TodoModel model;
    CalendarModel calendar;
    EXPECT_TRUE(model.RenameList(0, L"默认"));
    model.AddActive(L"睡觉 / sleep \"now\"", 0);
    WindowGeometry geom;
    UiState ui;
    const int block = calendar.AddBlock("2026-06-22", 945, 1020, L"睡觉");
    EXPECT_TRUE(block > 0);

    const std::string text = StoreFormat::Serialize(model, calendar, geom, ui);
    // 非 ASCII 直接以 UTF-8 落盘，不转义为 \uXXXX
    EXPECT_TRUE(text.find("默认") != std::string::npos);
    EXPECT_TRUE(text.find("睡觉") != std::string::npos);

    TodoModel loaded;
    CalendarModel loadedCalendar;
    WindowGeometry loadedGeom;
    UiState loadedUi;
    EXPECT_TRUE(StoreFormat::Parse(text, loaded, loadedCalendar, loadedGeom, loadedUi));
    EXPECT_EQ(loaded.CurrentList().title, std::wstring(L"默认"));
    ExpectTexts(loaded, {L"睡觉 / sleep \"now\""});
    EXPECT_EQ(loadedCalendar.BlocksForDay("2026-06-22")[0]->title, std::wstring(L"睡觉"));
    AssertInvariants(loaded);
}

void CapsuleStyleBarAndPipRoundTrip() {
    for (const char* style : {"bar", "pip"}) {
        TodoModel model;
        CalendarModel calendar;
        WindowGeometry geom;
        UiState ui;
        ui.capsuleStyle = style;
        const std::string text = StoreFormat::Serialize(model, calendar, geom, ui);
        TodoModel loaded;
        CalendarModel loadedCalendar;
        WindowGeometry loadedGeom;
        UiState loadedUi;
        EXPECT_TRUE(StoreFormat::Parse(text, loaded, loadedCalendar, loadedGeom, loadedUi));
        EXPECT_EQ(loadedUi.capsuleStyle, std::string(style));
    }
}

void UiParsingValidatesEnumsAndClampsRanges() {
    const std::string text = R"({
      "ui": {
        "themeId": "custom.current",
        "lightThemeId": "custom.light",
        "darkThemeId": "custom.dark",
        "mount": "taskbar",
        "lang": "fr",
        "capsuleStyle": "weird",
        "capsuleDockEdge": "top",
        "capsuleDockT": 2.5,
        "activeView": "popup",
        "calendarDay": "bad",
        "calendarView": "bogus",
        "backupDir": "D:\\Backup",
        "backupLastEpoch": -7
      },
      "lists": [ { "id": "inbox", "title": "Inbox", "items": [] } ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui));

    EXPECT_EQ(ui.themeId, std::string("custom.current"));
    EXPECT_EQ(ui.lightThemeId, std::string("custom.light"));
    EXPECT_EQ(ui.darkThemeId, std::string("custom.dark"));
    EXPECT_EQ(ui.mountMode, std::string("normal"));   // taskbar 迁移为 normal
    EXPECT_EQ(ui.lang, std::string(""));              // fr 非法 → 保留默认
    EXPECT_EQ(ui.capsuleStyle, std::string("slim"));  // 非法 → 默认
    EXPECT_EQ(ui.capsuleDockEdge, std::string("right"));
    EXPECT_NEAR(ui.capsuleDockT, 1.0, 0.000001);      // 2.5 → clamp 到 1.0
    EXPECT_EQ(ui.activeView, std::string("list"));    // popup 非法 → 默认
    EXPECT_EQ(ui.calendarDay, std::string(""));       // bad 非法 → 默认
    EXPECT_TRUE(ui.calendarView == CalendarViewMode::Day); // bogus → day
    EXPECT_EQ(ui.backupDir, std::wstring(L"D:\\Backup"));
    EXPECT_EQ(ui.backupLastEpoch, 0);                 // 负数时间戳 → 默认
    AssertInvariants(model);
}

void MissingCurrentListFallsBackAndDockTClamps() {
    const std::string text = R"({
      "ui": { "currentList": "list-404", "capsuleDockT": -0.25 },
      "lists": [
        { "id": "inbox", "title": "Inbox", "items": [
          { "text": "Inbox item", "done": false, "level": 0, "collapsed": false } ] },
        { "id": "list-7", "title": "Work", "completedExpanded": true, "items": [
          { "text": "Done work", "done": true, "level": 0, "collapsed": false } ] }
      ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui));

    EXPECT_EQ(model.ListCount(), 2);
    EXPECT_EQ(model.CurrentListIndex(), 0); // list-404 不存在 → 回退首个
    EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
    EXPECT_TRUE(model.ListAt(1)->completedExpanded);
    EXPECT_NEAR(ui.capsuleDockT, 0.0, 0.000001);
    ExpectTexts(model, {L"Inbox item"});
    AssertInvariants(model);
}

void EmptyAndWhitespaceLeaveSafeDefaults() {
    for (const std::string& blank : {std::string(""), std::string("   \n\t \r\n")}) {
        TodoModel model;
        CalendarModel calendar;
        WindowGeometry geom;
        UiState ui;
        EXPECT_TRUE(StoreFormat::Parse(blank, model, calendar, geom, ui));

        EXPECT_EQ(model.ListCount(), 1);
        EXPECT_EQ(model.CurrentList().id, std::string("inbox"));
        EXPECT_EQ(model.Count(), 0);
        EXPECT_FALSE(geom.valid);
        ExpectDefaultUi(ui);
        AssertInvariants(model);
    }
}

void InvalidJsonAndNonObjectRootFail() {
    const std::string cases[] = {
        "{ not valid json",
        "[1, 2, 3]",      // 顶层数组而非对象
        "\"a string\"",   // 顶层标量
        "42",
    };
    for (const std::string& bad : cases) {
        TodoModel model;
        CalendarModel calendar;
        WindowGeometry geom;
        UiState ui;
        EXPECT_FALSE(StoreFormat::Parse(bad, model, calendar, geom, ui));
    }
}

void DeeplyNestedJsonIsRejected() {
    std::string bomb;
    for (int i = 0; i < 200; ++i) bomb += '[';
    for (int i = 0; i < 200; ++i) bomb += ']';
    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    EXPECT_FALSE(StoreFormat::Parse(bomb, model, calendar, geom, ui));
}

void CalendarRowsIgnoreInvalidAndNormalizeDuplicateIds() {
    const std::string text = R"({
      "calendar": [
        { "id": 7, "day": "2026-06-22", "start": 60, "end": 120, "title": "first" },
        { "id": 7, "day": "2026-06-22", "start": 130, "end": 131, "title": "duplicate id" },
        { "id": 8, "day": "bad-day", "start": 60, "end": 120, "title": "ignored" },
        { "id": 9, "day": "2026-06-22", "start": 200, "end": 200, "title": "normalized" }
      ],
      "lists": [ { "id": "inbox", "title": "Inbox", "items": [] } ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui));

    const auto blocks = calendar.BlocksForDay("2026-06-22");
    EXPECT_EQ(blocks.size(), static_cast<size_t>(3)); // bad-day 行被丢弃
    EXPECT_EQ(blocks[0]->id, 7);
    EXPECT_EQ(blocks[0]->title, std::wstring(L"first"));
    EXPECT_TRUE(blocks[1]->id != 7);                  // 重复 id 被重新分配
    EXPECT_EQ(blocks[2]->startMinute, 200);
    EXPECT_EQ(blocks[2]->endMinute, 201);             // end<=start → 归一化为 start+1
    AssertInvariants(model);
}

void MalformedFieldsFallBackToDefaultsWithoutThrowing() {
    // 类型全错但 JSON 合法：解析须成功并逐字段回退默认，不抛异常。
    const std::string text = R"({
      "window": { "x": "nope", "y": null, "w": 100, "h": 200 },
      "ui": { "alwaysOnTop": "yes", "capsuleDockT": "half", "currentList": 5,
              "backupDir": 17, "backupLastEpoch": "soon" },
      "calendar": "not an array",
      "lists": [
        { "id": "inbox", "title": 123, "items": [
          { "text": "ok", "done": "no", "level": "deep", "collapsed": 1 },
          "not an object"
        ] }
      ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui));

    // window 部分字段非法：x/y 回退 0，w/h 取整数，整体标记 valid
    EXPECT_TRUE(geom.valid);
    EXPECT_EQ(geom.x, 0);
    EXPECT_EQ(geom.y, 0);
    EXPECT_EQ(geom.w, 100);
    EXPECT_EQ(geom.h, 200);
    EXPECT_TRUE(ui.alwaysOnTop);                 // "yes" 非 bool → 默认
    EXPECT_NEAR(ui.capsuleDockT, 0.5, 0.000001); // "half" 非数 → 默认
    EXPECT_TRUE(ui.backupDir.empty());            // backupDir 非字符串 → 默认
    EXPECT_EQ(ui.backupLastEpoch, 0);             // backupLastEpoch 非整数 → 默认
    EXPECT_EQ(model.ListCount(), 1);
    EXPECT_EQ(model.CurrentList().title, std::wstring(L"默认")); // title 非字符串 → 空 → 默认标题
    ExpectTexts(model, {L"ok"});                  // 字符串项保留，非对象项跳过
    ExpectDones(model, {false});
    ExpectLevels(model, {0});
    AssertInvariants(model);
}

const TestCase kTests[] = {
    {"SerializeRoundTripPreservesMultiListModelUiAndGeometry", SerializeRoundTripPreservesMultiListModelUiAndGeometry},
    {"UnicodeTitlesAndItemsRoundTripWithoutEscaping", UnicodeTitlesAndItemsRoundTripWithoutEscaping},
    {"CapsuleStyleBarAndPipRoundTrip", CapsuleStyleBarAndPipRoundTrip},
    {"UiParsingValidatesEnumsAndClampsRanges", UiParsingValidatesEnumsAndClampsRanges},
    {"MissingCurrentListFallsBackAndDockTClamps", MissingCurrentListFallsBackAndDockTClamps},
    {"EmptyAndWhitespaceLeaveSafeDefaults", EmptyAndWhitespaceLeaveSafeDefaults},
    {"InvalidJsonAndNonObjectRootFail", InvalidJsonAndNonObjectRootFail},
    {"DeeplyNestedJsonIsRejected", DeeplyNestedJsonIsRejected},
    {"CalendarRowsIgnoreInvalidAndNormalizeDuplicateIds", CalendarRowsIgnoreInvalidAndNormalizeDuplicateIds},
    {"MalformedFieldsFallBackToDefaultsWithoutThrowing", MalformedFieldsFallBackToDefaultsWithoutThrowing},
};

} // namespace

int main() {
    return RunTests("store_format", kTests);
}
