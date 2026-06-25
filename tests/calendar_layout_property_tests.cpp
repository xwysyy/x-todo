#include "CalendarLayout.h"
#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace xtodo_test;
using namespace GuiCalendar;

namespace {

float MidX(const Gui::Rect& r) { return (r.left + r.right) * 0.5f; }
float MidY(const Gui::Rect& r) { return (r.top + r.bottom) * 0.5f; }

void ExpectFiniteRect(const Gui::Rect& r) {
  EXPECT_TRUE(std::isfinite(r.left));
  EXPECT_TRUE(std::isfinite(r.top));
  EXPECT_TRUE(std::isfinite(r.right));
  EXPECT_TRUE(std::isfinite(r.bottom));
  EXPECT_TRUE(r.right >= r.left);
  EXPECT_TRUE(r.bottom >= r.top);
}

void ExpectPositiveRect(const Gui::Rect& r) {
  ExpectFiniteRect(r);
  EXPECT_TRUE(r.Width() > 0.0f);
  EXPECT_TRUE(r.Height() > 0.0f);
}

void HeaderLayoutKeepsControlsNonOverlappingAndHittableAcrossWidths() {
  const float widths[] = {96.0f, 120.0f, 160.0f, 220.0f, 320.0f, 520.0f, 900.0f};
  const float dpis[] = {1.0f, 1.25f, 1.5f, 2.0f};
  for (float width : widths) {
    for (float dpi : dpis) {
      for (bool showToday : {false, true}) {
        const HeaderLayout h = ComputeHeader(width, dpi, showToday);
        ExpectPositiveRect(h.prev);
        ExpectPositiveRect(h.next);
        ExpectPositiveRect(h.segment);
        ExpectPositiveRect(h.modeDay);
        ExpectPositiveRect(h.modeWeek);
        ExpectPositiveRect(h.modeMonth);
        EXPECT_TRUE(h.headerHeight > 0.0f);

        EXPECT_TRUE(h.prev.right <= h.next.left + 0.001f);
        EXPECT_TRUE(h.next.right <= h.segment.left + 0.001f);
        EXPECT_NEAR(h.modeDay.left, h.segment.left, 0.001);
        EXPECT_NEAR(h.modeMonth.right, h.segment.right, 0.001);
        EXPECT_TRUE(h.modeDay.right <= h.modeWeek.left + 0.001f);
        EXPECT_TRUE(h.modeWeek.right <= h.modeMonth.left + 0.001f);

        EXPECT_TRUE(HitTestHeader(MidX(h.prev), MidY(h.prev), h) == HeaderHit::Prev);
        EXPECT_TRUE(HitTestHeader(MidX(h.next), MidY(h.next), h) == HeaderHit::Next);
        EXPECT_TRUE(HitTestHeader(MidX(h.modeDay), MidY(h.modeDay), h) == HeaderHit::ModeDay);
        EXPECT_TRUE(HitTestHeader(MidX(h.modeWeek), MidY(h.modeWeek), h) == HeaderHit::ModeWeek);
        EXPECT_TRUE(HitTestHeader(MidX(h.modeMonth), MidY(h.modeMonth), h) == HeaderHit::ModeMonth);
        EXPECT_TRUE(HitTestHeader(-10.0f, -10.0f, h) == HeaderHit::None);

        if (h.titleVisible) {
          ExpectPositiveRect(h.title);
          EXPECT_TRUE(h.title.left >= h.next.right - 0.001f);
          EXPECT_TRUE(h.title.right <= h.segment.left + 0.001f);
        }
        if (h.todayVisible) {
          ExpectPositiveRect(h.today);
          EXPECT_TRUE(showToday);
          EXPECT_TRUE(h.segment.right <= h.today.left + 0.001f);
          EXPECT_TRUE(HitTestHeader(MidX(h.today), MidY(h.today), h) == HeaderHit::Today);
        }
      }
    }
  }
}

void DayFrameBlockHitTestingAndMinuteMathStayConsistent() {
  const Frame frame = ComputeFrame(640.0f, 540.0f, 1.0f, true);
  ExpectPositiveRect(frame.timelineViewport);
  ExpectPositiveRect(frame.gutter);
  ExpectPositiveRect(frame.lane);
  EXPECT_TRUE(frame.timelineViewport.top >= frame.header.headerHeight);
  EXPECT_NEAR(frame.contentHeight, frame.hourHeight * 24.0f, 0.001);

  for (int minute = 0; minute <= 1440; minute += 15) {
    const float y = frame.timelineViewport.top + (static_cast<float>(minute) / 60.0f) * frame.hourHeight;
    EXPECT_EQ(MinuteFromPoint(y, 0.0f, frame), minute);
    const int snapped = SnapMinute(minute + 7, 15);
    EXPECT_TRUE(snapped >= 0);
    EXPECT_TRUE(snapped <= 1440);
    EXPECT_EQ(snapped % 15, 0);
  }

  const Gui::Rect rect = ComputeBlockRect(frame, 42, 9 * 60, 10 * 60);
  ExpectPositiveRect(rect);
  const float targetScroll = ScrollForMinute(9 * 60, frame.timelineViewport.Height(), frame);
  EXPECT_TRUE(targetScroll >= 0.0f);
  EXPECT_TRUE(targetScroll <= frame.contentHeight - frame.timelineViewport.Height());
  EXPECT_TRUE(rect.top - targetScroll >= 0.0f);
  EXPECT_TRUE(rect.top - targetScroll <= frame.timelineViewport.Height());
  const float scroll = rect.top;
  const std::vector<BlockRect> blocks = {BlockRect{42, rect}};
  EXPECT_TRUE(HitTest(MidX(rect), frame.timelineViewport.top + 2.0f, scroll, 1.0f, frame, blocks).kind == HitKind::ResizeStart);
  EXPECT_TRUE(HitTest(MidX(rect), frame.timelineViewport.top + 18.0f, scroll, 1.0f, frame, blocks).kind == HitKind::BlockBody);
  EXPECT_TRUE(HitTest(MidX(rect), frame.timelineViewport.top + rect.Height() - 2.0f, scroll, 1.0f, frame, blocks).kind == HitKind::ResizeEnd);
  EXPECT_TRUE(HitTest(MidX(frame.gutter), MidY(frame.timelineViewport), 0.0f, 1.0f, frame, blocks).kind == HitKind::None);
  EXPECT_TRUE(HitTest(MidX(frame.lane), MidY(frame.timelineViewport), 0.0f, 1.0f, frame, {}).kind == HitKind::EmptyTimeline);

  const EditLayout edit = ComputeEditLayout(rect, 1.0f);
  Gui::Rect field;
  EXPECT_TRUE(EditFieldFrame(edit, EditField::Title, field));
  EXPECT_TRUE(HitTestEditField(MidX(field), MidY(field), edit) == EditField::Title);
  EXPECT_TRUE(EditFieldFrame(edit, EditField::StartTime, field));
  EXPECT_TRUE(HitTestEditField(MidX(field), MidY(field), edit) == EditField::StartTime);
  EXPECT_TRUE(EditFieldFrame(edit, EditField::EndTime, field));
  EXPECT_TRUE(HitTestEditField(MidX(field), MidY(field), edit) == EditField::EndTime);
  EXPECT_FALSE(EditFieldFrame(edit, EditField::None, field));
}

void TimeFormattingParsingSnappingAndDragRangesAreClosedUnderBoundaries() {
  for (int minute = -60; minute <= 1500; ++minute) {
    const int snapped = SnapMinute(minute, 15);
    EXPECT_TRUE(snapped >= 0);
    EXPECT_TRUE(snapped <= 1440);
    EXPECT_EQ(snapped % 15, 0);
  }

  for (int minute = 0; minute <= 1440; ++minute) {
    const std::wstring text = FormatTimeText(minute);
    int parsed = -1;
    EXPECT_TRUE(ParseTimeText(text, parsed));
    EXPECT_EQ(parsed, minute);
  }

  int parsed = 123;
  EXPECT_TRUE(ParseTimeText(L" 9:07\t", parsed));
  EXPECT_EQ(parsed, 9 * 60 + 7);
  const int before = parsed;
  const std::wstring invalidTimes[] = {L"", L"9", L"9:7", L"24:01", L"25:00", L"12:60", L"ab:cd"};
  for (const std::wstring& text : invalidTimes) {
    EXPECT_FALSE(ParseTimeText(text, parsed));
    EXPECT_EQ(parsed, before);
  }

  TimeRange range;
  EXPECT_TRUE(ParseTimeRangeText(L"08:00", L"08:15", range));
  EXPECT_EQ(range.startMinute, 8 * 60);
  EXPECT_EQ(range.endMinute, 8 * 60 + 15);
  EXPECT_FALSE(ParseTimeRangeText(L"08:00", L"08:00", range));
  EXPECT_FALSE(ParseTimeRangeText(L"08:00", L"07:59", range));

  for (int anchor : {-100, 0, 7, 720, 1435, 2000}) {
    for (int current : {-100, 0, 7, 720, 1435, 2000}) {
      const TimeRange dragged = RangeFromDrag(anchor, current, 15);
      EXPECT_TRUE(dragged.startMinute >= 0);
      EXPECT_TRUE(dragged.endMinute <= 1440);
      EXPECT_TRUE(dragged.endMinute > dragged.startMinute);
      EXPECT_TRUE(dragged.endMinute - dragged.startMinute >= 15);
      EXPECT_EQ(dragged.startMinute % 15, 0);
      EXPECT_EQ(dragged.endMinute % 15, 0);
    }
  }
}

void WeekLanePackingAndHitTestingPreserveOverlapSemantics() {
  const std::vector<TimeRange> spans = {
      TimeRange{9 * 60, 10 * 60},
      TimeRange{9 * 60 + 15, 9 * 60 + 45},
      TimeRange{9 * 60 + 30, 10 * 60 + 30},
      TimeRange{10 * 60 + 30, 11 * 60 + 30},
      TimeRange{10 * 60 + 40, 10 * 60 + 50},
      TimeRange{11 * 60 + 30, 12 * 60},
  };
  const std::vector<LaneSpan> lanes = PackDayLanes(spans);
  EXPECT_EQ(lanes.size(), spans.size());
  for (size_t i = 0; i < spans.size(); ++i) {
    EXPECT_TRUE(lanes[i].lane >= 0);
    EXPECT_TRUE(lanes[i].laneCount >= 1);
    EXPECT_TRUE(lanes[i].lane < lanes[i].laneCount);
    for (size_t j = i + 1; j < spans.size(); ++j) {
      const bool overlap = spans[i].startMinute < spans[j].endMinute && spans[j].startMinute < spans[i].endMinute;
      if (overlap) EXPECT_TRUE(lanes[i].lane != lanes[j].lane);
    }
  }

  const WeekFrame frame = ComputeWeekFrame(720.0f, 620.0f, 1.0f, true);
  ExpectPositiveRect(frame.timelineViewport);
  ExpectPositiveRect(frame.gutter);
  for (int day = 0; day < 7; ++day) {
    ExpectPositiveRect(frame.dayHeaders[static_cast<size_t>(day)]);
    ExpectPositiveRect(frame.columns[static_cast<size_t>(day)]);
    const WeekHitResult headerHit = HitTestWeek(MidX(frame.dayHeaders[static_cast<size_t>(day)]), MidY(frame.dayHeaders[static_cast<size_t>(day)]), 0.0f, 1.0f, frame, {});
    EXPECT_TRUE(headerHit.kind == WeekHitKind::DayHeader);
    EXPECT_EQ(headerHit.dayIndex, day);

    const Gui::Rect rect = ComputeWeekBlockRect(frame, day, lanes[0], spans[0].startMinute, spans[0].endMinute);
    ExpectPositiveRect(rect);
    const float scroll = rect.top;
    const std::vector<WeekBlockRect> blocks = {WeekBlockRect{777, day, rect}};
    const WeekHitResult blockHit = HitTestWeek(MidX(rect), frame.timelineViewport.top + 10.0f, scroll, 1.0f, frame, blocks);
    EXPECT_TRUE(blockHit.kind == WeekHitKind::Block);
    EXPECT_EQ(blockHit.blockId, 777);
    EXPECT_EQ(blockHit.dayIndex, day);
  }
}

void MonthGridHasFortyTwoContiguousHittableCells() {
  const MonthFrame frame = ComputeMonthFrame(700.0f, 640.0f, 1.0f, true);
  ExpectPositiveRect(frame.weekdayRow);
  ExpectPositiveRect(frame.grid);
  EXPECT_NEAR(frame.cellWidth * 7.0f, frame.grid.Width(), 0.01);
  EXPECT_NEAR(frame.cellHeight * 6.0f, frame.grid.Height(), 0.01);

  for (int i = 0; i < 7; ++i) {
    ExpectPositiveRect(frame.weekdayHeaders[static_cast<size_t>(i)]);
  }
  for (int i = 0; i < 42; ++i) {
    const Gui::Rect& cell = frame.cells[static_cast<size_t>(i)];
    ExpectPositiveRect(cell);
    const MonthHitResult hit = HitTestMonth(MidX(cell), MidY(cell), 1.0f, frame);
    EXPECT_TRUE(hit.kind == MonthHitKind::Cell);
    EXPECT_EQ(hit.cellIndex, i);
  }

  EXPECT_TRUE(HitTestMonth(frame.grid.left - 1.0f, MidY(frame.grid), 1.0f, frame).kind == MonthHitKind::None);
  EXPECT_TRUE(HitTestMonth(MidX(frame.grid), frame.grid.bottom + 1.0f, 1.0f, frame).kind == MonthHitKind::None);
}

const TestCase kTests[] = {
    {"HeaderLayoutKeepsControlsNonOverlappingAndHittableAcrossWidths", HeaderLayoutKeepsControlsNonOverlappingAndHittableAcrossWidths},
    {"DayFrameBlockHitTestingAndMinuteMathStayConsistent", DayFrameBlockHitTestingAndMinuteMathStayConsistent},
    {"TimeFormattingParsingSnappingAndDragRangesAreClosedUnderBoundaries", TimeFormattingParsingSnappingAndDragRangesAreClosedUnderBoundaries},
    {"WeekLanePackingAndHitTestingPreserveOverlapSemantics", WeekLanePackingAndHitTestingPreserveOverlapSemantics},
    {"MonthGridHasFortyTwoContiguousHittableCells", MonthGridHasFortyTwoContiguousHittableCells},
};

} // namespace

int main() { return RunTests("calendar_layout_property", kTests); }
