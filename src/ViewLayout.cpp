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
    out.calendar = Gui::Rect{ out.pin.left - gap - button, top, out.pin.left - gap, top + button };
    out.theme = Gui::Rect{ out.calendar.left - gap - button, top, out.calendar.left - gap, top + button };
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

    float x = pad;
    const float maxRight = out.addList.left - tabGap;
    for (const TabMetric& tab : tabs) {
        const float titleW = S(7.0f, dpiScale) * static_cast<float>(tab.titleLength);
        const float countW = CountWidth(tab.activeCount, dpiScale);
        float wantW = S(20.0f, dpiScale) + titleW + (countW > 0.0f ? S(7.0f, dpiScale) + countW : 0.0f);
        if (wantW < tabMinW) wantW = tabMinW;
        if (wantW > tabMaxW) wantW = tabMaxW;
        if (x + wantW > maxRight) break;
        out.tabs.push_back(TabRect{ tab.listIndex, tab.kind, Gui::Rect{ x, tabY, x + wantW, tabY + tabH } });
        x += wantW + tabGap;
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

// 空列表提示占满内容视口，渲染层在其中居中绘制图标 / 文案 / 新建按钮。
Gui::Rect ComputeEmptyActivePrompt(float windowWidth, float viewportHeight, float dpiScale) {
    const float pad = S(Theme::kPadX, dpiScale);
    float height = viewportHeight;
    const float minHeight = S(160.0f, dpiScale);
    if (height < minHeight) height = minHeight;
    return Gui::Rect{ pad, 0.0f, windowWidth - pad, height };
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
        if (title.calendar.Contains(x, y)) return ChromeHitResult{ ChromeHit::Calendar, -1 };
        if (title.pin.Contains(x, y)) return ChromeHitResult{ ChromeHit::Pin, -1 };
        if (title.close.Contains(x, y)) return ChromeHitResult{ ChromeHit::Close, -1 };
        return {};
    }

    if (y < S(Theme::kTitleH + Theme::kTabsH, dpiScale)) {
        if (addList.Contains(x, y)) return ChromeHitResult{ ChromeHit::AddList, -1 };
        for (const TabRect& tab : tabs) {
            if (!tab.rect.Contains(x, y)) continue;
            return ChromeHitResult{ ChromeHit::ListTab, tab.listIndex };
        }
    }
    return {};
}

} // namespace GuiLayout
