// 任务栏嵌入状态条（MountMode::Taskbar）。
// 专用子窗口 taskbarHwnd_ 通过带 parent 的 CreateWindowExW 直接挂到 Explorer
// 任务栏窗口（Shell_TrayWnd / Shell_SecondaryTrayWnd）下，绝不 reparent。
// 主窗口 hwnd_ 始终保持独立顶层 WS_POPUP，仅在任务栏模式下隐藏。
#include "MainWindow.h"
#include "Theme.h"
#include "I18n.h"
#include <windowsx.h>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kTaskbarBandClass[] = L"XTodoTaskbarBandClass";

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 收集任务栏关键入口（开始 / 搜索 / Widgets / 托盘 / 显示桌面）的 x 区间，
// 转成父窗口客户坐标，用于轻量避让。水平任务栏状态条占满高度，只需按 x 区间避让。
std::vector<std::pair<int,int>> CollectBlockerXRanges(HWND parent) {
    std::vector<std::pair<int,int>> v;
    struct Ctx { HWND parent; std::vector<std::pair<int,int>>* v; } ctx{ parent, &v };
    EnumChildWindows(parent, [](HWND ch, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        if (!IsWindowVisible(ch)) return TRUE;
        wchar_t cls[80] = {0};
        GetClassNameW(ch, cls, 79);
        static const wchar_t* kBlock[] = {
            L"Start", L"TrayNotifyWnd", L"TrayShowDesktopButtonWClass"
        };
        bool block = false;
        for (auto* b : kBlock) if (wcscmp(cls, b) == 0) { block = true; break; }
        if (!block && (wcsstr(cls, L"Search") || wcsstr(cls, L"Widget"))) block = true;
        if (!block) return TRUE;
        RECT rc{};
        if (!GetWindowRect(ch, &rc)) return TRUE;
        POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
        ScreenToClient(c->parent, &tl);
        ScreenToClient(c->parent, &br);
        c->v->push_back({ tl.x, br.x });
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return v;
}

// 在 [margin, parentW-margin-w] 内找最靠近 desired 且不与 blocker 相交的 x；找不到返回 -1。
int FindFreeX(int desired, int w, int margin, int parentW,
              const std::vector<std::pair<int,int>>& bl) {
    int lo = margin, hi = parentW - margin - w;
    if (hi < lo) return -1;
    auto overlaps = [&](int x) {
        for (auto& b : bl) if (x < b.second && x + w > b.first) return true;
        return false;
    };
    int x = ClampI(desired, lo, hi);
    if (!overlaps(x)) return x;
    for (int d = 1; d <= hi - lo; ++d) {
        if (x - d >= lo && !overlaps(x - d)) return x - d;
        if (x + d <= hi && !overlaps(x + d)) return x + d;
    }
    return -1;
}

} // namespace

LRESULT CALLBACK MainWindow::TaskbarWndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->TaskbarWndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool MainWindow::RegisterTaskbarBandClass() {
    if (taskbarClassRegistered_) return true;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = &MainWindow::TaskbarWndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kTaskbarBandClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;
    taskbarClassRegistered_ = true;
    return true;
}

bool MainWindow::EnsureTaskbarBand() {
    if (!RegisterTaskbarBandClass()) return false;

    struct Host { HWND hwnd; HMONITOR mon; std::wstring dev; };
    std::vector<Host> hosts;
    HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
    auto addHost = [&](HWND h) {
        if (!h) return;
        RECT rc{};
        if (!GetWindowRect(h, &rc)) return;
        HMONITOR mon = MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        hosts.push_back({ h, mon, mi.szDevice });
    };
    addHost(primary);
    HWND sec = nullptr;
    while ((sec = FindWindowExW(nullptr, sec, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr)
        addHost(sec);
    if (hosts.empty()) return false;

    // 选择规则：持久化显示器 > 主窗口所在 > 鼠标所在 > 主任务栏 > 首个有效。
    HWND host = nullptr;
    if (!ui_.taskbarMonitor.empty()) {
        std::wstring want = Utf8ToW(ui_.taskbarMonitor);
        for (auto& h : hosts) if (h.dev == want) { host = h.hwnd; break; }
    }
    if (!host && geom_.valid) {
        POINT c{ geom_.x + geom_.w / 2, geom_.y + geom_.h / 2 };
        HMONITOR m = MonitorFromPoint(c, MONITOR_DEFAULTTONULL);
        if (m) for (auto& h : hosts) if (h.mon == m) { host = h.hwnd; break; }
    }
    if (!host) {
        POINT mp;
        if (GetCursorPos(&mp)) {
            HMONITOR m = MonitorFromPoint(mp, MONITOR_DEFAULTTONULL);
            if (m) for (auto& h : hosts) if (h.mon == m) { host = h.hwnd; break; }
        }
    }
    if (!host && primary)
        for (auto& h : hosts) if (h.hwnd == primary) { host = h.hwnd; break; }
    if (!host) host = hosts.front().hwnd;

    // 父窗口变化（含 Explorer 重启）：销毁旧 band 后重建，绝不 reparent。
    if (taskbarHwnd_ && taskbarParent_ != host) DestroyTaskbarBand();

    taskbarParent_ = host;
    if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) {
        taskbarHwnd_ = CreateWindowExW(
            0, kTaskbarBandClass, L"",
            WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 10, 10,
            taskbarParent_, nullptr, GetModuleHandleW(nullptr), this);
        if (!taskbarHwnd_) { taskbarParent_ = nullptr; return false; }
    }

    TaskbarLayoutResult r = LayoutTaskbarBand();
    if (r == TaskbarLayoutResult::Fatal) { DestroyTaskbarBand(); return false; }

    for (auto& h : hosts) if (h.hwnd == host) { ui_.taskbarMonitor = WToUtf8(h.dev); break; }
    if (r == TaskbarLayoutResult::TransientUnavailable) {
        // 自动隐藏 / Explorer 过渡：不显示未布局的 band，延迟重布局（保留 Taskbar 模式，不回退）
        ShowWindow(taskbarHwnd_, SW_HIDE);
        SetTimer(hwnd_, kTaskbarRetryTimerId, 1000, nullptr);
    } else {
        ShowWindow(taskbarHwnd_, SW_SHOWNA);
    }
    InvalidateTaskbarBand();
    return true;
}

TaskbarLayoutResult MainWindow::LayoutTaskbarBand() {
    if (!taskbarParent_ || !IsWindow(taskbarParent_) || !taskbarHwnd_)
        return TaskbarLayoutResult::Fatal;
    RECT pc{};
    if (!GetClientRect(taskbarParent_, &pc)) return TaskbarLayoutResult::TransientUnavailable;
    UINT dpi = GetDpiForWindow(taskbarParent_); if (!dpi) dpi = 96;
    auto SC = [&](int v) { return MulDiv(v, dpi, 96); };
    int parentW = pc.right - pc.left, parentH = pc.bottom - pc.top;
    bool horizontal = parentH <= parentW;
    int margin = SC(4);

    if (horizontal) {
        int minW = SC(160), maxW = SC(520);
        int maxAllowed = parentW - 2 * margin;
        if (maxAllowed > maxW) maxAllowed = maxW;
        if (maxAllowed < minW) return TaskbarLayoutResult::Fatal;
        int h = parentH - 2 * margin;
        if (h < SC(20)) return TaskbarLayoutResult::TransientUnavailable; // 自动隐藏 / 过渡
        int w = ClampI(SC(ui_.taskbarWidth), minW, maxAllowed);
        int centerX = margin + (int)(ui_.taskbarDockT * (parentW - 2 * margin) + 0.5);
        auto blockers = CollectBlockerXRanges(taskbarParent_);
        int x = FindFreeX(centerX - w / 2, w, margin, parentW, blockers);
        if (x < 0) { // 缩窄重试
            w = SC(120);
            x = FindFreeX(centerX - w / 2, w, margin, parentW, blockers);
            if (x < 0) return TaskbarLayoutResult::Fatal; // 无安全位置：不创建挡入口的窗口
        }
        taskbarBandRect_ = RECT{ x, margin, x + w, margin + h };
    } else {
        int w = parentW - 2 * margin;
        if (w < SC(20)) return TaskbarLayoutResult::TransientUnavailable;
        int hMax = parentH - 2 * margin;
        if (hMax < SC(48)) return TaskbarLayoutResult::TransientUnavailable;
        int h = ClampI(SC(ui_.taskbarWidth), SC(48), SC(180));
        if (h > hMax) h = hMax;
        int centerY = margin + (int)(ui_.taskbarDockT * (parentH - 2 * margin) + 0.5);
        int y = ClampI(centerY - h / 2, margin, parentH - margin - h);
        taskbarBandRect_ = RECT{ margin, y, margin + w, y + h };
    }
    SetWindowPos(taskbarHwnd_, HWND_TOP,
        taskbarBandRect_.left, taskbarBandRect_.top,
        taskbarBandRect_.right - taskbarBandRect_.left,
        taskbarBandRect_.bottom - taskbarBandRect_.top,
        SWP_NOACTIVATE);
    return TaskbarLayoutResult::Ok;
}

void MainWindow::PaintTaskbarBand(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    // 整块填充：覆盖所有像素，避免父任务栏重绘后残影。
    HBRUSH bg = CreateSolidBrush(RGB(28, 28, 30));
    ::FillRect(mem, &rc, bg);
    DeleteObject(bg);

    COLORREF chip = taskbarPressed_ ? RGB(74, 74, 82)
                  : taskbarHover_   ? RGB(58, 58, 66)
                                    : RGB(44, 44, 52);
    HBRUSH cb = CreateSolidBrush(chip);
    HPEN   cp = CreatePen(PS_SOLID, 1, RGB(82, 82, 92));
    HBRUSH ob = (HBRUSH)SelectObject(mem, cb);
    HPEN   op = (HPEN)SelectObject(mem, cp);
    int rad = (h < 28) ? 6 : 10;
    RoundRect(mem, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(mem, ob); SelectObject(mem, op);
    DeleteObject(cb); DeleteObject(cp);

    int active = model_.ActiveCount();
    std::wstring text;
    if (active > 0) {
        int idx = ClampI(taskbarPreviewIndex_, 0, active - 1);
        text = L"(" + std::to_wstring(active) + L") " + model_.Items()[idx].text;
    } else {
        text = T(Str::TaskbarAllDone, lang_);
    }

    UINT dpi = GetDpiForWindow(taskbarParent_ ? taskbarParent_ : hwnd); if (!dpi) dpi = 96;
    int fs = (h < MulDiv(36, dpi, 96)) ? 12 : 13;
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(fs, dpi, 72);
    lstrcpynW(lf.lfFaceName, Theme::kFontFamily, 32);
    HFONT f = CreateFontIndirectW(&lf);
    HFONT of = (HFONT)SelectObject(mem, f);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(208, 224, 255)); // 近白偏蓝，深色任务栏上清晰
    RECT tr = rc;
    tr.left  += MulDiv(10, dpi, 96);
    tr.right -= MulDiv(10, dpi, 96);
    DrawTextW(mem, text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(mem, of);
    DeleteObject(f);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT MainWindow::TaskbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintTaskbarBand(hwnd);
        return 0;
    case WM_MOUSEMOVE: {
        if (!taskbarHover_) {
            taskbarHover_ = true;
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (taskbarPressed_) {
            POINT sp; GetCursorPos(&sp);
            if (!taskbarDragging_ &&
                (abs(sp.x - taskbarPressScreen_.x) > GetSystemMetrics(SM_CXDRAG) ||
                 abs(sp.y - taskbarPressScreen_.y) > GetSystemMetrics(SM_CYDRAG)))
                taskbarDragging_ = true;
            if (taskbarDragging_ && taskbarParent_) {
                POINT pcpt = sp; ScreenToClient(taskbarParent_, &pcpt);
                RECT prc{}; GetClientRect(taskbarParent_, &prc);
                int pw = prc.right, ph = prc.bottom;
                bool horiz = ph <= pw;
                int margin = MulDiv(4, GetDpiForWindow(taskbarParent_), 96);
                int bw = taskbarBandRect_.right - taskbarBandRect_.left;
                int bh = taskbarBandRect_.bottom - taskbarBandRect_.top;
                double t = horiz ? (double)(pcpt.x - taskbarPressOffset_.x + bw / 2 - margin) / (double)(pw - 2 * margin)
                                 : (double)(pcpt.y - taskbarPressOffset_.y + bh / 2 - margin) / (double)(ph - 2 * margin);
                ui_.taskbarDockT = t < 0 ? 0 : (t > 1 ? 1 : t);
                LayoutTaskbarBand();
            }
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        taskbarHover_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        taskbarPressed_ = true;
        taskbarDragging_ = false;
        GetCursorPos(&taskbarPressScreen_);
        taskbarPressOffset_ = POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        bool wasDrag = taskbarDragging_;
        if (GetCapture() == hwnd) ReleaseCapture();
        taskbarPressed_ = false;
        taskbarDragging_ = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (wasDrag) { CaptureTaskbarDockFromBand(); ScheduleSave(); }
        else ShowFromTaskbarBand(); // 未拖动才打开完整窗口
        return 0;
    }
    case WM_MBUTTONUP:
        CompleteTaskbarPreviewItem();
        return 0;
    case WM_RBUTTONUP:
        ShowTrayMenu(); // 复用现有菜单（按光标位置弹出）
        return 0;
    case WM_CAPTURECHANGED:
        taskbarPressed_ = false;
        taskbarDragging_ = false;
        return 0;
    case WM_DPICHANGED_BEFOREPARENT:
    case WM_DPICHANGED_AFTERPARENT:
        LayoutTaskbarBand();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        if (hwnd == taskbarHwnd_) {
            taskbarHwnd_ = nullptr;
            taskbarParent_ = nullptr;
            taskbarHover_ = taskbarPressed_ = taskbarDragging_ = false;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::DestroyTaskbarBand() {
    if (taskbarHwnd_ && IsWindow(taskbarHwnd_)) {
        HWND h = taskbarHwnd_;
        taskbarHwnd_ = nullptr; // 先清，避免 WM_NCDESTROY 重入
        DestroyWindow(h);
    }
    taskbarHwnd_ = nullptr;
    taskbarParent_ = nullptr;
    taskbarHover_ = taskbarPressed_ = taskbarDragging_ = false;
}

bool MainWindow::TryEnterTaskbarMode(bool /*userInitiated*/) {
    if (editing()) CommitEdit(false);
    if (mountMode_ == MountMode::Normal && IsWindowVisible(hwnd_) && !IsIconic(hwnd_))
        CaptureVisibleGeometry(); // 记住完整窗口几何，供 ShowFromTaskbarBand 复用
    KillTimer(hwnd_, kAnimTimerId);
    animActive_ = false;
    capsuleExpanded_ = false;
    if (!EnsureTaskbarBand()) { DestroyTaskbarBand(); return false; }
    mountMode_ = MountMode::Taskbar;
    ui_.mountMode = "taskbar";
    ShowWindow(hwnd_, SW_HIDE);
    ScheduleSave();
    return true;
}

void MainWindow::LeaveTaskbarMode() {
    DestroyTaskbarBand();
}

void MainWindow::ScheduleTaskbarRetry() {
    // 新原因触发的重试：清失败计数后定时，WM_TIMER 内连续重试才累计回退
    taskbarRetryCount_ = 0;
    SetTimer(hwnd_, kTaskbarRetryTimerId, 1000, nullptr);
}

void MainWindow::ShowFromTaskbarBand() {
    SetWindowRgn(hwnd_, nullptr, TRUE); // 清除可能残留的胶囊 region
    int x = geom_.valid ? geom_.x : 100;
    int y = geom_.valid ? geom_.y : 100;
    int w = geom_.valid ? geom_.w : (int)S(300);
    int h = geom_.valid ? geom_.h : (int)S(420);
    HMONITOR mon = MonitorFromWindow(taskbarParent_ ? taskbarParent_ : hwnd_,
                                     MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        RECT wa = mi.rcWork;
        if (x + w > wa.right)  x = wa.right - w;
        if (y + h > wa.bottom) y = wa.bottom - h;
        if (x < wa.left) x = wa.left;
        if (y < wa.top)  y = wa.top;
    }
    SetWindowPos(hwnd_, ui_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateLayeredState();
    RebuildLayout();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CompleteTaskbarPreviewItem() {
    if (model_.ActiveCount() == 0) return;
    if (IsWindowVisible(hwnd_) && editing()) CommitEdit(false);
    int idx = ClampI(taskbarPreviewIndex_, 0, model_.ActiveCount() - 1);
    model_.SetDone(idx, true);
    ClampTaskbarPreviewIndex();
    RebuildLayout();
    ScheduleSave();
    InvalidateTaskbarBand();
    if (IsWindowVisible(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CaptureTaskbarDockFromBand() {
    if (!taskbarParent_ || !IsWindow(taskbarParent_) || !taskbarHwnd_) return;
    RECT pc{}; GetClientRect(taskbarParent_, &pc);
    int pw = pc.right - pc.left, ph = pc.bottom - pc.top;
    bool horizontal = ph <= pw;
    int margin = MulDiv(4, GetDpiForWindow(taskbarParent_), 96);
    double t;
    if (horizontal) {
        int cx = (taskbarBandRect_.left + taskbarBandRect_.right) / 2;
        t = (double)(cx - margin) / (double)(pw - 2 * margin);
    } else {
        int cy = (taskbarBandRect_.top + taskbarBandRect_.bottom) / 2;
        t = (double)(cy - margin) / (double)(ph - 2 * margin);
    }
    ui_.taskbarDockT = t < 0 ? 0 : (t > 1 ? 1 : t);
}

void MainWindow::InvalidateTaskbarBand() {
    if (taskbarHwnd_ && IsWindow(taskbarHwnd_))
        InvalidateRect(taskbarHwnd_, nullptr, FALSE);
}

void MainWindow::ClampTaskbarPreviewIndex() {
    int a = model_.ActiveCount();
    taskbarPreviewIndex_ = (a <= 0) ? 0 : ClampI(taskbarPreviewIndex_, 0, a - 1);
}

void MainWindow::OnModelOrUiChangedForTaskbarBand() {
    if (mountMode_ != MountMode::Taskbar) return;
    ClampTaskbarPreviewIndex();
    InvalidateTaskbarBand();
}
