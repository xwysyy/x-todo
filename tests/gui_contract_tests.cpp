#include "EditIntent.h"
#include "CalendarLayout.h"
#include "GeometryPolicy.h"
#include "MenuModel.h"
#include "Theme.h"
#include "ViewLayout.h"
#include "WindowHitTest.h"
#include "test_framework.h"

#include <string>
#include <vector>

using namespace xtodo_test;

namespace {

float Mid(float a, float b) {
    return (a + b) / 2.0f;
}

bool HasCommand(const std::vector<GuiMenu::Item>& items, GuiMenu::Command command) {
    for (const GuiMenu::Item& item : items) {
        if (!item.separator && item.cmd == command) return true;
    }
    return false;
}

const GuiMenu::Item* FindCommand(const std::vector<GuiMenu::Item>& items, GuiMenu::Command command) {
    for (const GuiMenu::Item& item : items) {
        if (!item.separator && item.cmd == command) return &item;
    }
    return nullptr;
}

void ExpectChildControlLeavesRoundedFrameSafeArea(const Gui::Rect& frame,
                                                  const Gui::Rect& child,
                                                  float minInset) {
    EXPECT_TRUE(child.left >= frame.left + minInset);
    EXPECT_TRUE(child.top >= frame.top + minInset);
    EXPECT_TRUE(child.right <= frame.right - minInset);
    EXPECT_TRUE(child.bottom <= frame.bottom - minInset);
    EXPECT_TRUE(child.Width() > 0.0f);
    EXPECT_TRUE(child.Height() > 0.0f);
}

GuiHit::Input BaseHitInput() {
    GuiHit::Input input;
    input.width = 260.0f;
    input.height = 340.0f;
    input.dpiScale = 1.0f;
    input.titleHeight = Theme::kTitleH;
    input.resizeEdge = Theme::kResizeEdge;
    return input;
}

void NonClientHitTestPrioritizesTitleButtonsOverResizeBand() {
    const GuiLayout::TitleButtons title = GuiLayout::ComputeTitleButtons(260.0f, 1.0f);

    GuiHit::Input input = BaseHitInput();
    input.menu = title.menu;
    input.theme = title.theme;
    input.pin = title.pin;
    input.close = title.close;
    input.x = Mid(title.menu.left, title.menu.right);
    input.y = title.menu.top + 1.0f;

    EXPECT_TRUE(input.y < Theme::kResizeEdge);
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Client);
}

void NonClientHitTestMapsEdgesCornersCaptionAndCapsule() {
    GuiHit::Input input = BaseHitInput();
    input.x = 0.0f;
    input.y = 0.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::TopLeft);

    input.x = 259.0f;
    input.y = 339.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::BottomRight);

    input.x = 20.0f;
    input.y = 50.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Client);

    input.x = 40.0f;
    input.y = 20.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Caption);

    input.capsuleMode = true;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Client);
}

void NonClientHitTestScalesResizeEdgeAndCanForceClient() {
    GuiHit::Input input = BaseHitInput();
    input.dpiScale = 1.5f;
    input.x = 11.0f;
    input.y = 80.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Left);

    input.x = 12.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Client);

    input.forceClient = true;
    input.x = 0.0f;
    input.y = 0.0f;
    EXPECT_EQ(GuiHit::HitTestNonClient(input), GuiHit::NonClientHit::Client);
}

void GeometryPolicyScalesMinimumAndRejectsSubminimumCaptures() {
    const GuiGeometry::Size min = GuiGeometry::MinimumTrackSize(1.5f);
    EXPECT_EQ(min.w, 330);
    EXPECT_EQ(min.h, 240);

    EXPECT_TRUE(GuiGeometry::AcceptsGeometrySize(330, 240, 1.5f));
    EXPECT_FALSE(GuiGeometry::AcceptsGeometrySize(329, 240, 1.5f));
    EXPECT_FALSE(GuiGeometry::AcceptsGeometrySize(330, 239, 1.5f));

    EXPECT_TRUE(GuiGeometry::AcceptsLoadedGeometrySize(330, 240, 1.5f));
    EXPECT_FALSE(GuiGeometry::AcceptsLoadedGeometrySize(329, 240, 1.5f));
    EXPECT_FALSE(GuiGeometry::AcceptsLoadedGeometrySize(GuiGeometry::kMaxLoadedWindowW + 1, 240, 1.0f));
    EXPECT_FALSE(GuiGeometry::AcceptsLoadedGeometrySize(330, GuiGeometry::kMaxLoadedWindowH + 1, 1.0f));
}

void GeometryCapturePolicyMatchesWindowModes() {
    GuiGeometry::CaptureInput input;
    input.w = 260;
    input.h = 340;
    input.dpiScale = 1.0f;

    input.mountMode = GuiGeometry::MountMode::Normal;
    GuiGeometry::CaptureDecision decision = GuiGeometry::DecideCapture(input);
    EXPECT_TRUE(decision.accept);
    EXPECT_TRUE(decision.capturePosition);
    EXPECT_FALSE(decision.captureDock);

    input.mountMode = GuiGeometry::MountMode::Desktop;
    decision = GuiGeometry::DecideCapture(input);
    EXPECT_TRUE(decision.accept);
    EXPECT_TRUE(decision.capturePosition);
    EXPECT_FALSE(decision.captureDock);

    input.mountMode = GuiGeometry::MountMode::Capsule;
    input.capsuleExpanded = false;
    decision = GuiGeometry::DecideCapture(input);
    EXPECT_FALSE(decision.accept);

    input.capsuleExpanded = true;
    decision = GuiGeometry::DecideCapture(input);
    EXPECT_TRUE(decision.accept);
    EXPECT_FALSE(decision.capturePosition);
    EXPECT_TRUE(decision.captureDock);

    input.animActive = true;
    decision = GuiGeometry::DecideCapture(input);
    EXPECT_FALSE(decision.accept);

    EXPECT_TRUE(GuiGeometry::ShouldCaptureBeforeModeSwitch(false));
    EXPECT_FALSE(GuiGeometry::ShouldCaptureBeforeModeSwitch(true));
}

void ExpandedGeometryUsesStoredSizeAndClampsToWorkArea() {
    GuiGeometry::Size size = GuiGeometry::ExpandedSize(true, 500, 420, 800, 600);
    EXPECT_EQ(size.w, 500);
    EXPECT_EQ(size.h, 420);

    size = GuiGeometry::ExpandedSize(true, 900, 700, 800, 600);
    EXPECT_EQ(size.w, 800);
    EXPECT_EQ(size.h, 600);

    size = GuiGeometry::ExpandedSize(false, 0, 0, 0, 0);
    EXPECT_EQ(size.w, GuiGeometry::kDefaultWindowW + 40);
    EXPECT_EQ(size.h, GuiGeometry::kDefaultWindowH + 40);
}

void TitleButtonLayoutOrdersActionsAndStaysInsideWindow() {
    const GuiLayout::TitleButtons title = GuiLayout::ComputeTitleButtons(260.0f, 1.0f);

    EXPECT_TRUE(title.menu.left < title.theme.left);
    EXPECT_TRUE(title.theme.left < title.calendar.left);
    EXPECT_TRUE(title.calendar.left < title.pin.left);
    EXPECT_TRUE(title.pin.left < title.close.left);
    EXPECT_EQ(title.close.right, 252.0f);
    EXPECT_EQ(title.close.Width(), 24.0f);
    EXPECT_EQ(title.menu.top, 5.0f);
    EXPECT_EQ(title.close.bottom, 29.0f);
}

void ChromeHitTestCoversTitleButtonsTabsAndAddList() {
    const GuiLayout::TitleButtons title = GuiLayout::ComputeTitleButtons(360.0f, 1.0f);
    const std::vector<GuiLayout::TabMetric> metrics = {
        GuiLayout::TabMetric{ 0, 5, 2 },
        GuiLayout::TabMetric{ 1, 12, 0 },
    };
    const GuiLayout::TabStrip strip = GuiLayout::ComputeTabStrip(360.0f, 1.0f, metrics);
    EXPECT_EQ(strip.tabs.size(), static_cast<size_t>(2));

    GuiLayout::ChromeHitResult hit = GuiLayout::HitTestChrome(
        Mid(title.menu.left, title.menu.right), Mid(title.menu.top, title.menu.bottom),
        1.0f, title, strip.addList, strip.tabs);
    EXPECT_EQ(hit.kind, GuiLayout::ChromeHit::Menu);

    // Calendar is now a titlebar toggle button, not a tab.
    hit = GuiLayout::HitTestChrome(
        Mid(title.calendar.left, title.calendar.right), Mid(title.calendar.top, title.calendar.bottom),
        1.0f, title, strip.addList, strip.tabs);
    EXPECT_EQ(hit.kind, GuiLayout::ChromeHit::Calendar);

    hit = GuiLayout::HitTestChrome(
        Mid(strip.addList.left, strip.addList.right), Mid(strip.addList.top, strip.addList.bottom),
        1.0f, title, strip.addList, strip.tabs);
    EXPECT_EQ(hit.kind, GuiLayout::ChromeHit::AddList);

    hit = GuiLayout::HitTestChrome(
        Mid(strip.tabs[0].rect.left, strip.tabs[0].rect.right),
        Mid(strip.tabs[0].rect.top, strip.tabs[0].rect.bottom),
        1.0f, title, strip.addList, strip.tabs);
    EXPECT_EQ(hit.kind, GuiLayout::ChromeHit::ListTab);
    EXPECT_EQ(hit.listIndex, 0);

    hit = GuiLayout::HitTestChrome(
        Mid(strip.tabs[1].rect.left, strip.tabs[1].rect.right),
        Mid(strip.tabs[1].rect.top, strip.tabs[1].rect.bottom),
        1.0f, title, strip.addList, strip.tabs);
    EXPECT_EQ(hit.kind, GuiLayout::ChromeHit::ListTab);
    EXPECT_EQ(hit.listIndex, 1);
}

void TabStripNeverOverlapsAddList() {
    std::vector<GuiLayout::TabMetric> metrics;
    for (int i = 0; i < 12; ++i)
        metrics.push_back(GuiLayout::TabMetric{ i, 24, i + 1 });

    const GuiLayout::TabStrip strip = GuiLayout::ComputeTabStrip(260.0f, 1.0f, metrics);
    EXPECT_TRUE(!strip.tabs.empty());
    for (const GuiLayout::TabRect& tab : strip.tabs) {
        EXPECT_TRUE(tab.rect.right <= strip.addList.left - 6.0f);
        EXPECT_TRUE(tab.rect.left < tab.rect.right);
        EXPECT_TRUE(tab.rect.top < tab.rect.bottom);
    }
    EXPECT_TRUE(strip.tabs.size() < metrics.size());
}

void EmptyActivePromptFillsViewportForCentering() {
    // Empty list region spans the content viewport; the prompt is centered within it.
    const Gui::Rect r = GuiLayout::ComputeEmptyActivePrompt(260.0f, 400.0f, 1.0f);
    EXPECT_EQ(r.left, 14.0f);
    EXPECT_EQ(r.top, 0.0f);
    EXPECT_EQ(r.right, 246.0f);
    EXPECT_EQ(r.bottom, 400.0f);

    // A very short viewport still gets a usable minimum height.
    const Gui::Rect tiny = GuiLayout::ComputeEmptyActivePrompt(260.0f, 80.0f, 1.0f);
    EXPECT_EQ(tiny.bottom, 160.0f);
}

void RowLayoutKeepsIndentControlsAndHitTestingInLockstep() {
    const GuiLayout::RowControls root = GuiLayout::ComputeRowControls(260.0f, 0.0f, 34.0f, 0, 1.0f);
    const GuiLayout::RowControls child = GuiLayout::ComputeRowControls(260.0f, 0.0f, 34.0f, 2, 1.0f);

    EXPECT_TRUE(child.check.left > root.check.left);
    EXPECT_TRUE(child.text.left > root.text.left);
    EXPECT_TRUE(child.text.right < child.del.left);
    EXPECT_TRUE(child.del.right < child.handle.left);

    EXPECT_EQ(GuiLayout::HitTestRowControls(child, Mid(child.disclosure.left, child.disclosure.right),
                                            Mid(child.disclosure.top, child.disclosure.bottom),
                                            true, false),
              GuiLayout::RowHit::TreeToggle);
    EXPECT_EQ(GuiLayout::HitTestRowControls(child, Mid(child.disclosure.left, child.disclosure.right),
                                            Mid(child.disclosure.top, child.disclosure.bottom),
                                            false, false),
              GuiLayout::RowHit::None);
    EXPECT_EQ(GuiLayout::HitTestRowControls(child, Mid(child.check.left, child.check.right),
                                            Mid(child.check.top, child.check.bottom),
                                            true, false),
              GuiLayout::RowHit::Check);
    EXPECT_EQ(GuiLayout::HitTestRowControls(child, Mid(child.text.left, child.text.right),
                                            Mid(child.text.top, child.text.bottom),
                                            true, false),
              GuiLayout::RowHit::Text);
    EXPECT_EQ(GuiLayout::HitTestRowControls(child, Mid(child.handle.left, child.handle.right),
                                            Mid(child.handle.top, child.handle.bottom),
                                            true, true),
              GuiLayout::RowHit::None);
}

void EditIntentMapsKeyboardWithoutLeakingControlCharacters() {
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::Enter, false), GuiEdit::Intent::CommitAndAddNext);
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::Escape, false), GuiEdit::Intent::Cancel);
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::Tab, false), GuiEdit::Intent::Indent);
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::Tab, true), GuiEdit::Intent::Outdent);
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::DeleteKey, false), GuiEdit::Intent::RefreshAfterDefault);
    EXPECT_EQ(GuiEdit::KeyDownIntent(GuiEdit::Key::Other, true), GuiEdit::Intent::None);

    EXPECT_TRUE(GuiEdit::SuppressChar(L'\t'));
    EXPECT_TRUE(GuiEdit::SuppressChar(L'\r'));
    EXPECT_TRUE(GuiEdit::SuppressChar(0x1B));
    EXPECT_FALSE(GuiEdit::SuppressChar(L'a'));
}

void TitleAndTrayMenusDoNotExposeListManagementCommands() {
    GuiMenu::State state;
    state.lang = Lang::En;
    state.autostart = true;
    state.mountMode = GuiMenu::MountMode::Capsule;
    state.capsuleStyle = GuiMenu::CapsuleStyle::Dot;
    state.listCount = 3;

    const std::vector<GuiMenu::Item> tray = GuiMenu::BuildTrayMenu(state);
    const std::vector<GuiMenu::Item> title = GuiMenu::BuildTitleMenu(state);

    EXPECT_TRUE(HasCommand(tray, GuiMenu::kCmdShow));
    EXPECT_FALSE(HasCommand(title, GuiMenu::kCmdShow));
    EXPECT_TRUE(HasCommand(tray, GuiMenu::kCmdExit));
    EXPECT_TRUE(HasCommand(title, GuiMenu::kCmdExit));

    EXPECT_FALSE(HasCommand(tray, GuiMenu::kCmdListRename));
    EXPECT_FALSE(HasCommand(tray, GuiMenu::kCmdListDelete));
    EXPECT_FALSE(HasCommand(title, GuiMenu::kCmdListRename));
    EXPECT_FALSE(HasCommand(title, GuiMenu::kCmdListDelete));

    const GuiMenu::Item* dot = FindCommand(title, GuiMenu::kCmdStyleDot);
    EXPECT_TRUE(dot != nullptr);
    EXPECT_TRUE(dot->checked);
}

void ListTabMenuOwnsRenameAndDeletePolicy() {
    std::vector<GuiMenu::Item> one = GuiMenu::BuildListTabMenu(Lang::Zh, 1);
    const GuiMenu::Item* deleteOne = FindCommand(one, GuiMenu::kCmdListDelete);
    EXPECT_TRUE(FindCommand(one, GuiMenu::kCmdListRename) != nullptr);
    EXPECT_TRUE(deleteOne != nullptr);
    EXPECT_FALSE(deleteOne->enabled);
    EXPECT_TRUE(deleteOne->danger);

    std::vector<GuiMenu::Item> many = GuiMenu::BuildListTabMenu(Lang::Zh, 2);
    const GuiMenu::Item* deleteMany = FindCommand(many, GuiMenu::kCmdListDelete);
    EXPECT_TRUE(deleteMany != nullptr);
    EXPECT_TRUE(deleteMany->enabled);
}

void ThemeMenuBuildsStableCommandRangesAndCustomCap() {
    Theme::ThemeVisual custom;
    custom.id = "custom.a";
    custom.name = { L"自定义 A", L"Custom A" };
    std::vector<Theme::ThemeVisual> customs(9, custom);
    for (int i = 0; i < 9; ++i) {
        customs[(size_t)i].id = "custom." + std::to_string(i);
        customs[(size_t)i].name = { L"自定义", L"Custom" };
    }

    GuiMenu::State state;
    state.lang = Lang::En;
    state.themeMode = "custom";
    state.currentThemeId = "custom.3";
    state.customThemes = &customs;

    const std::vector<GuiMenu::Item> menu = GuiMenu::BuildThemeMenu(state);
    EXPECT_TRUE(FindCommand(menu, GuiMenu::kCmdThemeFollowSystem) != nullptr);
    EXPECT_EQ(std::string(GuiMenu::BuiltInThemeIdForCommand(GuiMenu::kCmdThemeBuiltinBase)), std::string("paper"));
    EXPECT_EQ(std::string(GuiMenu::BuiltInThemeIdForCommand(GuiMenu::kCmdThemeBuiltinBase + 4)), std::string("sand"));
    EXPECT_TRUE(GuiMenu::BuiltInThemeIdForCommand(GuiMenu::kCmdThemeBuiltinBase + 5) == nullptr);

    int customCount = 0;
    bool currentCustomChecked = false;
    for (const GuiMenu::Item& item : menu) {
        if (item.cmd >= GuiMenu::kCmdThemeCustomBase &&
            item.cmd < GuiMenu::kCmdThemeCustomBase + 8) {
            ++customCount;
            if (item.cmd == GuiMenu::kCmdThemeCustomBase + 3)
                currentCustomChecked = item.checked;
        }
    }
    EXPECT_EQ(customCount, 8);
    EXPECT_TRUE(currentCustomChecked);
    EXPECT_TRUE(HasCommand(menu, GuiMenu::kCmdThemeManager));
}

void CalendarLayoutSnapsDragCreationAndParsesMinutePrecision() {
    const GuiCalendar::Frame frame = GuiCalendar::ComputeFrame(320.0f, 500.0f, 1.0f);
    EXPECT_EQ(GuiCalendar::SnapMinute(9 * 60 + 7), 9 * 60);
    EXPECT_EQ(GuiCalendar::SnapMinute(9 * 60 + 8), 9 * 60 + 15);
    EXPECT_FALSE(GuiCalendar::DragExceeded(20.0f, 20.0f, 24.0f, 24.0f, 1.0f));
    EXPECT_TRUE(GuiCalendar::DragExceeded(20.0f, 20.0f, 20.0f, 29.0f, 1.0f));

    const int start = GuiCalendar::MinuteFromPoint(frame.timelineViewport.top, 0.0f, frame);
    const int end = GuiCalendar::MinuteFromPoint(frame.timelineViewport.top + frame.hourHeight, 0.0f, frame);
    const GuiCalendar::TimeRange range = GuiCalendar::RangeFromDrag(start, end);
    EXPECT_EQ(range.startMinute, 0);
    EXPECT_EQ(range.endMinute, 60);

    int minute = -1;
    EXPECT_TRUE(GuiCalendar::ParseTimeText(L"9:07", minute));
    EXPECT_EQ(minute, 9 * 60 + 7);
    EXPECT_TRUE(GuiCalendar::ParseTimeText(L"24:00", minute));
    EXPECT_EQ(minute, 1440);
    EXPECT_FALSE(GuiCalendar::ParseTimeText(L"24:01", minute));
    EXPECT_EQ(GuiCalendar::FormatTimeText(9 * 60 + 7), std::wstring(L"09:07"));
}

void CalendarHitTestingUsesBlockRectsAndResizeHandles() {
    const GuiCalendar::Frame frame = GuiCalendar::ComputeFrame(320.0f, 500.0f, 1.0f);
    const Gui::Rect rect = GuiCalendar::ComputeBlockRect(frame, 3, 9 * 60, 10 * 60);
    const std::vector<GuiCalendar::BlockRect> blocks = {
        GuiCalendar::BlockRect{ 3, rect },
    };
    const float x = Mid(rect.left, rect.right);
    const float scroll = rect.top - 20.0f;
    const float clientTop = frame.timelineViewport.top + rect.top - scroll;
    const float clientMid = frame.timelineViewport.top + Mid(rect.top, rect.bottom) - scroll;
    const float clientBottom = frame.timelineViewport.top + rect.bottom - 1.0f - scroll;

    GuiCalendar::HitResult hit = GuiCalendar::HitTest(x, clientTop + 1.0f, scroll, 1.0f, frame, blocks);
    EXPECT_EQ(hit.kind, GuiCalendar::HitKind::ResizeStart);
    EXPECT_EQ(hit.blockId, 3);

    hit = GuiCalendar::HitTest(x, clientMid, scroll, 1.0f, frame, blocks);
    EXPECT_EQ(hit.kind, GuiCalendar::HitKind::BlockBody);

    hit = GuiCalendar::HitTest(x, clientBottom, scroll, 1.0f, frame, blocks);
    EXPECT_EQ(hit.kind, GuiCalendar::HitKind::ResizeEnd);

    hit = GuiCalendar::HitTest(Mid(frame.lane.left, frame.lane.right),
                               frame.timelineViewport.top + 4.0f,
                               0.0f, 1.0f, frame, blocks);
    EXPECT_EQ(hit.kind, GuiCalendar::HitKind::EmptyTimeline);
}

void CalendarHeaderButtonsLayout() {
    const GuiCalendar::Frame frame = GuiCalendar::ComputeFrame(320.0f, 500.0f, 1.0f);
    const std::vector<GuiCalendar::BlockRect> none;

    EXPECT_EQ(GuiCalendar::HitTest(Mid(frame.prevDay.left, frame.prevDay.right),
                                   Mid(frame.prevDay.top, frame.prevDay.bottom),
                                   0.0f, 1.0f, frame, none).kind,
              GuiCalendar::HitKind::PrevDay);
    EXPECT_EQ(GuiCalendar::HitTest(Mid(frame.nextDay.left, frame.nextDay.right),
                                   Mid(frame.nextDay.top, frame.nextDay.bottom),
                                   0.0f, 1.0f, frame, none).kind,
              GuiCalendar::HitKind::NextDay);
    EXPECT_EQ(GuiCalendar::HitTest(Mid(frame.today.left, frame.today.right),
                                   Mid(frame.today.top, frame.today.bottom),
                                   0.0f, 1.0f, frame, none).kind,
              GuiCalendar::HitKind::Today);

    // Today button sits between prev and next without overlap.
    EXPECT_TRUE(frame.today.left > frame.prevDay.right);
    EXPECT_TRUE(frame.today.right <= frame.nextDay.left);
    // Timeline starts right below the date header (no all-day band).
    EXPECT_TRUE(frame.timelineViewport.top < frame.timelineViewport.bottom);
}

void CalendarEditLayoutSeparatesFieldsAndChildEdits() {
    const GuiCalendar::Frame frame = GuiCalendar::ComputeFrame(320.0f, 500.0f, 1.0f);
    const Gui::Rect block = GuiCalendar::ComputeBlockRect(frame, 3, 9 * 60, 9 * 60 + 15);
    const GuiCalendar::EditLayout layout = GuiCalendar::ComputeEditLayout(block, 1.0f);

    EXPECT_TRUE(layout.block.bottom >= block.top + 68.0f);
    EXPECT_TRUE(layout.titleFrame.left > layout.block.left);
    EXPECT_TRUE(layout.titleFrame.right < layout.block.right);
    EXPECT_TRUE(layout.titleEdit.left > layout.titleFrame.left);
    EXPECT_TRUE(layout.titleEdit.right < layout.titleFrame.right);
    EXPECT_TRUE(layout.titleEdit.top > layout.titleFrame.top);
    EXPECT_TRUE(layout.titleEdit.bottom < layout.titleFrame.bottom);

    EXPECT_TRUE(layout.startFrame.left == layout.titleFrame.left);
    EXPECT_TRUE(layout.startFrame.right < layout.endFrame.left);
    EXPECT_TRUE(layout.endFrame.right <= layout.titleFrame.right);
    EXPECT_TRUE(layout.startEdit.left > layout.startFrame.left);
    EXPECT_TRUE(layout.endEdit.right < layout.endFrame.right);

    EXPECT_EQ(GuiCalendar::HitTestEditField(Mid(layout.titleFrame.left, layout.titleFrame.right),
                                            Mid(layout.titleFrame.top, layout.titleFrame.bottom),
                                            layout),
              GuiCalendar::EditField::Title);
    EXPECT_EQ(GuiCalendar::HitTestEditField(Mid(layout.startFrame.left, layout.startFrame.right),
                                            Mid(layout.startFrame.top, layout.startFrame.bottom),
                                            layout),
              GuiCalendar::EditField::StartTime);
    EXPECT_EQ(GuiCalendar::HitTestEditField(Mid(layout.endFrame.left, layout.endFrame.right),
                                            Mid(layout.endFrame.top, layout.endFrame.bottom),
                                            layout),
              GuiCalendar::EditField::EndTime);
    EXPECT_EQ(GuiCalendar::HitTestEditField(Mid(layout.startFrame.left, layout.startFrame.right),
                                            layout.endFrame.bottom + 3.0f,
                                            layout),
              GuiCalendar::EditField::StartTime);
    EXPECT_EQ(GuiCalendar::HitTestEditField(Mid(layout.endFrame.left, layout.endFrame.right),
                                            layout.endFrame.bottom + 3.0f,
                                            layout),
              GuiCalendar::EditField::EndTime);
    EXPECT_EQ(GuiCalendar::HitTestEditField(layout.block.left - 1.0f,
                                            Mid(layout.block.top, layout.block.bottom),
                                            layout),
              GuiCalendar::EditField::None);

    Gui::Rect fieldRect;
    EXPECT_TRUE(GuiCalendar::EditFieldFrame(layout, GuiCalendar::EditField::StartTime, fieldRect));
    EXPECT_EQ(fieldRect.left, layout.startFrame.left);
    EXPECT_TRUE(GuiCalendar::EditFieldControlRect(layout, GuiCalendar::EditField::EndTime, fieldRect));
    EXPECT_EQ(fieldRect.left, layout.endEdit.left);
}

void CalendarChildEditorsLeaveRoundedFrameSafeArea() {
    const GuiCalendar::Frame frame = GuiCalendar::ComputeFrame(320.0f, 500.0f, 1.0f);
    const Gui::Rect block = GuiCalendar::ComputeBlockRect(frame, 3, 9 * 60, 9 * 60 + 15);
    const GuiCalendar::EditLayout layout = GuiCalendar::ComputeEditLayout(block, 1.0f);
    const float minChildInset = 4.0f;

    ExpectChildControlLeavesRoundedFrameSafeArea(layout.titleFrame, layout.titleEdit, minChildInset);
    ExpectChildControlLeavesRoundedFrameSafeArea(layout.startFrame, layout.startEdit, minChildInset);
    ExpectChildControlLeavesRoundedFrameSafeArea(layout.endFrame, layout.endEdit, minChildInset);

    EXPECT_EQ(GuiCalendar::HitTestEditField(layout.titleFrame.left + 1.0f,
                                            Mid(layout.titleFrame.top, layout.titleFrame.bottom),
                                            layout),
              GuiCalendar::EditField::Title);
    EXPECT_EQ(GuiCalendar::HitTestEditField(layout.startFrame.left + 1.0f,
                                            Mid(layout.startFrame.top, layout.startFrame.bottom),
                                            layout),
              GuiCalendar::EditField::StartTime);
    EXPECT_EQ(GuiCalendar::HitTestEditField(layout.endFrame.left + 1.0f,
                                            Mid(layout.endFrame.top, layout.endFrame.bottom),
                                            layout),
              GuiCalendar::EditField::EndTime);
}

void CalendarTimeRangeParsingIsAtomicAndStrict() {
    GuiCalendar::TimeRange range;
    EXPECT_TRUE(GuiCalendar::ParseTimeRangeText(L"09:30", L"10:00", range));
    EXPECT_EQ(range.startMinute, 9 * 60 + 30);
    EXPECT_EQ(range.endMinute, 10 * 60);

    EXPECT_TRUE(GuiCalendar::ParseTimeRangeText(L"23:59", L"24:00", range));
    EXPECT_EQ(range.startMinute, 23 * 60 + 59);
    EXPECT_EQ(range.endMinute, 24 * 60);

    EXPECT_FALSE(GuiCalendar::ParseTimeRangeText(L"10:00", L"09:30", range));
    EXPECT_FALSE(GuiCalendar::ParseTimeRangeText(L"09:30", L"09:30", range));
    EXPECT_FALSE(GuiCalendar::ParseTimeRangeText(L"bad", L"10:00", range));
    EXPECT_FALSE(GuiCalendar::ParseTimeRangeText(L"09:30", L"24:01", range));
}

const TestCase kTests[] = {
    {"NonClientHitTestPrioritizesTitleButtonsOverResizeBand", NonClientHitTestPrioritizesTitleButtonsOverResizeBand},
    {"NonClientHitTestMapsEdgesCornersCaptionAndCapsule", NonClientHitTestMapsEdgesCornersCaptionAndCapsule},
    {"NonClientHitTestScalesResizeEdgeAndCanForceClient", NonClientHitTestScalesResizeEdgeAndCanForceClient},
    {"GeometryPolicyScalesMinimumAndRejectsSubminimumCaptures", GeometryPolicyScalesMinimumAndRejectsSubminimumCaptures},
    {"GeometryCapturePolicyMatchesWindowModes", GeometryCapturePolicyMatchesWindowModes},
    {"ExpandedGeometryUsesStoredSizeAndClampsToWorkArea", ExpandedGeometryUsesStoredSizeAndClampsToWorkArea},
    {"TitleButtonLayoutOrdersActionsAndStaysInsideWindow", TitleButtonLayoutOrdersActionsAndStaysInsideWindow},
    {"ChromeHitTestCoversTitleButtonsTabsAndAddList", ChromeHitTestCoversTitleButtonsTabsAndAddList},
    {"TabStripNeverOverlapsAddList", TabStripNeverOverlapsAddList},
    {"EmptyActivePromptFillsViewportForCentering", EmptyActivePromptFillsViewportForCentering},
    {"RowLayoutKeepsIndentControlsAndHitTestingInLockstep", RowLayoutKeepsIndentControlsAndHitTestingInLockstep},
    {"EditIntentMapsKeyboardWithoutLeakingControlCharacters", EditIntentMapsKeyboardWithoutLeakingControlCharacters},
    {"TitleAndTrayMenusDoNotExposeListManagementCommands", TitleAndTrayMenusDoNotExposeListManagementCommands},
    {"ListTabMenuOwnsRenameAndDeletePolicy", ListTabMenuOwnsRenameAndDeletePolicy},
    {"ThemeMenuBuildsStableCommandRangesAndCustomCap", ThemeMenuBuildsStableCommandRangesAndCustomCap},
    {"CalendarLayoutSnapsDragCreationAndParsesMinutePrecision", CalendarLayoutSnapsDragCreationAndParsesMinutePrecision},
    {"CalendarHitTestingUsesBlockRectsAndResizeHandles", CalendarHitTestingUsesBlockRectsAndResizeHandles},
    {"CalendarHeaderButtonsLayout", CalendarHeaderButtonsLayout},
    {"CalendarEditLayoutSeparatesFieldsAndChildEdits", CalendarEditLayoutSeparatesFieldsAndChildEdits},
    {"CalendarChildEditorsLeaveRoundedFrameSafeArea", CalendarChildEditorsLeaveRoundedFrameSafeArea},
    {"CalendarTimeRangeParsingIsAtomicAndStrict", CalendarTimeRangeParsingIsAtomicAndStrict},
};

} // namespace

int main() {
    return RunTests("gui_contract", kTests);
}
