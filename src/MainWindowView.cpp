#include "MainWindow.h"
#include "EditIntent.h"
#include "Theme.h"
#include "ViewLayout.h"
#include "WindowHitTest.h"

#include <commctrl.h>
#include <cmath>
#include <ctime>
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
constexpr int kHoverEmptyActive = -2;

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

std::string TodayDayKey() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  static_cast<int>(st.wYear), static_cast<int>(st.wMonth), static_cast<int>(st.wDay));
    return std::string(buf);
}

int CurrentMinuteOfDay() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return static_cast<int>(st.wHour) * 60 + static_cast<int>(st.wMinute);
}

bool ParseDayKey(const std::string& day, int& year, int& month, int& date) {
    if (!IsValidCalendarDayKey(day)) return false;
    year = (day[0] - '0') * 1000 + (day[1] - '0') * 100 + (day[2] - '0') * 10 + (day[3] - '0');
    month = (day[5] - '0') * 10 + (day[6] - '0');
    date = (day[8] - '0') * 10 + (day[9] - '0');
    return true;
}

std::string OffsetDayKey(const std::string& day, int deltaDays) {
    int year = 0, month = 0, date = 0;
    if (!ParseDayKey(day, year, month, date)) return TodayDayKey();
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = date + deltaDays;
    tm.tm_hour = 12;
    if (std::mktime(&tm) == static_cast<std::time_t>(-1)) return day;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

std::wstring WidenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring CalendarDayLabel(const std::string& day, Lang lang) {
    int year = 0, month = 0, date = 0;
    if (!ParseDayKey(day, year, month, date)) return WidenAscii(day);
    wchar_t buf[32];
    if (lang == Lang::Zh) {
        swprintf_s(buf, L"%04d年%02d月%02d日", year, month, date);
    } else {
        swprintf_s(buf, L"%04d-%02d-%02d", year, month, date);
    }
    return std::wstring(buf);
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
    calendarBlockRects_.clear();
    activeEndY_ = 0.0f;
    emptyActiveRect_ = D2D1::RectF(0, 0, 0, 0);
    addListRect_ = D2D1::RectF(0, 0, 0, 0);
    calendarTabRect_ = D2D1::RectF(0, 0, 0, 0);

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

    activeEndY_ = docY;
    if (active == 0) {
        emptyActiveRect_ = ToD2DRect(GuiLayout::ComputeEmptyActivePrompt(
            W, docY, ViewportHeight(), total == 0, dpiScale()));
        docY = emptyActiveRect_.bottom;
    }

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
    tabMetrics.reserve((size_t)model_.ListCount() + 1);
    tabMetrics.push_back(GuiLayout::TabMetric{ -1, std::wcslen(T(Str::Calendar, lang_)), 0, GuiLayout::TabKind::Calendar });
    for (int i = 0; i < model_.ListCount(); ++i) {
        const TodoList* list = model_.ListAt(i);
        if (!list) continue;
        tabMetrics.push_back(GuiLayout::TabMetric{ i, list->title.size(), list->activeCount });
    }
    const GuiLayout::TabStrip tabStrip = GuiLayout::ComputeTabStrip(W, dpiScale(), tabMetrics);
    addListRect_ = ToD2DRect(tabStrip.addList);
    for (const GuiLayout::TabRect& tab : tabStrip.tabs) {
        if (tab.kind == GuiLayout::TabKind::Calendar) {
            calendarTabRect_ = ToD2DRect(tab.rect);
        } else {
            listTabs_.push_back(ListTabLayout{ tab.listIndex, ToD2DRect(tab.rect) });
        }
    }

    calendarFrame_ = GuiCalendar::ComputeFrame(W, ViewportHeight(), dpiScale());
    BuildCalendarBlockRects();
    if (calendarActive() && !calendarScrollInitialized_) AlignCalendarScrollToNow(false);
    ClampCalendarScroll();
    if (calendarEditing()) LayoutCalendarEditControls();
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
        tabs.reserve(listTabs_.size() + 1);
        if (calendarTabRect_.right > calendarTabRect_.left)
            tabs.push_back(GuiLayout::TabRect{ -1, GuiLayout::TabKind::Calendar, ToGuiRect(calendarTabRect_) });
        for (const ListTabLayout& tab : listTabs_)
            tabs.push_back(GuiLayout::TabRect{ tab.listIndex, GuiLayout::TabKind::List, ToGuiRect(tab.rect) });

        const GuiLayout::ChromeHitResult chrome =
            GuiLayout::HitTestChrome(x, y, dpiScale(), title, ToGuiRect(addListRect_), tabs);
        switch (chrome.kind) {
        case GuiLayout::ChromeHit::Menu:    h.kind = HitKind::Menu;    return h;
        case GuiLayout::ChromeHit::Theme:   h.kind = HitKind::Theme;   return h;
        case GuiLayout::ChromeHit::Pin:     h.kind = HitKind::Pin;     return h;
        case GuiLayout::ChromeHit::Close:   h.kind = HitKind::Close;   return h;
        case GuiLayout::ChromeHit::AddList: h.kind = HitKind::AddList; return h;
        case GuiLayout::ChromeHit::CalendarTab:
            h.kind = HitKind::CalendarTab;
            return h;
        case GuiLayout::ChromeHit::ListTab:
            h.kind = HitKind::ListTab;
            h.itemIndex = chrome.listIndex;
            return h;
        case GuiLayout::ChromeHit::None:
            return h; // 标题栏空白交给 NCHITTEST 拖动；标签栏空白无动作
        }
    }

    if (calendarActive()) {
        const float localY = y - ContentTop();
        const GuiCalendar::HitResult calendarHit =
            GuiCalendar::HitTest(x, localY, calendarScroll_, dpiScale(), calendarFrame_, calendarBlockRects_);
        switch (calendarHit.kind) {
        case GuiCalendar::HitKind::PrevDay:
            h.kind = HitKind::CalendarPrevDay;
            return h;
        case GuiCalendar::HitKind::NextDay:
            h.kind = HitKind::CalendarNextDay;
            return h;
        case GuiCalendar::HitKind::EmptyTimeline:
            h.kind = HitKind::CalendarEmptyTimeline;
            return h;
        case GuiCalendar::HitKind::BlockBody:
            h.kind = HitKind::CalendarBlock;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::ResizeStart:
            h.kind = HitKind::CalendarResizeStart;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::ResizeEnd:
            h.kind = HitKind::CalendarResizeEnd;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::None:
            return h;
        }
    }

    float docY = y - ContentTop() + scroll_;
    D2D1_POINT_2F dp{ x, docY };

    if (emptyActiveRect_.bottom > emptyActiveRect_.top && InRect(emptyActiveRect_, dp)) {
        h.kind = HitKind::EmptyActive;
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
    if (calendarTabRect_.right > calendarTabRect_.left) {
        const bool selected = calendarActive();
        if (selected) {
            DrawSurfaceFrame(calendarTabRect_, S(8), theme_.colors.paperElevated,
                             theme_.colors.paperEdge, S(1));
        }
        D2D1_RECT_F textR = calendarTabRect_;
        textR.left += S(10);
        textR.right -= S(9);
        Text(T(Str::Calendar, lang_), textR,
             selected ? theme_.colors.text : theme_.colors.textWeak, smallFormat_);
    }

    for (const ListTabLayout& tab : listTabs_) {
        const TodoList* list = model_.ListAt(tab.listIndex);
        if (!list) continue;

        const bool selected = !calendarActive() && tab.listIndex == current;
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

void MainWindow::DrawEmptyActivePrompt(bool hovered) {
    if (emptyActiveRect_.bottom <= emptyActiveRect_.top) return;

    const bool emptyList = model_.Count() == 0;
    if (emptyList) {
        const float radius = S(8);
        DrawSurfaceFrame(emptyActiveRect_, radius,
                         hovered ? theme_.colors.rowHover : theme_.colors.paper,
                         hovered ? theme_.colors.focusRing : theme_.colors.paperEdge,
                         hovered ? S(1.2f) : S(1.0f));

        D2D1_RECT_F titleRect = emptyActiveRect_;
        titleRect.bottom = (emptyActiveRect_.top + emptyActiveRect_.bottom) / 2.0f;
        titleRect.top += S(12);
        Text(T(Str::EmptyListTitle, lang_), titleRect, theme_.colors.text, textFormat_);

        D2D1_RECT_F promptRect = emptyActiveRect_;
        promptRect.top = titleRect.bottom - S(2);
        promptRect.bottom -= S(12);
        Text(T(Str::EmptyActivePrompt, lang_), promptRect,
             hovered ? theme_.colors.checkFill : theme_.colors.textWeak, smallFormat_);
        return;
    }

    if (hovered) FillRect(emptyActiveRect_, theme_.colors.rowHover);
    Text(T(Str::EmptyActivePrompt, lang_), emptyActiveRect_,
         hovered ? theme_.colors.checkFill : theme_.colors.textWeak, smallFormat_);
}

void MainWindow::DrawCalendarView(float W, float H) {
    const float top = ContentTop();
    const GuiCalendar::Frame& frame = calendarFrame_;
    const uint32_t subtle = Theme::Blend(theme_.colors.checkFill, theme_.colors.paper, 0.07f);
    const uint32_t blockFill = Theme::Blend(theme_.colors.checkFill, theme_.colors.paperElevated, 0.14f);
    const uint32_t selectedFill = Theme::Blend(theme_.colors.checkFill, theme_.colors.paperElevated, 0.20f);
    const uint32_t blockEdge = Theme::Blend(theme_.colors.checkFill, theme_.colors.paperEdge, 0.45f);

    D2D1_RECT_F dateR = ToD2DRect(frame.dateHeader);
    dateR.top += top;
    dateR.bottom += top;
    FillRect(dateR, theme_.colors.paper);
    Text(CalendarDayLabel(calendarDay_, lang_), dateR, theme_.colors.text, textFormat_);

    D2D1_RECT_F prevR = ToD2DRect(frame.prevDay);
    prevR.top += top;
    prevR.bottom += top;
    D2D1_RECT_F nextR = ToD2DRect(frame.nextDay);
    nextR.top += top;
    nextR.bottom += top;
    DrawSurfaceFrame(prevR, S(7), theme_.colors.paperElevated, theme_.colors.paperEdge, S(1));
    DrawSurfaceFrame(nextR, S(7), theme_.colors.paperElevated, theme_.colors.paperEdge, S(1));
    Text(L"<", prevR, theme_.colors.textWeak, smallFormat_);
    Text(L">", nextR, theme_.colors.textWeak, smallFormat_);

    D2D1_RECT_F allDay = ToD2DRect(frame.allDay);
    allDay.top += top;
    allDay.bottom += top;
    DrawSurfaceFrame(allDay, S(7), theme_.colors.paperElevated, theme_.colors.paperEdge, S(1));
    D2D1_RECT_F allDayLabel = allDay;
    allDayLabel.left += S(10);
    allDayLabel.right = allDayLabel.left + S(54);
    Text(T(Str::AllDay, lang_), allDayLabel, theme_.colors.textWeak, smallFormat_);

    const D2D1_RECT_F clip = D2D1::RectF(0, top + frame.timelineViewport.top, W, H - S(Theme::kFooterH));
    rt_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, top + frame.timelineViewport.top - calendarScroll_));

    for (int hour = 0; hour <= 24; ++hour) {
        const float y = (static_cast<float>(hour) / 24.0f) * frame.contentHeight;
        brush_->SetColor(Theme::D2DColor(hour == 0 ? theme_.colors.divider : subtle));
        rt_->DrawLine(D2D1::Point2F(frame.lane.left, y),
                      D2D1::Point2F(frame.lane.right, y), brush_, S(hour % 6 == 0 ? 1.2f : 1.0f));
        if (hour < 24) {
            wchar_t buf[8];
            swprintf_s(buf, L"%02d:00", hour);
            D2D1_RECT_F label = D2D1::RectF(S(7), y - S(8), frame.gutter.right - S(7), y + S(12));
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(buf, label, theme_.colors.textWeak, smallFormat_);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    const bool today = calendarDay_ == TodayDayKey();
    if (today) {
        const float y = (static_cast<float>(CurrentMinuteOfDay()) / 1440.0f) * frame.contentHeight;
        brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
        rt_->DrawLine(D2D1::Point2F(frame.lane.left, y),
                      D2D1::Point2F(frame.lane.right, y), brush_, S(1.4f));
    }

    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        const CalendarBlock* block = calendar_.FindBlock(blockRect.blockId);
        if (!block) continue;
        const bool selected = block->id == calendarEditId_;
        D2D1_RECT_F r = ToD2DRect(blockRect.rect);
        const float radius = S(7);
        DrawSurfaceFrame(r, radius, selected ? selectedFill : blockFill,
                         selected ? theme_.colors.focusRing : blockEdge, S(selected ? 1.2f : 1.0f));

        D2D1_RECT_F timeR = r;
        timeR.left += S(8);
        timeR.right -= S(8);
        timeR.top += S(4);
        timeR.bottom = timeR.top + S(16);
        std::wstring timeText = GuiCalendar::FormatTimeText(block->startMinute) + L" - " +
                                GuiCalendar::FormatTimeText(block->endMinute);
        Text(timeText, timeR, theme_.colors.textWeak, smallFormat_);

        D2D1_RECT_F titleR = r;
        titleR.left += S(8);
        titleR.right -= S(8);
        titleR.top += S(20);
        titleR.bottom -= S(4);
        Text(block->title.empty() ? std::wstring(L"") : block->title,
             titleR, theme_.colors.text, smallFormat_);
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();
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

    if (calendarActive()) {
        DrawCalendarView(W, H);
    } else {
        D2D1_RECT_F vp = D2D1::RectF(0, ContentTop(), W, H - S(Theme::kFooterH));
        rt_->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
        rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, ContentTop() - scroll_));

        DrawEmptyActivePrompt(hoverRow_ == kHoverEmptyActive);
        for (size_t i = 0; i < rows_.size(); i++)
            DrawRow(rows_[i], (int)i == hoverRow_);
        DrawSection();

        if (dragging_ && dragInsert_ >= 0) {
            float yy = activeEndY_;
            for (const RowLayout& r : rows_) {
                if (!r.completed && r.itemIndex == dragInsert_) { yy = r.row.top; break; }
            }
            brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
            rt_->DrawLine(D2D1::Point2F(S(Theme::kPadX), yy),
                          D2D1::Point2F(W - S(Theme::kPadX), yy), brush_, S(2));
        }

        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        rt_->PopAxisAlignedClip();
    }

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
    if (calendarActive()) {
        if (calendarEditing()) {
            const bool sameBlock =
                (h.kind == HitKind::CalendarBlock || h.kind == HitKind::CalendarResizeStart ||
                 h.kind == HitKind::CalendarResizeEnd) && h.itemIndex == calendarEditId_;
            if (!sameBlock) EndCalendarEdit(true);
        }
        pressHit_ = h;
        if (h.kind == HitKind::CalendarEmptyTimeline) {
            calendarDrag_.mode = CalendarDragMode::PendingCreate;
            calendarDrag_.startX = x;
            calendarDrag_.startY = y;
            calendarDrag_.anchorMinute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
            return;
        }
        if (h.kind == HitKind::CalendarBlock || h.kind == HitKind::CalendarResizeStart ||
            h.kind == HitKind::CalendarResizeEnd) {
            const CalendarBlock* block = calendar_.FindBlock(h.itemIndex);
            if (block) {
                calendarDrag_.mode = CalendarDragMode::PendingBlock;
                calendarDrag_.blockId = block->id;
                calendarDrag_.startX = x;
                calendarDrag_.startY = y;
                calendarDrag_.anchorMinute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
                calendarDrag_.originalStart = block->startMinute;
                calendarDrag_.originalEnd = block->endMinute;
            }
        }
        return;
    }
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
    if (calendarActive() && calendarDrag_.mode != CalendarDragMode::None) {
        const CalendarDragMode mode = calendarDrag_.mode;
        const int blockId = calendarDrag_.blockId;
        ResetCalendarDrag();
        if (mode == CalendarDragMode::Creating && calendar_.FindBlock(blockId)) {
            BuildCalendarBlockRects();
            BeginCalendarEdit(blockId, true);
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if ((mode == CalendarDragMode::Moving || mode == CalendarDragMode::ResizingStart ||
             mode == CalendarDragMode::ResizingEnd) && calendar_.FindBlock(blockId)) {
            if (calendarEditId_ == blockId) SyncCalendarEditors();
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (mode == CalendarDragMode::PendingBlock && calendar_.FindBlock(blockId)) {
            BeginCalendarEdit(blockId, true);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
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
    case HitKind::CalendarTab: SetActiveView(MainView::Calendar); break;
    case HitKind::CalendarPrevDay: SwitchCalendarDay(-1); break;
    case HitKind::CalendarNextDay: SwitchCalendarDay(1);  break;
    case HitKind::CalendarBlock:
    case HitKind::CalendarResizeStart:
    case HitKind::CalendarResizeEnd:
        BeginCalendarEdit(h.itemIndex, true);
        break;
    case HitKind::EmptyActive:
        CreateEmptyActiveItem();
        break;
    case HitKind::Menu:    ShowTitleMenu();           break;
    case HitKind::Theme:   ShowThemeMenu();           break;
    case HitKind::Pin:     TogglePin();               break;
    case HitKind::Close:   HideToTray();              break;
    default: break;
    }
    pressHit_ = Hit{};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnLButtonDoubleClick(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::CalendarTab) { SetActiveView(MainView::Calendar); return; }
    if (h.kind == HitKind::ListTab) RenameList(h.itemIndex);
}

void MainWindow::OnRButtonUp(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::CalendarTab) { SetActiveView(MainView::Calendar); return; }
    if (h.kind == HitKind::ListTab) ShowListTabMenu(h.itemIndex, x, y);
}

void MainWindow::OnMouseMove(float x, float y, bool lButton) {
    if (capsulePressing_) { UpdateCapsulePress(lButton); return; } // 胶囊按压：判定拖动并跟随
    if (calendarActive() && calendarDrag_.mode != CalendarDragMode::None) {
        if (!lButton) { ResetCalendarDrag(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        const int minute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
        if (calendarDrag_.mode == CalendarDragMode::PendingCreate) {
            if (!GuiCalendar::DragExceeded(calendarDrag_.startX, calendarDrag_.startY, x, y, dpiScale()))
                return;
            const GuiCalendar::TimeRange range = GuiCalendar::RangeFromDrag(calendarDrag_.anchorMinute, minute);
            const int id = calendar_.AddBlock(calendarDay_, range.startMinute, range.endMinute, L"");
            if (id > 0) {
                calendarDrag_.mode = CalendarDragMode::Creating;
                calendarDrag_.blockId = id;
                BuildCalendarBlockRects();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }

        if (calendarDrag_.mode == CalendarDragMode::PendingBlock) {
            if (!GuiCalendar::DragExceeded(calendarDrag_.startX, calendarDrag_.startY, x, y, dpiScale()))
                return;
            if (pressHit_.kind == HitKind::CalendarResizeStart)
                calendarDrag_.mode = CalendarDragMode::ResizingStart;
            else if (pressHit_.kind == HitKind::CalendarResizeEnd)
                calendarDrag_.mode = CalendarDragMode::ResizingEnd;
            else
                calendarDrag_.mode = CalendarDragMode::Moving;
        }

        bool changed = false;
        if (calendarDrag_.mode == CalendarDragMode::Creating) {
            const GuiCalendar::TimeRange range = GuiCalendar::RangeFromDrag(calendarDrag_.anchorMinute, minute);
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, range.startMinute, range.endMinute);
        } else if (calendarDrag_.mode == CalendarDragMode::Moving) {
            const int duration = calendarDrag_.originalEnd - calendarDrag_.originalStart;
            int delta = GuiCalendar::SnapMinute(minute) - GuiCalendar::SnapMinute(calendarDrag_.anchorMinute);
            int start = calendarDrag_.originalStart + delta;
            if (start < 0) start = 0;
            if (start + duration > 1440) start = 1440 - duration;
            if (start < 0) start = 0;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, start, start + duration);
        } else if (calendarDrag_.mode == CalendarDragMode::ResizingStart) {
            int start = GuiCalendar::SnapMinute(minute);
            if (start > calendarDrag_.originalEnd - 15) start = calendarDrag_.originalEnd - 15;
            if (start < 0) start = 0;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, start, calendarDrag_.originalEnd);
        } else if (calendarDrag_.mode == CalendarDragMode::ResizingEnd) {
            int end = GuiCalendar::SnapMinute(minute);
            if (end < calendarDrag_.originalStart + 15) end = calendarDrag_.originalStart + 15;
            if (end > 1440) end = 1440;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, calendarDrag_.originalStart, end);
        }
        if (changed) {
            BuildCalendarBlockRects();
            if (calendarEditId_ == calendarDrag_.blockId) LayoutCalendarEditControls();
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
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
    int hover = (h.kind == HitKind::EmptyActive) ? kHoverEmptyActive : h.rowIndex;
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
    if (calendarActive()) {
        calendarScroll_ -= (delta / 120.0f) * S(96);
        ClampCalendarScroll();
        if (calendarEditing()) LayoutCalendarEditControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
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

// ——————————————————————————— 日历视图 / 编辑 ———————————————————————————

void MainWindow::EnsureCalendarDay() {
    if (!calendarDay_.empty() && IsValidCalendarDayKey(calendarDay_)) return;
    if (IsValidCalendarDayKey(ui_.calendarDay)) calendarDay_ = ui_.calendarDay;
    else calendarDay_ = TodayDayKey();
    ui_.calendarDay = calendarDay_;
}

void MainWindow::SetActiveView(MainView view) {
    if (view == activeView_) return;
    if (editing()) CommitEdit(false);
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    activeView_ = view;
    ui_.activeView = view == MainView::Calendar ? "calendar" : "list";
    hoverRow_ = -1;
    dragging_ = false;
    dragFrom_ = dragInsert_ = -1;
    if (calendarActive()) {
        EnsureCalendarDay();
        calendarScrollInitialized_ = false;
    }
    RebuildLayout();
    if (calendarActive()) AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SwitchCalendarDay(int deltaDays) {
    if (deltaDays == 0) return;
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    EnsureCalendarDay();
    calendarDay_ = OffsetCalendarDayKey(deltaDays);
    ui_.calendarDay = calendarDay_;
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::string MainWindow::OffsetCalendarDayKey(int deltaDays) const {
    return OffsetDayKey(calendarDay_.empty() ? TodayDayKey() : calendarDay_, deltaDays);
}

void MainWindow::ClampCalendarScroll() {
    float maxScroll = calendarFrame_.contentHeight - calendarFrame_.timelineViewport.Height();
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (calendarScroll_ < 0.0f) calendarScroll_ = 0.0f;
    if (calendarScroll_ > maxScroll) calendarScroll_ = maxScroll;
}

void MainWindow::AlignCalendarScrollToNow(bool force) {
    if (!force && calendarScrollInitialized_) return;
    EnsureCalendarDay();
    const int minute = (calendarDay_ == TodayDayKey()) ? CurrentMinuteOfDay() : 8 * 60;
    calendarScroll_ = GuiCalendar::ScrollForMinute(minute, ViewportHeight(), calendarFrame_);
    calendarScrollInitialized_ = true;
    ClampCalendarScroll();
}

void MainWindow::BuildCalendarBlockRects() {
    calendarBlockRects_.clear();
    EnsureCalendarDay();
    for (const CalendarBlock* block : calendar_.BlocksForDay(calendarDay_)) {
        if (!block) continue;
        calendarBlockRects_.push_back(GuiCalendar::BlockRect{
            block->id,
            GuiCalendar::ComputeBlockRect(calendarFrame_, block->id, block->startMinute, block->endMinute)
        });
    }
}

void MainWindow::EnsureCalendarEditors() {
    if (!editFont_) {
        LOGFONTW lf{};
        lf.lfHeight  = -(LONG)S(Theme::kFontSize);
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
        editFont_ = CreateFontIndirectW(&lf);
    }
    auto createEdit = [&](HWND& edit, DWORD style) {
        if (edit) return;
        edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style,
                               0, 0, 10, 10, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(edit, CalendarEditProcStatic, 2, (DWORD_PTR)this);
        SendMessageW(edit, WM_SETFONT, (WPARAM)editFont_, TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(RoundToInt(S(3)), RoundToInt(S(3))));
    };
    createEdit(calendarTitleEdit_, ES_AUTOHSCROLL);
    createEdit(calendarStartEdit_, ES_AUTOHSCROLL | ES_CENTER);
    createEdit(calendarEndEdit_, ES_AUTOHSCROLL | ES_CENTER);
}

void MainWindow::BeginCalendarEdit(int blockId, bool selectTitle) {
    const CalendarBlock* block = calendar_.FindBlock(blockId);
    if (!block) return;
    if (editing()) CommitEdit(false);
    EnsureCalendarEditors();
    calendarEditId_ = blockId;
    SyncCalendarEditors();
    LayoutCalendarEditControls();
    ShowWindow(calendarTitleEdit_, SW_SHOW);
    ShowWindow(calendarStartEdit_, SW_SHOW);
    ShowWindow(calendarEndEdit_, SW_SHOW);
    SetFocus(calendarTitleEdit_);
    if (selectTitle) {
        int len = GetWindowTextLengthW(calendarTitleEdit_);
        SendMessageW(calendarTitleEdit_, EM_SETSEL, 0, len);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::EndCalendarEdit(bool removeEmpty) {
    if (!calendarEditing()) return;
    const int blockId = calendarEditId_;
    if (calendarTitleEdit_) calendar_.SetBlockTitle(blockId, ReadWindowText(calendarTitleEdit_));
    CommitCalendarTimeEdit(calendarStartEdit_, true);
    CommitCalendarTimeEdit(calendarEndEdit_, true);
    calendarEditId_ = -1;
    HideCalendarEditors();
    if (removeEmpty && CalendarBlockTitleEmpty(blockId)) {
        calendar_.RemoveBlock(blockId);
    }
    BuildCalendarBlockRects();
    ScheduleSave();
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HideCalendarEditors() {
    if (calendarTitleEdit_) ShowWindow(calendarTitleEdit_, SW_HIDE);
    if (calendarStartEdit_) ShowWindow(calendarStartEdit_, SW_HIDE);
    if (calendarEndEdit_) ShowWindow(calendarEndEdit_, SW_HIDE);
}

void MainWindow::SyncCalendarEditors() {
    if (!calendarEditing()) return;
    const CalendarBlock* block = calendar_.FindBlock(calendarEditId_);
    if (!block) return;
    EnsureCalendarEditors();
    calendarSyncing_ = true;
    SetWindowTextW(calendarTitleEdit_, block->title.c_str());
    SetWindowTextW(calendarStartEdit_, GuiCalendar::FormatTimeText(block->startMinute).c_str());
    SetWindowTextW(calendarEndEdit_, GuiCalendar::FormatTimeText(block->endMinute).c_str());
    calendarSyncing_ = false;
}

void MainWindow::LayoutCalendarEditControls() {
    if (!calendarEditing() || !calendarTitleEdit_) return;
    const CalendarBlock* block = calendar_.FindBlock(calendarEditId_);
    if (!block) { EndCalendarEdit(false); return; }

    Gui::Rect rect{};
    bool found = false;
    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        if (blockRect.blockId == calendarEditId_) {
            rect = blockRect.rect;
            found = true;
            break;
        }
    }
    if (!found) { HideCalendarEditors(); return; }

    const float clientTop = ContentTop() + calendarFrame_.timelineViewport.top - calendarScroll_;
    const int left = RoundToInt(rect.left + S(8));
    const int top = RoundToInt(clientTop + rect.top + S(4));
    const int width = RoundToInt(rect.right - rect.left - S(16));
    const int timeW = RoundToInt(S(54));
    const int timeH = RoundToInt(S(18));
    const int gap = RoundToInt(S(5));
    const int titleTop = top + timeH + RoundToInt(S(3));
    int titleH = RoundToInt(rect.bottom - rect.top - S(28));
    if (titleH < RoundToInt(S(18))) titleH = RoundToInt(S(18));

    const int viewportTop = RoundToInt(ContentTop() + calendarFrame_.timelineViewport.top);
    const int viewportBottom = RoundToInt(ContentTop() + calendarFrame_.timelineViewport.bottom);
    if (top >= viewportBottom || titleTop + titleH <= viewportTop || width <= 20) {
        HideCalendarEditors();
        return;
    }

    MoveWindow(calendarStartEdit_, left, top, timeW, timeH, TRUE);
    MoveWindow(calendarEndEdit_, left + timeW + gap, top, timeW, timeH, TRUE);
    MoveWindow(calendarTitleEdit_, left, titleTop, width, titleH, TRUE);
    ShowWindow(calendarStartEdit_, SW_SHOW);
    ShowWindow(calendarEndEdit_, SW_SHOW);
    ShowWindow(calendarTitleEdit_, SW_SHOW);
}

void MainWindow::OnCalendarEditChanged(HWND edit) {
    if (calendarSyncing_ || !calendarEditing()) return;
    if (edit == calendarTitleEdit_) {
        calendar_.SetBlockTitle(calendarEditId_, ReadWindowText(calendarTitleEdit_));
        ScheduleSave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (edit == calendarStartEdit_ || edit == calendarEndEdit_) {
        CommitCalendarTimeEdit(edit, false);
    }
}

void MainWindow::CommitCalendarTimeEdit(HWND edit, bool syncText) {
    if (!calendarEditing() || !edit) return;
    CalendarBlock* block = calendar_.FindBlock(calendarEditId_);
    if (!block) return;
    int minute = 0;
    if (!GuiCalendar::ParseTimeText(ReadWindowText(edit), minute)) return;

    int start = block->startMinute;
    int end = block->endMinute;
    if (edit == calendarStartEdit_) {
        int duration = end - start;
        if (duration < 1) duration = 1;
        start = minute;
        end = start + duration;
        if (end > 1440) {
            end = 1440;
            start = end - duration;
        }
        if (start < 0) start = 0;
    } else if (edit == calendarEndEdit_) {
        end = minute;
        if (end <= start) end = start + 1;
        if (end > 1440) end = 1440;
    }

    if (calendar_.SetBlockRange(calendarEditId_, start, end)) {
        BuildCalendarBlockRects();
        LayoutCalendarEditControls();
        if (syncText) SyncCalendarEditors();
        ScheduleSave();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool MainWindow::CalendarBlockTitleEmpty(int blockId) const {
    const CalendarBlock* block = calendar_.FindBlock(blockId);
    if (!block) return true;
    return Trim(block->title).empty();
}

void MainWindow::ResetCalendarDrag() {
    calendarDrag_ = CalendarDragState{};
}

void MainWindow::CancelCalendarCapture() {
    if (calendarDrag_.mode == CalendarDragMode::Creating && CalendarBlockTitleEmpty(calendarDrag_.blockId)) {
        calendar_.RemoveBlock(calendarDrag_.blockId);
        BuildCalendarBlockRects();
    }
    ResetCalendarDrag();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK MainWindow::CalendarEditProcStatic(HWND h, UINT m, WPARAM w, LPARAM l,
                                                    UINT_PTR id, DWORD_PTR ref) {
    (void)id;
    MainWindow* self = reinterpret_cast<MainWindow*>(ref);
    switch (m) {
    case WM_GETDLGCODE:
        return DefSubclassProc(h, m, w, l) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            self->EndCalendarEdit(true);
            return 0;
        }
        if (w == VK_RETURN) {
            if (h == self->calendarStartEdit_ || h == self->calendarEndEdit_)
                self->CommitCalendarTimeEdit(h, true);
            self->EndCalendarEdit(true);
            return 0;
        }
        break;
    case WM_CHAR:
        if (w == L'\r' || w == 0x1B) return 0;
        break;
    case WM_KILLFOCUS: {
        HWND next = reinterpret_cast<HWND>(w);
        if (h == self->calendarStartEdit_ || h == self->calendarEndEdit_)
            self->CommitCalendarTimeEdit(h, true);
        if (next == self->calendarTitleEdit_ || next == self->calendarStartEdit_ || next == self->calendarEndEdit_)
            break;
        self->EndCalendarEdit(true);
        return 0;
    }
    }
    return DefSubclassProc(h, m, w, l);
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
    if (mountMode_ != MountMode::Capsule || !capsuleExpanded_ || animActive_ ||
        editing() || calendarEditing()) return;
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
