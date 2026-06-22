#pragma once

#include "GuiTypes.h"

#include <cstddef>
#include <vector>

namespace GuiLayout {

struct TitleButtons {
    Gui::Rect close;
    Gui::Rect pin;
    Gui::Rect calendar;
    Gui::Rect theme;
    Gui::Rect menu;
};

enum class TabKind {
    List,
};

struct TabMetric {
    int listIndex = -1;
    std::size_t titleLength = 0;
    int activeCount = 0;
    TabKind kind = TabKind::List;
};

struct TabRect {
    int listIndex = -1;
    TabKind kind = TabKind::List;
    Gui::Rect rect;
};

struct TabStrip {
    Gui::Rect addList;
    std::vector<TabRect> tabs;
};

struct RowControls {
    Gui::Rect row;
    Gui::Rect disclosure;
    Gui::Rect check;
    Gui::Rect text;
    Gui::Rect del;
    Gui::Rect handle;
};

enum class RowHit {
    None,
    TreeToggle,
    Check,
    Text,
    Delete,
    Handle,
};

enum class ChromeHit {
    None,
    Menu,
    Theme,
    Pin,
    Close,
    AddList,
    ListTab,
    Calendar,
};

struct ChromeHitResult {
    ChromeHit kind = ChromeHit::None;
    int listIndex = -1;
};

TitleButtons ComputeTitleButtons(float windowWidth, float dpiScale);
TabStrip ComputeTabStrip(float windowWidth, float dpiScale, const std::vector<TabMetric>& tabs);
RowControls ComputeRowControls(float windowWidth, float docY, float rowHeight, int level, float dpiScale);
Gui::Rect ComputeEmptyActivePrompt(float windowWidth, float viewportHeight, float dpiScale);

RowHit HitTestRowControls(const RowControls& row, float x, float docY, bool hasChildren, bool completed);
ChromeHitResult HitTestChrome(float x, float y, float dpiScale,
                              const TitleButtons& title,
                              const Gui::Rect& addList,
                              const std::vector<TabRect>& tabs);

} // namespace GuiLayout
