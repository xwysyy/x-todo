#pragma once

#include "GuiTypes.h"

#include <array>
#include <string>
#include <vector>

namespace GuiCalendar {

// ----------------------------------------------------------------------------
// Shared calendar header (Day / Week / Month).
//
// Layout, left to right: a grouped prev/next nav pair, a flexible period title,
// a connected Day|Week|Month segmented control, and an optional "back to today"
// icon pinned to the right. The layout degrades on narrow widths by priority
// (nav > segmented > today > title): the title shortens then hides, the today
// icon hides, and the segmented control shrinks, while nav and segmented never
// overlap and no rect goes negative-width.
// ----------------------------------------------------------------------------

struct HeaderLayout {
    Gui::Rect prev;       // ‹ previous period
    Gui::Rect next;       // › next period
    Gui::Rect title;      // period label (only when titleVisible)
    Gui::Rect segment;    // connected segmented container
    Gui::Rect modeDay;    // three equal segments inside `segment`
    Gui::Rect modeWeek;
    Gui::Rect modeMonth;
    Gui::Rect today;      // back-to-today icon (only when todayVisible)
    float headerHeight = 0.0f;
    bool titleVisible = false;
    bool todayVisible = false;
    bool compactTitle = false; // title area is tight; caller should use a short label
};

enum class HeaderHit { None, Prev, Next, Today, ModeDay, ModeWeek, ModeMonth };

// `showToday` requests the today icon (caller passes false when already on today).
HeaderLayout ComputeHeader(float windowWidth, float dpiScale, bool showToday);
HeaderHit HitTestHeader(float x, float y, const HeaderLayout& header);

// ----------------------------------------------------------------------------
// Day view.
// ----------------------------------------------------------------------------

struct Frame {
    HeaderLayout header;
    Gui::Rect timelineViewport;
    Gui::Rect gutter;
    Gui::Rect lane;
    float hourHeight = 0.0f;
    float contentHeight = 0.0f;
};

struct BlockRect {
    int blockId = -1;
    Gui::Rect rect;
};

// Day body hits only; header hits come from HitTestHeader.
enum class HitKind {
    None,
    EmptyTimeline,
    BlockBody,
    ResizeStart,
    ResizeEnd,
};

struct HitResult {
    HitKind kind = HitKind::None;
    int blockId = -1;
};

struct TimeRange {
    int startMinute = 0;
    int endMinute = 15;
};

enum class EditField {
    None,
    Title,
    StartTime,
    EndTime,
};

struct EditLayout {
    Gui::Rect block;
    Gui::Rect titleFrame;
    Gui::Rect titleEdit;
    Gui::Rect startFrame;
    Gui::Rect startEdit;
    Gui::Rect endFrame;
    Gui::Rect endEdit;
};

Frame ComputeFrame(float windowWidth, float viewportHeight, float dpiScale, bool showToday = true);
Gui::Rect ComputeBlockRect(const Frame& frame, int blockId, int startMinute, int endMinute);
HitResult HitTest(float x, float y, float scroll, float dpiScale, const Frame& frame,
                  const std::vector<BlockRect>& blocks);
EditLayout ComputeEditLayout(const Gui::Rect& blockRect, float dpiScale);
EditField HitTestEditField(float x, float y, const EditLayout& layout);
bool EditFieldFrame(const EditLayout& layout, EditField field, Gui::Rect& frame);
bool EditFieldControlRect(const EditLayout& layout, EditField field, Gui::Rect& rect);

int MinuteFromPoint(float y, float scroll, const Frame& frame);
int SnapMinute(int minute, int stepMinutes = 15);
bool DragExceeded(float startX, float startY, float x, float y, float dpiScale);
TimeRange RangeFromDrag(int anchorMinute, int currentMinute, int minDuration = 15);
float ScrollForMinute(int minute, float viewportHeight, const Frame& frame);

bool ParseTimeText(const std::wstring& text, int& minute);
bool ParseTimeRangeText(const std::wstring& startText, const std::wstring& endText,
                        TimeRange& range);
std::wstring FormatTimeText(int minute);

// ----------------------------------------------------------------------------
// Week view: seven day columns over one shared vertical time axis.
// ----------------------------------------------------------------------------

struct WeekFrame {
    HeaderLayout header;
    Gui::Rect dayHeaderRow;
    Gui::Rect timelineViewport;
    Gui::Rect gutter;
    std::array<Gui::Rect, 7> dayHeaders{};
    std::array<Gui::Rect, 7> columns{};
    float hourHeight = 0.0f;
    float contentHeight = 0.0f;
};

// Week body hits only; header hits come from HitTestHeader.
enum class WeekHitKind {
    None,
    DayHeader,
    Block,
    EmptyColumn,
};

struct WeekHitResult {
    WeekHitKind kind = WeekHitKind::None;
    int dayIndex = -1;
    int blockId = -1;
};

// Lane assignment for one day's blocks. laneCount is the column subdivision for
// the block's overlap cluster.
struct LaneSpan {
    int lane = 0;
    int laneCount = 1;
};

struct WeekBlockRect {
    int blockId = -1;
    int dayIndex = -1;
    Gui::Rect rect;
};

WeekFrame ComputeWeekFrame(float windowWidth, float viewportHeight, float dpiScale,
                           bool showToday = true);

// Packs blocks already sorted by start, then end, then id, into deterministic
// lanes. Returns one LaneSpan per input span, in the same order.
std::vector<LaneSpan> PackDayLanes(const std::vector<TimeRange>& sortedSpans);

Gui::Rect ComputeWeekBlockRect(const WeekFrame& frame, int dayIndex, const LaneSpan& lane,
                               int startMinute, int endMinute);
WeekHitResult HitTestWeek(float x, float y, float scroll, float dpiScale,
                          const WeekFrame& frame, const std::vector<WeekBlockRect>& blocks);

// ----------------------------------------------------------------------------
// Month view: a fixed 42-cell grid (six weeks of seven days).
// ----------------------------------------------------------------------------

struct MonthFrame {
    HeaderLayout header;
    Gui::Rect weekdayRow;
    std::array<Gui::Rect, 7> weekdayHeaders{};
    Gui::Rect grid;
    std::array<Gui::Rect, 42> cells{};
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
};

// Month body hits only; header hits come from HitTestHeader.
enum class MonthHitKind {
    None,
    Cell,
};

struct MonthHitResult {
    MonthHitKind kind = MonthHitKind::None;
    int cellIndex = -1;
};

MonthFrame ComputeMonthFrame(float windowWidth, float viewportHeight, float dpiScale,
                             bool showToday = true);
MonthHitResult HitTestMonth(float x, float y, float dpiScale, const MonthFrame& frame);

} // namespace GuiCalendar
