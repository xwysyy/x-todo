#include "CalendarLayout.h"

#include "CalendarModel.h"
#include "Theme.h"

#include <cmath>
#include <cwchar>

namespace GuiCalendar {
namespace {

float S(float value, float dpiScale) {
    return value * dpiScale;
}

int RoundToInt(float value) {
    return static_cast<int>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

int Digit(wchar_t ch) {
    return (ch >= L'0' && ch <= L'9') ? static_cast<int>(ch - L'0') : -1;
}

} // namespace

Frame ComputeFrame(float windowWidth, float viewportHeight, float dpiScale) {
    const float pad = S(Theme::kPadX, dpiScale);
    const float headerH = S(52.0f, dpiScale);
    const float allDayH = S(32.0f, dpiScale);
    const float statusH = S(24.0f, dpiScale);
    const float timelineTop = headerH + allDayH + S(8.0f, dpiScale);
    const float gutterW = S(54.0f, dpiScale);
    const float hourH = S(56.0f, dpiScale);
    const float navSize = S(26.0f, dpiScale);
    const float navTop = (headerH - navSize) * 0.5f;
    const float todayW = S(46.0f, dpiScale);
    const float gap = S(6.0f, dpiScale);

    Frame out;
    out.dateHeader = Gui::Rect{ pad, 0.0f, windowWidth - pad, headerH };
    out.prevDay = Gui::Rect{ pad, navTop, pad + navSize, navTop + navSize };
    out.nextDay = Gui::Rect{ windowWidth - pad - navSize, navTop,
                             windowWidth - pad, navTop + navSize };
    out.today = Gui::Rect{ out.nextDay.left - gap - todayW, navTop,
                           out.nextDay.left - gap, navTop + navSize };
    out.allDay = Gui::Rect{ pad, headerH, windowWidth - pad, headerH + allDayH };

    float timelineBottom = viewportHeight - statusH;
    if (timelineBottom < timelineTop) timelineBottom = timelineTop;
    out.timelineViewport = Gui::Rect{ 0.0f, timelineTop, windowWidth, timelineBottom };
    out.gutter = Gui::Rect{ 0.0f, timelineTop, gutterW, timelineBottom };
    out.lane = Gui::Rect{ gutterW, timelineTop, windowWidth - pad, timelineBottom };
    out.statusBar = Gui::Rect{ 0.0f, timelineBottom, windowWidth,
                               viewportHeight > timelineBottom ? viewportHeight : timelineBottom };
    out.hourHeight = hourH;
    out.contentHeight = 24.0f * hourH;
    return out;
}

Gui::Rect ComputeBlockRect(const Frame& frame, int blockId, int startMinute, int endMinute) {
    (void)blockId;
    const float y1 = (static_cast<float>(ClampCalendarMinute(startMinute)) / 60.0f) * frame.hourHeight;
    const float y2 = (static_cast<float>(ClampCalendarMinute(endMinute)) / 60.0f) * frame.hourHeight;
    float bottom = y2;
    const float minHeight = frame.hourHeight * (34.0f / 56.0f);
    if (bottom < y1 + minHeight) bottom = y1 + minHeight;
    return Gui::Rect{ frame.lane.left + 4.0f, y1 + 2.0f, frame.lane.right - 4.0f, bottom - 2.0f };
}

HitResult HitTest(float x, float y, float scroll, float dpiScale, const Frame& frame,
                  const std::vector<BlockRect>& blocks) {
    if (frame.prevDay.Contains(x, y)) return HitResult{ HitKind::PrevDay, -1 };
    if (frame.nextDay.Contains(x, y)) return HitResult{ HitKind::NextDay, -1 };
    if (frame.today.Contains(x, y)) return HitResult{ HitKind::Today, -1 };
    if (!frame.timelineViewport.Contains(x, y)) return {};

    const float docY = y - frame.timelineViewport.top + scroll;
    const float handle = S(7.0f, dpiScale);
    for (const BlockRect& block : blocks) {
        const Gui::Rect& r = block.rect;
        if (x < r.left || x >= r.right || docY < r.top || docY >= r.bottom) continue;
        if (docY < r.top + handle) return HitResult{ HitKind::ResizeStart, block.blockId };
        if (docY >= r.bottom - handle) return HitResult{ HitKind::ResizeEnd, block.blockId };
        return HitResult{ HitKind::BlockBody, block.blockId };
    }

    if (frame.lane.Contains(x, y)) return HitResult{ HitKind::EmptyTimeline, -1 };
    return {};
}

int MinuteFromPoint(float y, float scroll, const Frame& frame) {
    const float docY = y - frame.timelineViewport.top + scroll;
    if (frame.hourHeight <= 0.0f) return 0;
    const float minute = (docY / frame.hourHeight) * 60.0f;
    return ClampCalendarMinute(RoundToInt(minute));
}

int SnapMinute(int minute, int stepMinutes) {
    if (stepMinutes <= 0) stepMinutes = 15;
    minute = ClampCalendarMinute(minute);
    const int snapped = ((minute + stepMinutes / 2) / stepMinutes) * stepMinutes;
    return ClampCalendarMinute(snapped);
}

bool DragExceeded(float startX, float startY, float x, float y, float dpiScale) {
    const float threshold = S(8.0f, dpiScale);
    const float dx = x - startX;
    const float dy = y - startY;
    return dx * dx + dy * dy >= threshold * threshold;
}

TimeRange RangeFromDrag(int anchorMinute, int currentMinute, int minDuration) {
    if (minDuration <= 0) minDuration = 15;
    int a = SnapMinute(anchorMinute);
    int b = SnapMinute(currentMinute);
    if (a == b) b = a + minDuration;
    int start = a < b ? a : b;
    int end = a < b ? b : a;
    if (end - start < minDuration) end = start + minDuration;
    if (end > 1440) {
        end = 1440;
        start = end - minDuration;
    }
    if (start < 0) start = 0;
    return TimeRange{ start, end };
}

float ScrollForMinute(int minute, float viewportHeight, const Frame& frame) {
    minute = ClampCalendarMinute(minute);
    const float timelineH = frame.timelineViewport.Height();
    const float visible = viewportHeight < timelineH ? viewportHeight : timelineH;
    float scroll = (static_cast<float>(minute) / 60.0f) * frame.hourHeight - visible * 0.35f;
    if (scroll < 0.0f) scroll = 0.0f;
    const float maxScroll = frame.contentHeight - timelineH;
    if (maxScroll > 0.0f && scroll > maxScroll) scroll = maxScroll;
    return scroll;
}

bool ParseTimeText(const std::wstring& text, int& minute) {
    std::wstring s;
    for (wchar_t ch : text) {
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') s.push_back(ch);
    }
    const size_t colon = s.find(L':');
    if (colon == std::wstring::npos || colon == 0 || colon > 2 || s.size() - colon - 1 != 2)
        return false;

    int hour = 0;
    for (size_t i = 0; i < colon; ++i) {
        const int d = Digit(s[i]);
        if (d < 0) return false;
        hour = hour * 10 + d;
    }
    const int m1 = Digit(s[colon + 1]);
    const int m2 = Digit(s[colon + 2]);
    if (m1 < 0 || m2 < 0) return false;
    const int mins = m1 * 10 + m2;
    if (hour < 0 || hour > 24 || mins < 0 || mins > 59) return false;
    if (hour == 24 && mins != 0) return false;
    minute = hour * 60 + mins;
    return true;
}

std::wstring FormatTimeText(int minute) {
    minute = ClampCalendarMinute(minute);
    wchar_t buf[8];
#ifdef _WIN32
    swprintf_s(buf, L"%02d:%02d", minute / 60, minute % 60);
#else
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%02d:%02d", minute / 60, minute % 60);
#endif
    return std::wstring(buf);
}

} // namespace GuiCalendar
