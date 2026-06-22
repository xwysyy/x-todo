#include "CalendarLayout.h"
#include "test_framework.h"

#include <cmath>
#include <vector>

using namespace xtodo_test;

namespace {

float Mid(float a, float b) { return (a + b) * 0.5f; }

GuiCalendar::TimeRange TR(int start, int end) { return GuiCalendar::TimeRange{ start, end }; }

void HeaderGroupsNavConnectsSegmentsAndPinsToday() {
    const GuiCalendar::HeaderLayout h = GuiCalendar::ComputeHeader(700.0f, 1.0f, true);

    // Nav is a left-aligned pair.
    EXPECT_TRUE(h.prev.left < h.next.left);
    EXPECT_TRUE(h.next.left >= h.prev.right - 0.5f);
    EXPECT_TRUE(h.next.left <= h.prev.right + 6.0f);

    // Segmented sits right of the nav group, with three equal contiguous parts.
    EXPECT_TRUE(h.segment.left >= h.next.right);
    EXPECT_TRUE(h.segment.right > h.segment.left);
    EXPECT_NEAR(h.modeDay.left, h.segment.left, 0.01);
    EXPECT_NEAR(h.modeMonth.right, h.segment.right, 0.01);
    EXPECT_NEAR(h.modeDay.right, h.modeWeek.left, 0.01);
    EXPECT_NEAR(h.modeWeek.right, h.modeMonth.left, 0.01);
    EXPECT_TRUE(std::fabs(h.modeDay.Width() - h.modeWeek.Width()) < 0.5f);
    EXPECT_TRUE(std::fabs(h.modeWeek.Width() - h.modeMonth.Width()) < 0.5f);

    // Today icon is pinned to the right edge.
    EXPECT_TRUE(h.todayVisible);
    EXPECT_TRUE(h.today.left > h.segment.right);

    // Hit-testing routes each region.
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.prev.left, h.prev.right), Mid(h.prev.top, h.prev.bottom), h) ==
                GuiCalendar::HeaderHit::Prev);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.next.left, h.next.right), Mid(h.next.top, h.next.bottom), h) ==
                GuiCalendar::HeaderHit::Next);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.modeDay.left, h.modeDay.right), Mid(h.modeDay.top, h.modeDay.bottom), h) ==
                GuiCalendar::HeaderHit::ModeDay);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.modeWeek.left, h.modeWeek.right), Mid(h.modeWeek.top, h.modeWeek.bottom), h) ==
                GuiCalendar::HeaderHit::ModeWeek);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.modeMonth.left, h.modeMonth.right), Mid(h.modeMonth.top, h.modeMonth.bottom), h) ==
                GuiCalendar::HeaderHit::ModeMonth);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(Mid(h.today.left, h.today.right), Mid(h.today.top, h.today.bottom), h) ==
                GuiCalendar::HeaderHit::Today);
}

void HeaderHidesTodayWhenNotRequested() {
    const GuiCalendar::HeaderLayout h = GuiCalendar::ComputeHeader(700.0f, 1.0f, false);
    EXPECT_FALSE(h.todayVisible);
    EXPECT_TRUE(GuiCalendar::HitTestHeader(700.0f - 20.0f, 20.0f, h) != GuiCalendar::HeaderHit::Today);
}

void HeaderDegradesOnNarrowWidthsWithoutOverlap() {
    const float widths[] = { 400.0f, 280.0f, 200.0f, 160.0f, 120.0f };
    for (float w : widths) {
        const GuiCalendar::HeaderLayout h = GuiCalendar::ComputeHeader(w, 1.0f, true);
        // Segmented never overlaps the nav group and is always positive width.
        EXPECT_TRUE(h.segment.left >= h.next.right);
        EXPECT_TRUE(h.segment.right > h.segment.left);
        // Segments stay contiguous and equal at every width.
        EXPECT_NEAR(h.modeDay.right, h.modeWeek.left, 0.01);
        EXPECT_NEAR(h.modeWeek.right, h.modeMonth.left, 0.01);
        EXPECT_NEAR(h.modeMonth.right, h.segment.right, 0.01);
        // When the title is visible it must not run into the segmented control.
        if (h.titleVisible) {
            EXPECT_TRUE(h.title.right <= h.segment.left + 0.5f);
            EXPECT_TRUE(h.title.right > h.title.left);
        }
        // When today is visible it stays right of the segmented control.
        if (h.todayVisible) EXPECT_TRUE(h.today.left >= h.segment.right);
    }

    // Tightest realistic widths drop the title (and then today) before crowding.
    const GuiCalendar::HeaderLayout narrow = GuiCalendar::ComputeHeader(160.0f, 1.0f, true);
    EXPECT_FALSE(narrow.titleVisible);
    EXPECT_FALSE(narrow.todayVisible);
}

void WeekFrameHasSevenEqualContiguousColumns() {
    const GuiCalendar::WeekFrame f = GuiCalendar::ComputeWeekFrame(700.0f, 600.0f, 1.0f);
    EXPECT_TRUE(f.hourHeight > 0.0f);
    EXPECT_NEAR(f.contentHeight, 24.0f * f.hourHeight, 0.01);
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(f.columns[(size_t)i].right > f.columns[(size_t)i].left);
        if (i > 0) {
            EXPECT_TRUE(std::fabs(f.columns[(size_t)i].left - f.columns[(size_t)(i - 1)].right) < 0.5f);
        }
        EXPECT_TRUE(std::fabs(f.columns[(size_t)i].Width() - f.columns[0].Width()) < 1.0f);
        EXPECT_TRUE(std::fabs(f.dayHeaders[(size_t)i].left - f.columns[(size_t)i].left) < 0.5f);
        EXPECT_TRUE(std::fabs(f.dayHeaders[(size_t)i].right - f.columns[(size_t)i].right) < 0.5f);
    }
    EXPECT_TRUE(f.columns[0].left >= f.gutter.right - 0.5f);
}

void LanePackingIsDeterministic() {
    using GuiCalendar::PackDayLanes;

    const std::vector<GuiCalendar::LaneSpan> a = PackDayLanes({ TR(540, 600), TR(600, 660) });
    EXPECT_EQ((int)a.size(), 2);
    EXPECT_EQ(a[0].lane, 0);
    EXPECT_EQ(a[0].laneCount, 1);
    EXPECT_EQ(a[1].lane, 0);
    EXPECT_EQ(a[1].laneCount, 1);

    const std::vector<GuiCalendar::LaneSpan> b = PackDayLanes({ TR(540, 600), TR(570, 630) });
    EXPECT_EQ(b[0].lane, 0);
    EXPECT_EQ(b[1].lane, 1);
    EXPECT_EQ(b[0].laneCount, 2);
    EXPECT_EQ(b[1].laneCount, 2);

    const std::vector<GuiCalendar::LaneSpan> c =
        PackDayLanes({ TR(540, 600), TR(555, 615), TR(585, 660) });
    EXPECT_EQ(c[0].lane, 0);
    EXPECT_EQ(c[1].lane, 1);
    EXPECT_EQ(c[2].lane, 2);
    EXPECT_EQ(c[0].laneCount, 3);
    EXPECT_EQ(c[2].laneCount, 3);

    const std::vector<GuiCalendar::LaneSpan> d =
        PackDayLanes({ TR(540, 660), TR(570, 600), TR(660, 720) });
    EXPECT_EQ(d[0].laneCount, 2);
    EXPECT_EQ(d[1].laneCount, 2);
    EXPECT_EQ(d[2].lane, 0);
    EXPECT_EQ(d[2].laneCount, 1);
}

void WeekBlockRectsMapDayAndLane() {
    const GuiCalendar::WeekFrame f = GuiCalendar::ComputeWeekFrame(700.0f, 600.0f, 1.0f);
    const GuiCalendar::LaneSpan single{ 0, 1 };
    const Gui::Rect d0 = GuiCalendar::ComputeWeekBlockRect(f, 0, single, 540, 600);
    const Gui::Rect d3 = GuiCalendar::ComputeWeekBlockRect(f, 3, single, 540, 600);
    EXPECT_NEAR(d0.top, d3.top, 0.01);
    EXPECT_NEAR(d0.bottom, d3.bottom, 0.01);
    EXPECT_TRUE(d3.left > d0.left);
    EXPECT_TRUE(d0.left >= f.columns[0].left - 0.01f);
    EXPECT_TRUE(d0.right <= f.columns[0].right + 0.01f);

    const Gui::Rect a = GuiCalendar::ComputeWeekBlockRect(f, 2, GuiCalendar::LaneSpan{ 0, 2 }, 540, 600);
    const Gui::Rect b = GuiCalendar::ComputeWeekBlockRect(f, 2, GuiCalendar::LaneSpan{ 1, 2 }, 540, 600);
    EXPECT_TRUE(b.left > a.left);
    EXPECT_TRUE(a.right <= b.left + 0.5f);
}

void WeekBodyHitTestRoutesDayHeaderBlockEmpty() {
    const GuiCalendar::WeekFrame f = GuiCalendar::ComputeWeekFrame(700.0f, 600.0f, 1.0f);
    const std::vector<GuiCalendar::WeekBlockRect> none;

    const GuiCalendar::WeekHitResult header =
        GuiCalendar::HitTestWeek(Mid(f.dayHeaders[3].left, f.dayHeaders[3].right),
                                 Mid(f.dayHeaders[3].top, f.dayHeaders[3].bottom), 0.0f, 1.0f, f, none);
    EXPECT_TRUE(header.kind == GuiCalendar::WeekHitKind::DayHeader);
    EXPECT_EQ(header.dayIndex, 3);

    const Gui::Rect rect = GuiCalendar::ComputeWeekBlockRect(f, 2, GuiCalendar::LaneSpan{ 0, 1 }, 60, 120);
    const std::vector<GuiCalendar::WeekBlockRect> blocks = { GuiCalendar::WeekBlockRect{ 7, 2, rect } };
    const float localY = f.timelineViewport.top + Mid(rect.top, rect.bottom);
    const GuiCalendar::WeekHitResult bh =
        GuiCalendar::HitTestWeek(Mid(rect.left, rect.right), localY, 0.0f, 1.0f, f, blocks);
    EXPECT_TRUE(bh.kind == GuiCalendar::WeekHitKind::Block);
    EXPECT_EQ(bh.blockId, 7);

    const GuiCalendar::WeekHitResult empty =
        GuiCalendar::HitTestWeek(Mid(f.columns[5].left, f.columns[5].right),
                                 f.timelineViewport.top + 4.0f, 0.0f, 1.0f, f, none);
    EXPECT_TRUE(empty.kind == GuiCalendar::WeekHitKind::EmptyColumn);
    EXPECT_EQ(empty.dayIndex, 5);
}

void MonthFrameHasFortyTwoCellGrid() {
    const GuiCalendar::MonthFrame f = GuiCalendar::ComputeMonthFrame(700.0f, 600.0f, 1.0f);
    EXPECT_TRUE(f.cellWidth > 0.0f);
    EXPECT_TRUE(f.cellHeight > 0.0f);
    EXPECT_EQ((int)f.cells.size(), 42);

    EXPECT_NEAR(f.cells[7].left, f.cells[0].left, 0.01);
    EXPECT_NEAR(f.cells[7].top, f.cells[0].bottom, 0.01);
    for (int c = 1; c < 7; ++c) {
        EXPECT_TRUE(std::fabs(f.cells[(size_t)c].left - f.cells[(size_t)(c - 1)].right) < 0.5f);
        EXPECT_TRUE(std::fabs(f.cells[(size_t)c].Width() - f.cells[0].Width()) < 1.0f);
        EXPECT_TRUE(std::fabs(f.weekdayHeaders[(size_t)c].left - f.cells[(size_t)c].left) < 0.5f);
    }
}

void MonthBodyHitTestReturnsCellIndex() {
    const GuiCalendar::MonthFrame f = GuiCalendar::ComputeMonthFrame(700.0f, 600.0f, 1.0f);
    const GuiCalendar::MonthHitResult first =
        GuiCalendar::HitTestMonth(Mid(f.cells[0].left, f.cells[0].right),
                                  Mid(f.cells[0].top, f.cells[0].bottom), 1.0f, f);
    EXPECT_TRUE(first.kind == GuiCalendar::MonthHitKind::Cell);
    EXPECT_EQ(first.cellIndex, 0);

    const GuiCalendar::MonthHitResult last =
        GuiCalendar::HitTestMonth(Mid(f.cells[41].left, f.cells[41].right),
                                  Mid(f.cells[41].top, f.cells[41].bottom), 1.0f, f);
    EXPECT_TRUE(last.kind == GuiCalendar::MonthHitKind::Cell);
    EXPECT_EQ(last.cellIndex, 41);
}

const TestCase kTests[] = {
    {"HeaderGroupsNavConnectsSegmentsAndPinsToday", HeaderGroupsNavConnectsSegmentsAndPinsToday},
    {"HeaderHidesTodayWhenNotRequested", HeaderHidesTodayWhenNotRequested},
    {"HeaderDegradesOnNarrowWidthsWithoutOverlap", HeaderDegradesOnNarrowWidthsWithoutOverlap},
    {"WeekFrameHasSevenEqualContiguousColumns", WeekFrameHasSevenEqualContiguousColumns},
    {"LanePackingIsDeterministic", LanePackingIsDeterministic},
    {"WeekBlockRectsMapDayAndLane", WeekBlockRectsMapDayAndLane},
    {"WeekBodyHitTestRoutesDayHeaderBlockEmpty", WeekBodyHitTestRoutesDayHeaderBlockEmpty},
    {"MonthFrameHasFortyTwoCellGrid", MonthFrameHasFortyTwoCellGrid},
    {"MonthBodyHitTestReturnsCellIndex", MonthBodyHitTestReturnsCellIndex},
};

} // namespace

int main() {
    return RunTests("calendar_layout", kTests);
}
