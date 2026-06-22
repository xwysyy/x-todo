#pragma once

#include "GuiTypes.h"

#include <string>
#include <vector>

namespace GuiCalendar {

struct Frame {
    Gui::Rect dateHeader;
    Gui::Rect prevDay;
    Gui::Rect nextDay;
    Gui::Rect today;
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

enum class HitKind {
    None,
    PrevDay,
    NextDay,
    Today,
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

Frame ComputeFrame(float windowWidth, float viewportHeight, float dpiScale);
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

} // namespace GuiCalendar
