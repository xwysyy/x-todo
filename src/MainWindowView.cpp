#include "MainWindow.h"
#include "Theme.h"

#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <string>

namespace {
bool InRect(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

// ——————————————————————————— 布局 ———————————————————————————

float MainWindow::ContentTop() const { return S(Theme::kTitleH); }
float MainWindow::ContentHeight() const { return contentH_; }

float MainWindow::ViewportHeight() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float h = (float)(rc.bottom - rc.top) - S(Theme::kTitleH) - S(Theme::kFooterH);
    return h < 0 ? 0 : h;
}

void MainWindow::ClampScroll() {
    float maxScroll = contentH_ - ViewportHeight();
    if (maxScroll < 0) maxScroll = 0;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > maxScroll) scroll_ = maxScroll;
}

void MainWindow::RebuildLayout() {
    for (auto& r : rows_) // 释放上一轮缓存的删除线布局，避免泄漏
        if (r.strikeLayout) { r.strikeLayout->Release(); r.strikeLayout = nullptr; }
    rows_.clear();

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left);

    const float pad     = S(Theme::kPadX);
    const float rowH    = S(Theme::kRowH);
    const float checkSz = S(Theme::kCheckSize);
    const float handleW = S(18);
    const float delW    = S(20);
    const float gap     = S(6);

    float docY = 0;
    const int active = model_.ActiveCount();
    const int total  = model_.Count();

    auto makeRow = [&](int itemIndex, bool completed) {
        RowLayout r{};
        r.itemIndex = itemIndex;
        r.completed = completed;
        r.row    = D2D1::RectF(pad, docY, W - pad, docY + rowH);
        float cy = docY + rowH / 2;
        r.check  = D2D1::RectF(pad, cy - checkSz / 2, pad + checkSz, cy + checkSz / 2);
        r.handle = D2D1::RectF(W - pad - handleW, docY, W - pad, docY + rowH);
        r.del    = D2D1::RectF(r.handle.left - gap - delW, cy - delW / 2, r.handle.left - gap, cy + delW / 2);
        r.text   = D2D1::RectF(r.check.right + S(8), docY, r.del.left - gap, docY + rowH);
        rows_.push_back(r);
        docY += rowH;
    };

    for (int i = 0; i < active; i++) makeRow(i, false);

    if (total - active > 0) {
        sectionRect_ = D2D1::RectF(pad, docY, W - pad, docY + S(Theme::kSectionH));
        clearRect_   = D2D1::RectF(W - pad - S(44), docY, W - pad, docY + S(Theme::kSectionH));
        docY += S(Theme::kSectionH);
        if (ui_.completedExpanded)
            for (int i = active; i < total; i++) makeRow(i, true);
    } else {
        sectionRect_ = D2D1::RectF(0, 0, 0, 0);
        clearRect_   = D2D1::RectF(0, 0, 0, 0);
    }
    contentH_ = docY;

    // 标题栏按钮（固定客户坐标）
    const float btn = S(24), m = S(8);
    const float ty  = (S(Theme::kTitleH) - btn) / 2;
    closeRect_ = D2D1::RectF(W - m - btn, ty, W - m, ty + btn);
    pinRect_   = D2D1::RectF(closeRect_.left - S(4) - btn, ty, closeRect_.left - S(4), ty + btn);
    menuRect_  = D2D1::RectF(pinRect_.left - S(4) - btn, ty, pinRect_.left - S(4), ty + btn);
}

// ——————————————————————————— 命中测试 ———————————————————————————

MainWindow::Hit MainWindow::HitTest(float x, float y) {
    Hit h;
    D2D1_POINT_2F p{ x, y };

    if (y < S(Theme::kTitleH)) {
        if (InRect(menuRect_, p))  { h.kind = HitKind::Menu;  return h; }
        if (InRect(pinRect_, p))   { h.kind = HitKind::Pin;   return h; }
        if (InRect(closeRect_, p)) { h.kind = HitKind::Close; return h; }
        return h; // 标题栏空白 -> 交给 NCHITTEST 拖动
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float H = (float)(rc.bottom - rc.top);
    if (y >= H - S(Theme::kFooterH)) { h.kind = HitKind::Footer; return h; }

    float docY = y - ContentTop() + scroll_;
    D2D1_POINT_2F dp{ x, docY };

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
            if (InRect(r.check, dp))                  { h.kind = HitKind::Check;  return h; }
            if (InRect(r.del, dp))                    { h.kind = HitKind::Delete; return h; }
            if (!r.completed && InRect(r.handle, dp)) { h.kind = HitKind::Handle; return h; }
            if (!r.completed && InRect(r.text, dp))   { h.kind = HitKind::Text;   return h; }
            return h;
        }
    }
    return h;
}

// ——————————————————————————— 渲染 ———————————————————————————

void MainWindow::FillRect(const D2D1_RECT_F& r, uint32_t rgb, float a) {
    brush_->SetColor(Theme::Color(rgb, a));
    rt_->FillRectangle(r, brush_);
}

void MainWindow::StrokeRect(const D2D1_RECT_F& r, uint32_t rgb, float w, float a) {
    brush_->SetColor(Theme::Color(rgb, a));
    rt_->DrawRectangle(r, brush_, w);
}

void MainWindow::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t rgb,
                      IDWriteTextFormat* fmt) {
    if (s.empty()) return;
    brush_->SetColor(Theme::Color(rgb));
    rt_->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, brush_);
}

void MainWindow::DrawCheckbox(const D2D1_RECT_F& box, bool checked) {
    D2D1_ROUNDED_RECT rr{ box, S(4), S(4) };
    if (checked) {
        brush_->SetColor(Theme::Color(Theme::kCheckFill));
        rt_->FillRoundedRectangle(rr, brush_);
        brush_->SetColor(Theme::Color(Theme::kCheckMark));
        float l = box.left, t = box.top, w = box.right - box.left, hh = box.bottom - box.top;
        rt_->DrawLine(D2D1::Point2F(l + w * 0.24f, t + hh * 0.52f),
                      D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f), brush_, S(2));
        rt_->DrawLine(D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f),
                      D2D1::Point2F(l + w * 0.76f, t + hh * 0.30f), brush_, S(2));
    } else {
        brush_->SetColor(Theme::Color(Theme::kCheckBorder));
        rt_->DrawRoundedRectangle(rr, brush_, S(1.5f));
    }
}

void MainWindow::DrawRow(const RowLayout& r, bool hovered) {
    if (hovered) FillRect(r.row, Theme::kHover, 0.04f);

    DrawCheckbox(r.check, r.completed);

    if (!(editing() && editIndex_ == r.itemIndex)) {
        const std::wstring& s = model_.Items()[r.itemIndex].text;
        if (r.completed) {
            if (!r.strikeLayout && !s.empty()) { // 仅首帧创建带删除线的布局，之后复用
                IDWriteTextLayout* layout = nullptr;
                if (SUCCEEDED(dwrite_->CreateTextLayout(s.c_str(), (UINT32)s.size(), textFormat_,
                                                        r.text.right - r.text.left,
                                                        r.text.bottom - r.text.top, &layout))) {
                    DWRITE_TEXT_RANGE rg{ 0, (UINT32)s.size() };
                    layout->SetStrikethrough(TRUE, rg);
                    r.strikeLayout = layout;
                }
            }
            if (r.strikeLayout) {
                brush_->SetColor(Theme::Color(Theme::kTextDone));
                rt_->DrawTextLayout(D2D1::Point2F(r.text.left, r.text.top), r.strikeLayout, brush_);
            }
        } else {
            Text(s, r.text, Theme::kText, textFormat_);
        }
    }

    if (hovered) {
        brush_->SetColor(Theme::Color(Theme::kDanger));
        D2D1_RECT_F d = r.del;
        float p = S(5);
        rt_->DrawLine(D2D1::Point2F(d.left + p, d.top + p), D2D1::Point2F(d.right - p, d.bottom - p), brush_, S(1.6f));
        rt_->DrawLine(D2D1::Point2F(d.right - p, d.top + p), D2D1::Point2F(d.left + p, d.bottom - p), brush_, S(1.6f));

        if (!r.completed) {
            brush_->SetColor(Theme::Color(Theme::kHandle));
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

    brush_->SetColor(Theme::Color(Theme::kDivider));
    rt_->DrawLine(D2D1::Point2F(sectionRect_.left, sectionRect_.top),
                  D2D1::Point2F(sectionRect_.right, sectionRect_.top), brush_, S(1));

    wchar_t buf[96];
    swprintf_s(buf, L"%s (%d) %s", T(Str::Completed, lang_), model_.CompletedCount(),
               ui_.completedExpanded ? L"▾" : L"▸");
    D2D1_RECT_F lr = sectionRect_;
    lr.left += S(2);
    Text(buf, lr, Theme::kTextWeak, smallFormat_);

    Text(T(Str::Clear, lang_), clearRect_, Theme::kDanger, smallFormat_);
}

void MainWindow::DrawTitleBar() {
    D2D1_RECT_F tr = D2D1::RectF(S(Theme::kPadX), 0, menuRect_.left - S(8), S(Theme::kTitleH));
    Text(L"X-TODO", tr, Theme::kTextWeak, smallFormat_);

    // 菜单按钮：三条横线
    brush_->SetColor(Theme::Color(Theme::kTextWeak));
    {
        float mcx = (menuRect_.left + menuRect_.right) / 2;
        float mcy = (menuRect_.top + menuRect_.bottom) / 2;
        for (int i = -1; i <= 1; i++) {
            float yy = mcy + i * S(4);
            rt_->DrawLine(D2D1::Point2F(mcx - S(6), yy), D2D1::Point2F(mcx + S(6), yy), brush_, S(1.5f));
        }
    }

    D2D1_POINT_2F pc = D2D1::Point2F((pinRect_.left + pinRect_.right) / 2,
                                     (pinRect_.top + pinRect_.bottom) / 2);
    D2D1_ELLIPSE pe = D2D1::Ellipse(pc, S(5), S(5));
    if (ui_.alwaysOnTop) {
        brush_->SetColor(Theme::Color(Theme::kCheckFill));
        rt_->FillEllipse(pe, brush_);
    } else {
        brush_->SetColor(Theme::Color(Theme::kHandle));
        rt_->DrawEllipse(pe, brush_, S(1.5f));
    }

    brush_->SetColor(Theme::Color(Theme::kTextWeak));
    D2D1_RECT_F c = closeRect_;
    float p = S(7);
    rt_->DrawLine(D2D1::Point2F(c.left + p, c.top + p), D2D1::Point2F(c.right - p, c.bottom - p), brush_, S(1.6f));
    rt_->DrawLine(D2D1::Point2F(c.right - p, c.top + p), D2D1::Point2F(c.left + p, c.bottom - p), brush_, S(1.6f));
}

void MainWindow::DrawFooter() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);
    float top = H - S(Theme::kFooterH);

    brush_->SetColor(Theme::Color(Theme::kDivider));
    rt_->DrawLine(D2D1::Point2F(0, top), D2D1::Point2F(W, top), brush_, S(1));

    D2D1_RECT_F tr = D2D1::RectF(S(Theme::kPadX), top, W - S(Theme::kPadX), H);
    Text(T(Str::NewItem, lang_), tr, Theme::kTextWeak, smallFormat_);
}

bool MainWindow::Render() {
    if (!CreateDeviceResources()) return false;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);

    if (capsuleShrunk()) { // 折叠胶囊：只画小纸块 + 未完成计数
        rt_->BeginDraw();
        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        rt_->Clear(Theme::Color(Theme::kPaper));
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(1, 1, W - 1, H - 1), S(8), S(8) };
        brush_->SetColor(Theme::Color(Theme::kPaperEdge));
        rt_->DrawRoundedRectangle(rr, brush_, S(1.5f));
        int n = model_.ActiveCount();
        wchar_t buf[16];
        swprintf_s(buf, L"%d", n);
        brush_->SetColor(Theme::Color(n > 0 ? Theme::kText : Theme::kTextWeak));
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
    rt_->Clear(Theme::Color(Theme::kPaper));

    // 可滚动内容层
    D2D1_RECT_F vp = D2D1::RectF(0, ContentTop(), W, H - S(Theme::kFooterH));
    rt_->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, ContentTop() - scroll_));

    for (size_t i = 0; i < rows_.size(); i++)
        DrawRow(rows_[i], (int)i == hoverRow_);
    DrawSection();

    if (dragging_ && dragInsert_ >= 0) {
        float yy = dragInsert_ * S(Theme::kRowH);
        brush_->SetColor(Theme::Color(Theme::kCheckFill));
        rt_->DrawLine(D2D1::Point2F(S(Theme::kPadX), yy),
                      D2D1::Point2F(W - S(Theme::kPadX), yy), brush_, S(2));
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();

    // 固定层
    DrawTitleBar();
    DrawFooter();
    StrokeRect(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), Theme::kPaperEdge, 1.0f);

    if (rt_->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return false;
    }
    return true;
}

// ——————————————————————————— 鼠标 / 键盘 ———————————————————————————

void MainWindow::OnLButtonDown(float x, float y) {
    if (animActive_) return;                                     // 动画中忽略点击
    if (capsuleShrunk()) { StartCapsuleAnim(true); return; }     // 胶囊态点击也滑出
    if (editing()) { CommitEdit(false); return; }
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
    if (dragging_) {
        dragging_ = false;
        int target = dragInsert_;
        if (target > dragFrom_) target--; // 移除源项后插入点左移
        if (target >= 0 && target != dragFrom_) {
            model_.MoveActive(dragFrom_, target);
            RebuildLayout();
            ScheduleSave();
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
    case HitKind::Check:
        model_.SetDone(h.itemIndex, !model_.Items()[h.itemIndex].done);
        RebuildLayout();
        ClampScroll();
        ScheduleSave();
        break;
    case HitKind::Text:
        BeginEdit(h.itemIndex);
        break;
    case HitKind::Delete:
        if (MessageBoxW(hwnd_, T(Str::DeleteItemMsg, lang_), L"X-TODO",
                        MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            DeleteItem(h.itemIndex);
        break;
    case HitKind::Section: ToggleCompletedExpanded(); break;
    case HitKind::Clear:   ClearCompletedConfirm();   break;
    case HitKind::Menu:    ShowTitleMenu();           break;
    case HitKind::Pin:     TogglePin();               break;
    case HitKind::Close:   HideToTray();              break;
    case HitKind::Footer: {
        int n = model_.AddActive(L"");
        RebuildLayout();
        scroll_ = contentH_;
        ClampScroll();
        BeginEdit(n);
        break;
    }
    default: break;
    }
    pressHit_ = Hit{};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnMouseMove(float x, float y, bool /*lButton*/) {
    if (dragging_) {
        dragY_ = y;
        float docY = y - ContentTop() + scroll_;
        int active = model_.ActiveCount();
        int insert = active;
        float rowH = S(Theme::kRowH);
        for (int i = 0; i < active; i++) {
            if (docY < i * rowH + rowH / 2) { insert = i; break; }
        }
        dragInsert_ = insert;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    Hit h = HitTest(x, y);
    if (h.rowIndex != hoverRow_) {
        hoverRow_ = h.rowIndex;
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
    if (mountMode_ != MountMode::Normal) return HTCLIENT; // 仅普通形态可拖动/缩放窗口
    POINT p{ sx, sy };
    ScreenToClient(hwnd_, &p);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float e = S(Theme::kResizeEdge);
    bool L = p.x < e, R = p.x >= rc.right - e, T = p.y < e, B = p.y >= rc.bottom - e;
    if (T && L) return HTTOPLEFT;
    if (T && R) return HTTOPRIGHT;
    if (B && L) return HTBOTTOMLEFT;
    if (B && R) return HTBOTTOMRIGHT;
    if (L) return HTLEFT;
    if (R) return HTRIGHT;
    if (T) return HTTOP;
    if (B) return HTBOTTOM;

    if (p.y < S(Theme::kTitleH)) {
        D2D1_POINT_2F pt{ (float)p.x, (float)p.y };
        if (InRect(menuRect_, pt) || InRect(pinRect_, pt) || InRect(closeRect_, pt)) return HTCLIENT;
        return HTCAPTION;
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
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
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

    int len = GetWindowTextLengthW(edit_);
    std::wstring text(len, L'\0');
    if (len) GetWindowTextW(edit_, text.data(), len + 1);
    ShowWindow(edit_, SW_HIDE);

    text = Trim(text);
    if (text.empty()) model_.Remove(idx);
    else              model_.SetText(idx, text);

    RebuildLayout();
    ScheduleSave();

    if (addNext && !text.empty()) {
        int n = model_.AddActive(L"");
        RebuildLayout();
        scroll_ = contentH_;
        ClampScroll();
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
    ClampScroll();
    SetFocus(hwnd_);
    MaybeCollapseCapsule(); // 取消编辑后，鼠标已在外则收回胶囊
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::MaybeCollapseCapsule() {
    if (mountMode_ != MountMode::Capsule || !capsuleExpanded_ || animActive_) return;
    POINT cur;
    GetCursorPos(&cur);
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    if (!PtInRect(&wr, cur)) StartCapsuleAnim(false);
}

void MainWindow::LayoutEditBox() {
    if (!editing() || !edit_) return;
    float off = ContentTop() - scroll_;
    for (const RowLayout& r : rows_) {
        if (r.itemIndex == editIndex_ && !r.completed) {
            int left = (int)r.text.left;
            int top  = (int)(r.row.top + off + S(4));
            int w    = (int)(r.text.right - r.text.left);
            int h    = (int)(r.row.bottom - r.row.top - S(8));
            MoveWindow(edit_, left, top, w, h, TRUE);
            return;
        }
    }
}

LRESULT CALLBACK MainWindow::EditProcStatic(HWND h, UINT m, WPARAM w, LPARAM l,
                                            UINT_PTR id, DWORD_PTR ref) {
    MainWindow* self = reinterpret_cast<MainWindow*>(ref);
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_RETURN) { self->CommitEdit(true);  return 0; }
        if (w == VK_ESCAPE) { self->CancelEdit();      return 0; }
        break;
    case WM_CHAR:
        if (w == 0x0D || w == 0x1B) return 0; // 吞掉回车/Esc 的字符，避免提示音
        break;
    case WM_KILLFOCUS:
        self->CommitEdit(false);
        break;
    }
    return DefSubclassProc(h, m, w, l); // 注意：仅 4 参，不传 id/ref
}
