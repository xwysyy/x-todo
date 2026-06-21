#include "MainWindow.h"
#include "EditIntent.h"
#include "Theme.h"
#include "ViewLayout.h"
#include "WindowHitTest.h"

#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>
#include <utility>

namespace {
bool InRect(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

Gui::Rect ToGuiRect(const D2D1_RECT_F& r) {
    return Gui::Rect{ r.left, r.top, r.right, r.bottom };
}

D2D1_RECT_F ToD2DRect(const Gui::Rect& r) {
    return D2D1::RectF(r.left, r.top, r.right, r.bottom);
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

int RoundToInt(float v) { return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f); }
constexpr int kHoverAddRow = -2;

std::wstring ReadWindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring text((size_t)len + 1, L'\0');
    int got = GetWindowTextW(hwnd, text.data(), len + 1);
    if (got < 0) got = 0;
    text.resize((size_t)got);
    return text;
}

std::wstring NormalizeTodoText(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return Trim(text);
}

bool IsWrapDelimiter(wchar_t ch) {
    switch (ch) {
    case L'/': case L'\\': case L'.': case L'-': case L'_':
    case L'?': case L'&': case L'=': case L'#': case L':':
        return true;
    default:
        return false;
    }
}

std::wstring MakeBreakableText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    int run = 0;
    for (wchar_t ch : text) {
        out.push_back(ch);
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            run = 0;
            continue;
        }
        run++;
        if (IsWrapDelimiter(ch) || run >= 24) {
            out.push_back(L'\x200B');
            run = 0;
        }
    }
    return out;
}
} // namespace

// ——————————————————————————— 布局 ———————————————————————————

float MainWindow::ContentTop() const { return S(Theme::kTitleH + Theme::kTabsH); }
float MainWindow::ContentHeight() const { return contentH_; }

float MainWindow::ViewportHeight() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float h = (float)(rc.bottom - rc.top) - ContentTop() - S(Theme::kFooterH);
    return h < 0 ? 0 : h;
}

void MainWindow::ClampScroll() {
    float maxScroll = contentH_ - ViewportHeight();
    if (maxScroll < 0) maxScroll = 0;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > maxScroll) scroll_ = maxScroll;
}

void MainWindow::ScrollItemIntoView(int itemIndex) {
    for (const RowLayout& r : rows_) {
        if (r.itemIndex != itemIndex) continue;
        float viewH = ViewportHeight();
        if (r.row.top < scroll_) scroll_ = r.row.top;
        else if (r.row.bottom > scroll_ + viewH) scroll_ = r.row.bottom - viewH;
        ClampScroll();
        return;
    }
    ClampScroll();
}

int MainWindow::PreviousVisibleActiveItem(int itemIndex) const {
    int previous = -1;
    for (const RowLayout& r : rows_) {
        if (r.completed) break;
        if (r.itemIndex == itemIndex) return previous;
        previous = r.itemIndex;
    }
    return -1;
}

void MainWindow::RebuildLayout() {
    for (auto& r : rows_) // 释放上一轮缓存的删除线布局，避免泄漏
        if (r.strikeLayout) { r.strikeLayout->Release(); r.strikeLayout = nullptr; }
    rows_.clear();
    listTabs_.clear();
    addListRect_ = D2D1::RectF(0, 0, 0, 0);

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left);

    const float pad     = S(Theme::kPadX);
    const float baseRowH = S(Theme::kRowH);

    float docY = 0;
    const int active = model_.ActiveCount();
    const int total  = model_.Count();

    auto makeRow = [&](int itemIndex, bool completed) {
        RowLayout r{};
        r.itemIndex = itemIndex;
        r.completed = completed;
        const TodoItem& item = model_.Items()[itemIndex];
        std::wstring measureText;
        const std::wstring& savedText = item.text;
        const std::wstring* text = &savedText;
        if (editing() && editIndex_ == itemIndex && edit_) {
            measureText = ReadWindowText(edit_);
            text = &measureText;
        }
        const GuiLayout::RowControls baseControls =
            GuiLayout::ComputeRowControls(W, docY, baseRowH, item.level, dpiScale());
        const float rowH = MeasureRowHeight(*text, baseControls.text.Width());
        const GuiLayout::RowControls finalControls =
            GuiLayout::ComputeRowControls(W, docY, rowH, item.level, dpiScale());
        r.row        = ToD2DRect(finalControls.row);
        r.disclosure = ToD2DRect(finalControls.disclosure);
        r.check      = ToD2DRect(finalControls.check);
        r.text       = ToD2DRect(finalControls.text);
        r.del        = ToD2DRect(finalControls.del);
        r.handle     = ToD2DRect(finalControls.handle);
        r.hasChildren = model_.HasChildren(itemIndex);
        r.collapsed = item.collapsed;
        rows_.push_back(r);
        docY += rowH;
    };

    for (int i = 0; i < active; ) {
        makeRow(i, false);
        i = model_.Items()[(size_t)i].collapsed ? model_.SubtreeEnd(i) : i + 1;
    }

    addRect_ = D2D1::RectF(pad, docY, W - pad, docY + baseRowH);
    docY += baseRowH;

    if (total - active > 0) {
        sectionRect_ = D2D1::RectF(pad, docY, W - pad, docY + S(Theme::kSectionH));
        clearRect_   = D2D1::RectF(W - pad - S(44), docY, W - pad, docY + S(Theme::kSectionH));
        docY += S(Theme::kSectionH);
        if (model_.CurrentList().completedExpanded) {
            for (int i = active; i < total; ) {
                makeRow(i, true);
                i = model_.Items()[(size_t)i].collapsed ? model_.SubtreeEnd(i) : i + 1;
            }
        }
    } else {
        sectionRect_ = D2D1::RectF(0, 0, 0, 0);
        clearRect_   = D2D1::RectF(0, 0, 0, 0);
    }
    contentH_ = docY;

    const GuiLayout::TitleButtons title = GuiLayout::ComputeTitleButtons(W, dpiScale());
    closeRect_ = ToD2DRect(title.close);
    pinRect_   = ToD2DRect(title.pin);
    themeRect_ = ToD2DRect(title.theme);
    menuRect_  = ToD2DRect(title.menu);

    std::vector<GuiLayout::TabMetric> tabMetrics;
    tabMetrics.reserve((size_t)model_.ListCount());
    for (int i = 0; i < model_.ListCount(); ++i) {
        const TodoList* list = model_.ListAt(i);
        if (!list) continue;
        tabMetrics.push_back(GuiLayout::TabMetric{ i, list->title.size(), list->activeCount });
    }
    const GuiLayout::TabStrip tabStrip = GuiLayout::ComputeTabStrip(W, dpiScale(), tabMetrics);
    addListRect_ = ToD2DRect(tabStrip.addList);
    for (const GuiLayout::TabRect& tab : tabStrip.tabs) {
        listTabs_.push_back(ListTabLayout{ tab.listIndex, ToD2DRect(tab.rect) });
    }
}

float MainWindow::MeasureRowHeight(const std::wstring& text, float textWidth) {
    const float base = S(Theme::kRowH);
    if (!dwrite_ || !textFormat_ || text.empty() || textWidth <= S(8)) return base;

    IDWriteTextLayout* layout = nullptr;
    std::wstring breakable = MakeBreakableText(text);
    HRESULT hr = dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(), textFormat_,
                                           textWidth, S(2000), &layout);
    if (FAILED(hr) || !layout) return base;
    DWRITE_TEXT_METRICS tm{};
    float h = base;
    if (SUCCEEDED(layout->GetMetrics(&tm))) {
        float want = (float)std::ceil(tm.height + S(14));
        if (want > h) h = want;
    }
    layout->Release();
    return h;
}

// ——————————————————————————— 命中测试 ———————————————————————————

MainWindow::Hit MainWindow::HitTest(float x, float y) {
    Hit h;

    if (y < ContentTop()) {
        GuiLayout::TitleButtons title;
        title.close = ToGuiRect(closeRect_);
        title.pin = ToGuiRect(pinRect_);
        title.theme = ToGuiRect(themeRect_);
        title.menu = ToGuiRect(menuRect_);
        std::vector<GuiLayout::TabRect> tabs;
        tabs.reserve(listTabs_.size());
        for (const ListTabLayout& tab : listTabs_)
            tabs.push_back(GuiLayout::TabRect{ tab.listIndex, ToGuiRect(tab.rect) });

        const GuiLayout::ChromeHitResult chrome =
            GuiLayout::HitTestChrome(x, y, dpiScale(), title, ToGuiRect(addListRect_), tabs);
        switch (chrome.kind) {
        case GuiLayout::ChromeHit::Menu:    h.kind = HitKind::Menu;    return h;
        case GuiLayout::ChromeHit::Theme:   h.kind = HitKind::Theme;   return h;
        case GuiLayout::ChromeHit::Pin:     h.kind = HitKind::Pin;     return h;
        case GuiLayout::ChromeHit::Close:   h.kind = HitKind::Close;   return h;
        case GuiLayout::ChromeHit::AddList: h.kind = HitKind::AddList; return h;
        case GuiLayout::ChromeHit::ListTab:
            h.kind = HitKind::ListTab;
            h.itemIndex = chrome.listIndex;
            return h;
        case GuiLayout::ChromeHit::None:
            return h; // 标题栏空白交给 NCHITTEST 拖动；标签栏空白无动作
        }
    }

    float docY = y - ContentTop() + scroll_;
    D2D1_POINT_2F dp{ x, docY };

    if (addRect_.bottom > addRect_.top && InRect(addRect_, dp)) {
        h.kind = HitKind::Add;
        return h;
    }

    if (sectionRect_.bottom > sectionRect_.top &&
        docY >= sectionRect_.top && docY < sectionRect_.bottom) {
        if (InRect(clearRect_, dp)) { h.kind = HitKind::Clear; return h; }
        h.kind = HitKind::Section;
        return h;
    }

    for (size_t i = 0; i < rows_.size(); i++) {
        const RowLayout& r = rows_[i];
        if (docY >= r.row.top && docY < r.row.bottom) {
            h.rowIndex = (int)i;
            h.itemIndex = r.itemIndex;
            GuiLayout::RowControls controls;
            controls.row = ToGuiRect(r.row);
            controls.disclosure = ToGuiRect(r.disclosure);
            controls.check = ToGuiRect(r.check);
            controls.text = ToGuiRect(r.text);
            controls.del = ToGuiRect(r.del);
            controls.handle = ToGuiRect(r.handle);
            switch (GuiLayout::HitTestRowControls(controls, x, docY, r.hasChildren, r.completed)) {
            case GuiLayout::RowHit::TreeToggle: h.kind = HitKind::TreeToggle; return h;
            case GuiLayout::RowHit::Check:      h.kind = HitKind::Check;      return h;
            case GuiLayout::RowHit::Text:       h.kind = HitKind::Text;       return h;
            case GuiLayout::RowHit::Delete:     h.kind = HitKind::Delete;     return h;
            case GuiLayout::RowHit::Handle:     h.kind = HitKind::Handle;     return h;
            case GuiLayout::RowHit::None:       break;
            }
            return h;
        }
    }
    return h;
}

// ——————————————————————————— 渲染 ———————————————————————————

void MainWindow::FillRect(const D2D1_RECT_F& r, uint32_t rgb, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->FillRectangle(r, brush_);
}

void MainWindow::StrokeRect(const D2D1_RECT_F& r, uint32_t rgb, float w, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->DrawRectangle(r, brush_, w);
}

void MainWindow::FillRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->FillRoundedRectangle(rr, brush_);
}

void MainWindow::StrokeRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float w, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->DrawRoundedRectangle(rr, brush_, w);
}

void MainWindow::DrawSurfaceFrame(const D2D1_RECT_F& r, float radius, uint32_t fill,
                                  uint32_t edge, float stroke) {
    D2D1_ROUNDED_RECT rr{ r, radius, radius };
    FillRoundRect(rr, fill);
    StrokeRoundRect(rr, edge, stroke);
}

void MainWindow::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t rgb,
                      IDWriteTextFormat* fmt) {
    if (s.empty()) return;
    brush_->SetColor(Theme::D2DColor(rgb));
    rt_->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, brush_);
}

void MainWindow::DrawCheckbox(const D2D1_RECT_F& box, bool checked) {
    D2D1_ROUNDED_RECT rr{ box, S(4), S(4) };
    if (checked) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
        rt_->FillRoundedRectangle(rr, brush_);
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkMark));
        float l = box.left, t = box.top, w = box.right - box.left, hh = box.bottom - box.top;
        rt_->DrawLine(D2D1::Point2F(l + w * 0.24f, t + hh * 0.52f),
                      D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f), brush_, S(2));
        rt_->DrawLine(D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f),
                      D2D1::Point2F(l + w * 0.76f, t + hh * 0.30f), brush_, S(2));
    } else {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkBorder));
        rt_->DrawRoundedRectangle(rr, brush_, S(1.5f));
    }
}

void MainWindow::DrawRow(const RowLayout& r, bool hovered) {
    if (hovered) FillRect(r.row, theme_.colors.rowHover); // 最终消费色，不再混 alpha

    int level = ClampTodoLevel(model_.Items()[r.itemIndex].level);
    if (level > 0) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak, 0.45f));
        float x = r.check.left - S(9);
        rt_->DrawLine(D2D1::Point2F(x, r.row.top + S(7)),
                      D2D1::Point2F(x, r.row.bottom - S(7)), brush_, S(1));
    }

    if (r.hasChildren)
        Text(r.collapsed ? L"▸" : L"▾", r.disclosure, theme_.colors.textWeak, smallFormat_);

    DrawCheckbox(r.check, r.completed);

    if (!(editing() && editIndex_ == r.itemIndex)) {
        const std::wstring& s = model_.Items()[r.itemIndex].text;
        std::wstring breakable = MakeBreakableText(s);
        if (r.completed) {
            if (!r.strikeLayout && !s.empty()) { // 仅首帧创建带删除线的布局，之后复用
                IDWriteTextLayout* layout = nullptr;
                if (SUCCEEDED(dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(), textFormat_,
                                                        r.text.right - r.text.left,
                                                        r.text.bottom - r.text.top, &layout))) {
                    DWRITE_TEXT_RANGE rg{ 0, (UINT32)breakable.size() };
                    layout->SetStrikethrough(TRUE, rg);
                    r.strikeLayout = layout;
                }
            }
            if (r.strikeLayout) {
                brush_->SetColor(Theme::D2DColor(theme_.colors.textDone));
                rt_->DrawTextLayout(D2D1::Point2F(r.text.left, r.text.top), r.strikeLayout, brush_);
            }
        } else {
            Text(breakable, r.text, theme_.colors.text, textFormat_);
        }
    }

    if (hovered) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.danger));
        D2D1_RECT_F d = r.del;
        float p = S(5);
        rt_->DrawLine(D2D1::Point2F(d.left + p, d.top + p), D2D1::Point2F(d.right - p, d.bottom - p), brush_, S(1.6f));
        rt_->DrawLine(D2D1::Point2F(d.right - p, d.top + p), D2D1::Point2F(d.left + p, d.bottom - p), brush_, S(1.6f));

        if (!r.completed) {
            brush_->SetColor(Theme::D2DColor(theme_.colors.handle));
            float cx = (r.handle.left + r.handle.right) / 2;
            float hh = r.handle.bottom - r.handle.top;
            for (int i = 0; i < 3; i++) {
                float yy = r.handle.top + hh * (0.34f + 0.16f * i);
                rt_->DrawLine(D2D1::Point2F(cx - S(5), yy), D2D1::Point2F(cx + S(5), yy), brush_, S(1.5f));
            }
        }
    }
}

void MainWindow::DrawSection() {
    if (model_.CompletedCount() == 0) return;

    brush_->SetColor(Theme::D2DColor(theme_.colors.divider));
    rt_->DrawLine(D2D1::Point2F(sectionRect_.left, sectionRect_.top),
                  D2D1::Point2F(sectionRect_.right, sectionRect_.top), brush_, S(1));

    wchar_t buf[96];
    swprintf_s(buf, L"%s (%d) %s", T(Str::Completed, lang_), model_.CompletedCount(),
               model_.CurrentList().completedExpanded ? L"▾" : L"▸");
    D2D1_RECT_F lr = sectionRect_;
    lr.left += S(2);
    Text(buf, lr, theme_.colors.textWeak, smallFormat_);

    Text(T(Str::Clear, lang_), clearRect_, theme_.colors.danger, smallFormat_);
}

void MainWindow::DrawTitleBar() {
    D2D1_RECT_F tr = D2D1::RectF(S(Theme::kPadX), 0, menuRect_.left - S(8), S(Theme::kTitleH));
    Text(L"X-TODO", tr, theme_.colors.textWeak, smallFormat_);

    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    {
        float mcx = (menuRect_.left + menuRect_.right) / 2;
        float mcy = (menuRect_.top + menuRect_.bottom) / 2;
        for (int i = -1; i <= 1; i++) {
            float yy = mcy + i * S(4);
            rt_->DrawLine(D2D1::Point2F(mcx - S(6), yy), D2D1::Point2F(mcx + S(6), yy), brush_, S(1.5f));
        }
    }

    D2D1_POINT_2F tc = D2D1::Point2F((themeRect_.left + themeRect_.right) / 2,
                                     (themeRect_.top + themeRect_.bottom) / 2);
    D2D1_ELLIPSE palette = D2D1::Ellipse(D2D1::Point2F(tc.x - S(0.4f), tc.y), S(6.4f), S(5.4f));
    brush_->SetColor(Theme::D2DColor(theme_.colors.paperElevated));
    rt_->FillEllipse(palette, brush_);
    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    rt_->DrawEllipse(palette, brush_, S(1.2f));

    auto swatch = [&](float dx, float dy, float radius, uint32_t color) {
        brush_->SetColor(Theme::D2DColor(color));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tc.x + S(dx), tc.y + S(dy)),
                                       S(radius), S(radius)), brush_);
    };
    swatch(-3.1f, -1.7f, 1.15f, theme_.colors.checkFill);
    swatch(-0.6f, -2.5f, 1.05f, theme_.colors.danger);
    swatch(-2.2f,  1.3f, 1.05f, theme_.colors.handleHover);

    D2D1_ELLIPSE hole = D2D1::Ellipse(D2D1::Point2F(tc.x + S(3.0f), tc.y + S(1.4f)), S(1.55f), S(1.35f));
    brush_->SetColor(Theme::D2DColor(theme_.colors.paper));
    rt_->FillEllipse(hole, brush_);
    brush_->SetColor(Theme::D2DColor(theme_.colors.paperEdge));
    rt_->DrawEllipse(hole, brush_, S(0.9f));

    D2D1_POINT_2F pc = D2D1::Point2F((pinRect_.left + pinRect_.right) / 2,
                                     (pinRect_.top + pinRect_.bottom) / 2);
    D2D1_ELLIPSE pe = D2D1::Ellipse(pc, S(5), S(5));
    if (ui_.alwaysOnTop) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
        rt_->FillEllipse(pe, brush_);
    } else {
        brush_->SetColor(Theme::D2DColor(theme_.colors.handle));
        rt_->DrawEllipse(pe, brush_, S(1.5f));
    }

    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    D2D1_RECT_F c = closeRect_;
    float p = S(7);
    rt_->DrawLine(D2D1::Point2F(c.left + p, c.top + p), D2D1::Point2F(c.right - p, c.bottom - p), brush_, S(1.6f));
    rt_->DrawLine(D2D1::Point2F(c.right - p, c.top + p), D2D1::Point2F(c.left + p, c.bottom - p), brush_, S(1.6f));
}

void MainWindow::DrawListTabs() {
    const float tabTop = S(Theme::kTitleH);
    const float tabBottom = ContentTop();
    FillRect(D2D1::RectF(0, tabTop, addListRect_.right + S(Theme::kPadX), tabBottom),
             theme_.colors.paper);

    const int current = model_.CurrentListIndex();
    for (const ListTabLayout& tab : listTabs_) {
        const TodoList* list = model_.ListAt(tab.listIndex);
        if (!list) continue;

        const bool selected = tab.listIndex == current;
        if (selected) {
            DrawSurfaceFrame(tab.rect, S(8), theme_.colors.paperElevated,
                             theme_.colors.paperEdge, S(1));
        }

        D2D1_RECT_F textR = tab.rect;
        textR.left += S(10);
        textR.right -= S(9);
        if (list->activeCount > 0) textR.right -= S(25);
        Text(list->title, textR, selected ? theme_.colors.text : theme_.colors.textWeak, smallFormat_);

        if (list->activeCount > 0) {
            wchar_t countBuf[16];
            swprintf_s(countBuf, L"%d", list->activeCount);
            const float pillW = S(15.0f + 6.0f * (float)wcslen(countBuf));
            const float pillH = S(16);
            D2D1_RECT_F pill = D2D1::RectF(tab.rect.right - S(8) - pillW,
                                           tab.rect.top + (tab.rect.bottom - tab.rect.top - pillH) / 2.0f,
                                           tab.rect.right - S(8),
                                           tab.rect.top + (tab.rect.bottom - tab.rect.top + pillH) / 2.0f);
            D2D1_ROUNDED_RECT prr{ pill, pillH / 2.0f, pillH / 2.0f };
            const uint32_t pillFill = selected
                ? Theme::Blend(theme_.colors.checkFill, theme_.colors.paperElevated, 0.16f)
                : Theme::Blend(theme_.colors.checkFill, theme_.colors.paper, 0.10f);
            brush_->SetColor(Theme::D2DColor(pillFill));
            rt_->FillRoundedRectangle(prr, brush_);
            brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt_->DrawTextW(countBuf, (UINT32)wcslen(countBuf), smallFormat_, pill, brush_,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    if (addListRect_.right > addListRect_.left) {
        DrawSurfaceFrame(addListRect_, S(8), theme_.colors.paperElevated,
                         theme_.colors.paperEdge, S(1));

        const float cx = (addListRect_.left + addListRect_.right) / 2.0f;
        const float cy = (addListRect_.top + addListRect_.bottom) / 2.0f;
        const float half = S(5);
        brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
        rt_->DrawLine(D2D1::Point2F(cx - half, cy), D2D1::Point2F(cx + half, cy), brush_, S(1.5f));
        rt_->DrawLine(D2D1::Point2F(cx, cy - half), D2D1::Point2F(cx, cy + half), brush_, S(1.5f));
    }
}

void MainWindow::DrawAddRow(bool hovered) {
    if (addRect_.bottom <= addRect_.top) return;
    if (hovered) FillRect(addRect_, theme_.colors.rowHover); // 最终消费色，不再混 alpha

    const float size = S(Theme::kCheckSize);
    const float cy = (addRect_.top + addRect_.bottom) / 2.0f;
    int level = 0;
    for (const RowLayout& r : rows_) {
        if (r.completed) break;
        level = ClampTodoLevel(model_.Items()[r.itemIndex].level);
    }
    const float left = addRect_.left + S(18) * (float)level;
    D2D1_RECT_F icon = D2D1::RectF(left, cy - size / 2.0f,
                                   left + size, cy + size / 2.0f);
    const float cx = (float)RoundToInt((icon.left + icon.right) * 0.5f) + 0.5f;
    const float iy = (icon.top + icon.bottom) * 0.5f;
    const float half = S(6);
    brush_->SetColor(Theme::D2DColor(hovered ? theme_.colors.checkFill : theme_.colors.handle));
    rt_->DrawLine(D2D1::Point2F(cx - half, iy), D2D1::Point2F(cx + half, iy), brush_, S(1.6f));
    rt_->DrawLine(D2D1::Point2F(cx, iy - half), D2D1::Point2F(cx, iy + half), brush_, S(1.6f));
}

bool MainWindow::Render() {
    if (capsuleShrunk() && capsuleStyle_ == CapsuleStyle::Dot)
        return RenderDotCapsuleLayered();

    if (!CreateDeviceResources()) return false;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);

    if (capsuleShrunk()) { // 折叠胶囊：按样式绘制
        rt_->BeginDraw();
        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        const int n = model_.TotalActiveCount();
        rt_->Clear(Theme::D2DColor(theme_.capsule.slimPaper));
        const float radius = W < H ? W * 0.5f : H * 0.5f;
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(0.75f, 0.75f, W - 0.75f, H - 0.75f), radius, radius };
        brush_->SetColor(Theme::D2DColor(theme_.capsule.slimEdge));
        rt_->DrawRoundedRectangle(rr, brush_, S(1.5f));
        wchar_t buf[16];
        swprintf_s(buf, L"%d", n);
        brush_->SetColor(Theme::D2DColor(n > 0 ? theme_.capsule.slimText : theme_.colors.textWeak));
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        rt_->DrawTextW(buf, (UINT32)wcslen(buf), smallFormat_, D2D1::RectF(0, 0, W, H), brush_);
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        if (rt_->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
            DiscardDeviceResources();
            return false;
        }
        return true;
    }

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(Theme::D2DColor(theme_.colors.paper));

    // 可滚动内容层
    D2D1_RECT_F vp = D2D1::RectF(0, ContentTop(), W, H - S(Theme::kFooterH));
    rt_->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, ContentTop() - scroll_));

    for (size_t i = 0; i < rows_.size(); i++)
        DrawRow(rows_[i], (int)i == hoverRow_);
    DrawAddRow(hoverRow_ == kHoverAddRow);
    DrawSection();

    if (dragging_ && dragInsert_ >= 0) {
        float yy = addRect_.top;
        for (const RowLayout& r : rows_) {
            if (!r.completed && r.itemIndex == dragInsert_) { yy = r.row.top; break; }
        }
        brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
        rt_->DrawLine(D2D1::Point2F(S(Theme::kPadX), yy),
                      D2D1::Point2F(W - S(Theme::kPadX), yy), brush_, S(2));
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();

    // 固定层
    DrawTitleBar();
    DrawListTabs();
    StrokeRect(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), theme_.colors.paperEdge, 1.0f);

    if (rt_->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return false;
    }
    return true;
}

bool MainWindow::RenderDotCapsuleLayered() {
    if (layeredMode_ != 2) UpdateLayeredState();

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return false;

    const int n = model_.TotalActiveCount();
    const uint32_t rgb = capsuleHover_
        ? (n > 0 ? theme_.capsule.dotActiveHover : theme_.capsule.dotIdleHover)
        : (n > 0 ? theme_.capsule.dotActive : theme_.capsule.dotIdle);
    const int r = (rgb >> 16) & 0xff;
    const int g = (rgb >> 8) & 0xff;
    const int b = rgb & 0xff;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(screen);
    if (!bmp || !mem || !bits) {
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        ReleaseDC(nullptr, screen);
        return false;
    }

    uint32_t* px = static_cast<uint32_t*>(bits);
    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double radius = (w < h ? w : h) * 0.5 - 0.5;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const double dx = (x + 0.5) - cx;
            const double dy = (y + 0.5) - cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            double cover = radius + 0.5 - dist;
            if (cover < 0.0) cover = 0.0;
            if (cover > 1.0) cover = 1.0;
            const int a = (int)(cover * 255.0 + 0.5);
            const int pr = (r * a + 127) / 255;
            const int pg = (g * a + 127) / 255;
            const int pb = (b * a + 127) / 255;
            px[y * w + x] = ((uint32_t)a << 24) | ((uint32_t)pr << 16) |
                            ((uint32_t)pg << 8) | (uint32_t)pb;
        }
    }

    HGDIOBJ old = SelectObject(mem, bmp);
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    POINT dst{ wr.left, wr.top };
    POINT src{ 0, 0 };
    SIZE size{ w, h };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL ok = UpdateLayeredWindow(hwnd_, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
    return ok != FALSE;
}

// ——————————————————————————— 鼠标 / 键盘 ———————————————————————————

void MainWindow::OnLButtonDown(float x, float y) {
    if (animActive_) return;                                     // 动画中忽略点击
    if (capsuleShrunk()) { BeginCapsulePress((int)x, (int)y); return; } // 区分点击展开 / 拖动吸附
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    pressHit_ = h;
    if (h.kind == HitKind::Handle) {
        dragging_   = true;
        dragFrom_   = h.itemIndex;
        dragY_      = y;
        dragInsert_ = h.itemIndex;
    }
}

void MainWindow::OnLButtonUp(float x, float y) {
    if (capsulePressing_) { FinishCapsulePress(); return; } // 胶囊按压：松手收尾（展开或吸附）
    if (dragging_) {
        dragging_ = false;
        if (dragInsert_ >= 0) {
            bool moved = model_.MoveActive(dragFrom_, dragInsert_);
            if (moved) {
                RebuildLayout();
                ScheduleSave();
            }
        }
        dragFrom_ = dragInsert_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    Hit h = HitTest(x, y);
    if (h.kind != pressHit_.kind || h.itemIndex != pressHit_.itemIndex) {
        pressHit_ = Hit{};
        return;
    }

    switch (h.kind) {
    case HitKind::TreeToggle:
        if (model_.ToggleCollapsed(h.itemIndex)) {
            RebuildLayout();
            ClampScroll();
            ScheduleSave();
        }
        break;
    case HitKind::Check:
        model_.SetDone(h.itemIndex, !model_.Items()[h.itemIndex].done);
        RebuildLayout();
        ClampScroll();
        RefreshTrayIcon();
        ScheduleSave();
        break;
    case HitKind::Text:
        BeginEdit(h.itemIndex);
        break;
    case HitKind::Delete: {
        int subtreeSize = model_.SubtreeEnd(h.itemIndex) - h.itemIndex;
        bool confirmed = false;
        if (subtreeSize > 1) {
            std::wstring msg = (lang_ == Lang::Zh)
                ? (L"删除这一项及其 " + std::to_wstring(subtreeSize - 1) + L" 个子项？")
                : (L"Delete this item and its " + std::to_wstring(subtreeSize - 1) + L" children?");
            confirmed = ConfirmText(msg, true);
        } else {
            confirmed = Confirm(Str::DeleteItemMsg, MB_ICONQUESTION);
        }
        if (confirmed)
            DeleteItem(h.itemIndex);
        break;
    }
    case HitKind::Section: ToggleCompletedExpanded(); break;
    case HitKind::Clear:   ClearCompletedConfirm();   break;
    case HitKind::ListTab: SwitchList(h.itemIndex);   break;
    case HitKind::AddList: CreateList();              break;
    case HitKind::Menu:    ShowTitleMenu();           break;
    case HitKind::Theme:   ShowThemeMenu();           break;
    case HitKind::Pin:     TogglePin();               break;
    case HitKind::Close:   HideToTray();              break;
    case HitKind::Add: {
        int level = 0;
        for (const RowLayout& r : rows_) {
            if (r.completed) break;
            level = ClampTodoLevel(model_.Items()[r.itemIndex].level);
        }
        int n = model_.AddActive(L"", level);
        RebuildLayout();
        ScrollItemIntoView(n);
        RefreshTrayIcon();
        BeginEdit(n);
        break;
    }
    default: break;
    }
    pressHit_ = Hit{};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnLButtonDoubleClick(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::ListTab) RenameList(h.itemIndex);
}

void MainWindow::OnRButtonUp(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::ListTab) ShowListTabMenu(h.itemIndex, x, y);
}

void MainWindow::OnMouseMove(float x, float y, bool lButton) {
    if (capsulePressing_) { UpdateCapsulePress(lButton); return; } // 胶囊按压：判定拖动并跟随
    if (dragging_) {
        dragY_ = y;
        float docY = y - ContentTop() + scroll_;
        int active = model_.ActiveCount();
        int insert = active;
        for (const RowLayout& r : rows_) {
            if (r.completed) break;
            if (docY < (r.row.top + r.row.bottom) / 2) { insert = r.itemIndex; break; }
        }
        int dragEnd = model_.SubtreeEnd(dragFrom_);
        if (insert > dragFrom_ && insert < dragEnd) insert = dragEnd;
        dragInsert_ = insert;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    Hit h = HitTest(x, y);
    int hover = (h.kind == HitKind::Add) ? kHoverAddRow : h.rowIndex;
    if (hover != hoverRow_) {
        hoverRow_ = hover;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseLeave() {
    if (hoverRow_ != -1) {
        hoverRow_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseWheel(int delta) {
    scroll_ -= (delta / 120.0f) * S(48);
    ClampScroll();
    if (editing()) LayoutEditBox();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT MainWindow::OnNcHitTest(int sx, int sy) {
    POINT p{ sx, sy };
    ScreenToClient(hwnd_, &p);
    RECT rc;
    GetClientRect(hwnd_, &rc);

    GuiHit::Input input;
    input.x = (float)p.x;
    input.y = (float)p.y;
    input.width = (float)(rc.right - rc.left);
    input.height = (float)(rc.bottom - rc.top);
    input.dpiScale = dpiScale();
    input.titleHeight = Theme::kTitleH;
    input.resizeEdge = Theme::kResizeEdge;
    input.forceClient = mountMode_ == MountMode::Capsule && (capsuleShrunk() || animActive_);
    input.capsuleMode = mountMode_ == MountMode::Capsule;
    input.menu = ToGuiRect(menuRect_);
    input.theme = ToGuiRect(themeRect_);
    input.pin = ToGuiRect(pinRect_);
    input.close = ToGuiRect(closeRect_);

    switch (GuiHit::HitTestNonClient(input)) {
    case GuiHit::NonClientHit::Client:      return HTCLIENT;
    case GuiHit::NonClientHit::Caption:     return HTCAPTION;
    case GuiHit::NonClientHit::Left:        return HTLEFT;
    case GuiHit::NonClientHit::Right:       return HTRIGHT;
    case GuiHit::NonClientHit::Top:         return HTTOP;
    case GuiHit::NonClientHit::Bottom:      return HTBOTTOM;
    case GuiHit::NonClientHit::TopLeft:     return HTTOPLEFT;
    case GuiHit::NonClientHit::TopRight:    return HTTOPRIGHT;
    case GuiHit::NonClientHit::BottomLeft:  return HTBOTTOMLEFT;
    case GuiHit::NonClientHit::BottomRight: return HTBOTTOMRIGHT;
    }
    return HTCLIENT;
}

// ——————————————————————————— 行内编辑 ———————————————————————————

void MainWindow::BeginEdit(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= model_.Count()) return;
    if (model_.Items()[itemIndex].done) return; // 已完成不可编辑
    if (editing()) CommitEdit(false);

    editIndex_ = itemIndex;

    if (!edit_) {
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL,
                                0, 0, 10, 10, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(edit_, EditProcStatic, 1, (DWORD_PTR)this);
    }

    if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
    LOGFONTW lf{};
    lf.lfHeight  = -(LONG)S(Theme::kFontSize);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
    editFont_ = CreateFontIndirectW(&lf);
    SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

    SetWindowTextW(edit_, model_.Items()[itemIndex].text.c_str());
    LayoutEditBox();
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    int n = GetWindowTextLengthW(edit_);
    SendMessageW(edit_, EM_SETSEL, n, n); // 光标置末尾
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CommitEdit(bool addNext) {
    if (!editing()) return;
    int idx = editIndex_;
    editIndex_ = -1;

    std::wstring text = ReadWindowText(edit_);
    ShowWindow(edit_, SW_HIDE);

    text = NormalizeTodoText(std::move(text));
    if (text.empty()) model_.Remove(idx);
    else              model_.SetText(idx, text);

    RebuildLayout();
    RefreshTrayIcon();
    ScheduleSave();

    if (addNext && !text.empty()) {
        int nextLevel = (idx >= 0 && idx < model_.Count()) ? model_.Items()[idx].level : 0;
        int insertAt = (idx >= 0 && idx < model_.ActiveCount()) ? model_.SubtreeEnd(idx) : model_.ActiveCount();
        int n = model_.InsertActive(insertAt, L"", nextLevel);
        RebuildLayout();
        ScrollItemIntoView(n);
        RefreshTrayIcon();
        BeginEdit(n);
    } else {
        ClampScroll();
        SetFocus(hwnd_);
        MaybeCollapseCapsule(); // 编辑结束，鼠标已在外则收回胶囊
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CancelEdit() {
    if (!editing()) return;
    int idx = editIndex_;
    editIndex_ = -1;
    ShowWindow(edit_, SW_HIDE);

    // 新建后未输入即取消的空项：移除
    if (idx >= 0 && idx < model_.Count() && model_.Items()[idx].text.empty())
        model_.Remove(idx);

    RebuildLayout();
    RefreshTrayIcon();
    ClampScroll();
    SetFocus(hwnd_);
    MaybeCollapseCapsule(); // 取消编辑后，鼠标已在外则收回胶囊
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::MaybeCollapseCapsule() {
    if (mountMode_ != MountMode::Capsule || !capsuleExpanded_ || animActive_ || editing()) return;
    POINT cur;
    GetCursorPos(&cur);
    RECT wr;
    if (!GetWindowRect(hwnd_, &wr)) return;
    if (!PtInRect(&wr, cur)) StartCapsuleAnim(false);
}

void MainWindow::LayoutEditBox() {
    if (!editing() || !edit_) return;
    float off = ContentTop() - scroll_;
    for (const RowLayout& r : rows_) {
        if (r.itemIndex != editIndex_ || r.completed) continue;

        const int rowTop = RoundToInt(r.row.top + off);
        const int rowH   = RoundToInt(r.row.bottom - r.row.top);
        const int left   = RoundToInt(r.text.left);
        const int width  = RoundToInt(r.text.right - r.text.left);
        if (rowH <= 0 || width <= 0) return;

        MoveWindow(edit_, left, rowTop, width, rowH, FALSE);
        RECT fmt{ 0, RoundToInt(S(5)), width, rowH };
        SendMessageW(edit_, EM_SETRECTNP, 0, (LPARAM)&fmt);
        MoveWindow(edit_, left, rowTop, width, rowH, TRUE);
        return;
    }
}

LRESULT CALLBACK MainWindow::EditProcStatic(HWND h, UINT m, WPARAM w, LPARAM l,
                                            UINT_PTR id, DWORD_PTR ref) {
    MainWindow* self = reinterpret_cast<MainWindow*>(ref);
    auto refresh = [&]() {
        self->RebuildLayout();
        self->ClampScroll();
        self->LayoutEditBox();
        InvalidateRect(self->hwnd_, nullptr, FALSE);
    };
    switch (m) {
    case WM_GETDLGCODE:
        return DefSubclassProc(h, m, w, l) | DLGC_WANTTAB;
    case WM_KEYDOWN: {
        GuiEdit::Key key = GuiEdit::Key::Other;
        if (w == VK_RETURN) key = GuiEdit::Key::Enter;
        else if (w == VK_ESCAPE) key = GuiEdit::Key::Escape;
        else if (w == VK_TAB) key = GuiEdit::Key::Tab;
        else if (w == VK_DELETE) key = GuiEdit::Key::DeleteKey;

        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const GuiEdit::Intent intent = GuiEdit::KeyDownIntent(key, shiftDown);
        switch (intent) {
        case GuiEdit::Intent::CommitAndAddNext:
            self->CommitEdit(true);
            return 0;
        case GuiEdit::Intent::Cancel:
            self->CancelEdit();
            return 0;
        case GuiEdit::Intent::Indent:
        case GuiEdit::Intent::Outdent: {
            const bool outdent = intent == GuiEdit::Intent::Outdent;
            bool changed = outdent
                ? self->model_.OutdentItem(self->editIndex_)
                : self->model_.IndentItemUnder(self->editIndex_,
                                               self->PreviousVisibleActiveItem(self->editIndex_));
            if (changed) {
                refresh();
                self->ScheduleSave();
            }
            return 0;
        }
        case GuiEdit::Intent::RefreshAfterDefault: {
            LRESULT r = DefSubclassProc(h, m, w, l);
            refresh();
            return r;
        }
        case GuiEdit::Intent::None:
            break;
        }
        break;
    }
    case WM_CHAR:
        if (GuiEdit::SuppressChar((wchar_t)w)) return 0; // 吞掉 Tab/回车/Esc 的字符，避免提示音
        {
            LRESULT r = DefSubclassProc(h, m, w, l);
            refresh();
            return r;
        }
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
    case WM_UNDO: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        refresh();
        return r;
    }
    case WM_KILLFOCUS:
        self->CommitEdit(false);
        break;
    }
    return DefSubclassProc(h, m, w, l); // 注意：仅 4 参，不传 id/ref
}
