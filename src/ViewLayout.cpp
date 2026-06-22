#include "ViewLayout.h"

#include "Theme.h"
#include "TodoModel.h"

#include <cwchar>

namespace GuiLayout {
namespace {

float S(float value, float dpiScale) {
    return value * dpiScale;
}

float CountWidth(int activeCount, float dpiScale) {
    if (activeCount <= 0) return 0.0f;
    wchar_t buf[16];
#ifdef _WIN32
    swprintf_s(buf, L"%d", activeCount);
#else
    std::swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%d", activeCount);
#endif
    return S(15.0f + 6.0f * static_cast<float>(std::wcslen(buf)), dpiScale);
}

} // namespace

TitleButtons ComputeTitleButtons(float windowWidth, float dpiScale) {
    const float button = S(24.0f, dpiScale);
    const float margin = S(8.0f, dpiScale);
    const float gap = S(4.0f, dpiScale);
    const float top = (S(Theme::kTitleH, dpiScale) - button) / 2.0f;

    TitleButtons out;
    out.close = Gui::Rect{ windowWidth - margin - button, top, windowWidth - margin, top + button };
    out.pin = Gui::Rect{ out.close.left - gap - button, top, out.close.left - gap, top + button };
    out.theme = Gui::Rect{ out.pin.left - gap - button, top, out.pin.left - gap, top + button };
    out.menu = Gui::Rect{ out.theme.left - gap - button, top, out.theme.left - gap, top + button };
    return out;
}

TabStrip ComputeTabStrip(float windowWidth, float dpiScale, const std::vector<TabMetric>& tabs) {
    const float pad = S(Theme::kPadX, dpiScale);
    const float tabTop = S(Theme::kTitleH, dpiScale);
    const float tabsH = S(Theme::kTabsH, dpiScale);
    const float addSize = S(24.0f, dpiScale);
    const float tabGap = S(6.0f, dpiScale);
    const float tabMinW = S(52.0f, dpiScale);
    const float tabMaxW = S(118.0f, dpiScale);
    const float tabH = S(28.0f, dpiScale);
    const float tabY = tabTop + (tabsH - tabH) / 2.0f;

    TabStrip out;
    out.addList = Gui::Rect{
        windowWidth - pad - addSize,
        tabTop + (tabsH - addSize) / 2.0f,
        windowWidth - pad,
        tabTop + (tabsH + addSize) / 2.0f,
    };

    auto widthOf = [&](const TabMetric& tab) {
        const float titleW = S(7.0f, dpiScale) * static_cast<float>(tab.titleLength);
        const float countW = tab.kind == TabKind::List ? CountWidth(tab.activeCount, dpiScale) : 0.0f;
        float wantW = S(20.0f, dpiScale) + titleW + (countW > 0.0f ? S(7.0f, dpiScale) + countW : 0.0f);
        if (wantW < tabMinW) wantW = tabMinW;
        if (wantW > tabMaxW) wantW = tabMaxW;
        return wantW;
    };

    // 日历是固定的视图开关，始终排在 list 标签之后并始终可见。先为它预留宽度，溢出时优先
    // 丢最右的 list 标签，而不是把日历挤掉。
    const TabMetric* calendar = nullptr;
    for (const TabMetric& tab : tabs) {
        if (tab.kind == TabKind::Calendar) { calendar = &tab; break; }
    }
    const float calendarW = calendar ? widthOf(*calendar) : 0.0f;
    const float listMaxRight =
        out.addList.left - tabGap - (calendar ? calendarW + tabGap : 0.0f);

    float x = pad;
    for (const TabMetric& tab : tabs) {
        if (tab.kind == TabKind::Calendar) continue;
        const float wantW = widthOf(tab);
        if (x + wantW > listMaxRight) break;
        out.tabs.push_back(TabRect{ tab.listIndex, tab.kind, Gui::Rect{ x, tabY, x + wantW, tabY + tabH } });
        x += wantW + tabGap;
    }
    if (calendar) {
        out.tabs.push_back(TabRect{ -1, TabKind::Calendar,
                                    Gui::Rect{ x, tabY, x + calendarW, tabY + tabH } });
    }
    return out;
}

RowControls ComputeRowControls(float windowWidth, float docY, float rowHeight, int level, float dpiScale) {
    const float pad = S(Theme::kPadX, dpiScale);
    const float checkSz = S(Theme::kCheckSize, dpiScale);
    const float handleW = S(18.0f, dpiScale);
    const float delW = S(20.0f, dpiScale);
    const float gap = S(6.0f, dpiScale);
    const float treeW = S(14.0f, dpiScale);
    const float indent = S(18.0f, dpiScale) * static_cast<float>(ClampTodoLevel(level));
    const float treeLeft = pad + indent;
    const float checkLeft = treeLeft + treeW;
    const float textLeft = checkLeft + checkSz + S(8.0f, dpiScale);
    float textRight = windowWidth - pad - handleW - gap - delW - gap;
    if (textRight < textLeft + S(20.0f, dpiScale)) textRight = textLeft + S(20.0f, dpiScale);

    const float controlTop = docY + S(7.0f, dpiScale);
    RowControls out;
    out.row = Gui::Rect{ pad, docY, windowWidth - pad, docY + rowHeight };
    out.disclosure = Gui::Rect{ treeLeft, controlTop, treeLeft + treeW, controlTop + S(20.0f, dpiScale) };
    out.check = Gui::Rect{ checkLeft, docY + S(8.0f, dpiScale), checkLeft + checkSz, docY + S(8.0f, dpiScale) + checkSz };
    out.handle = Gui::Rect{ windowWidth - pad - handleW, controlTop, windowWidth - pad, controlTop + S(20.0f, dpiScale) };
    out.del = Gui::Rect{ out.handle.left - gap - delW, controlTop, out.handle.left - gap, controlTop + delW };
    out.text = Gui::Rect{ textLeft, docY, textRight, docY + rowHeight };
    return out;
}

Gui::Rect ComputeEmptyActivePrompt(float windowWidth, float docY, float viewportHeight,
                                   bool listEmpty, float dpiScale) {
    (void)viewportHeight;
    const float pad = S(Theme::kPadX, dpiScale);
    const float top = listEmpty ? docY + S(12.0f, dpiScale) : docY;
    // 空列表只给一张紧凑的提示卡片，不再撑满整个视口。
    const float height = listEmpty ? S(76.0f, dpiScale) : S(Theme::kRowH, dpiScale);
    return Gui::Rect{ pad, top, windowWidth - pad, top + height };
}

RowHit HitTestRowControls(const RowControls& row, float x, float docY, bool hasChildren, bool completed) {
    if (!row.row.Contains(x, docY)) return RowHit::None;
    if (hasChildren && row.disclosure.Contains(x, docY)) return RowHit::TreeToggle;
    if (row.check.Contains(x, docY)) return RowHit::Check;
    if (row.del.Contains(x, docY)) return RowHit::Delete;
    if (!completed && row.handle.Contains(x, docY)) return RowHit::Handle;
    if (!completed && row.text.Contains(x, docY)) return RowHit::Text;
    return RowHit::None;
}

ChromeHitResult HitTestChrome(float x, float y, float dpiScale,
                              const TitleButtons& title,
                              const Gui::Rect& addList,
                              const std::vector<TabRect>& tabs) {
    if (y < S(Theme::kTitleH, dpiScale)) {
        if (title.menu.Contains(x, y)) return ChromeHitResult{ ChromeHit::Menu, -1 };
        if (title.theme.Contains(x, y)) return ChromeHitResult{ ChromeHit::Theme, -1 };
        if (title.pin.Contains(x, y)) return ChromeHitResult{ ChromeHit::Pin, -1 };
        if (title.close.Contains(x, y)) return ChromeHitResult{ ChromeHit::Close, -1 };
        return {};
    }

    if (y < S(Theme::kTitleH + Theme::kTabsH, dpiScale)) {
        if (addList.Contains(x, y)) return ChromeHitResult{ ChromeHit::AddList, -1 };
        for (const TabRect& tab : tabs) {
            if (!tab.rect.Contains(x, y)) continue;
            if (tab.kind == TabKind::Calendar) return ChromeHitResult{ ChromeHit::CalendarTab, -1 };
            return ChromeHitResult{ ChromeHit::ListTab, tab.listIndex };
        }
    }
    return {};
}

} // namespace GuiLayout
