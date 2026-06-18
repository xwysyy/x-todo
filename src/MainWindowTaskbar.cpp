// 任务栏嵌入状态条（MountMode::Taskbar）。
// 参考 TrafficMonitor 的 Win11 路径：创建 WS_POPUP 工具窗，SetParent 到
// Shell_TrayWnd / Shell_SecondaryTrayWnd，后续使用任务栏父窗口客户坐标布局。
// 主窗口 hwnd_ 始终保持独立顶层 WS_POPUP，仅在状态条可见后隐藏。
#include "MainWindow.h"
#include "Theme.h"
#include "I18n.h"
#include <wingdi.h>
#include <windowsx.h>
#include <cwchar>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kTaskbarBandClass[] = L"XTodoTaskbarBandClass";

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool SetWindowLongPtrChecked(HWND hwnd, int index, LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    LONG_PTR old = SetWindowLongPtrW(hwnd, index, value);
    return old != 0 || GetLastError() == ERROR_SUCCESS;
}

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

bool AttachToTaskbarParent(HWND child, HWND parent) {
    if (!child || !parent || !IsWindow(child) || !IsWindow(parent)) return false;

    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    style &= ~WS_CHILD;
    style |= WS_POPUP | WS_SYSMENU | WS_CLIPSIBLINGS;
    if (!SetWindowLongPtrChecked(child, GWL_STYLE, style)) return false;

    LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;
    ex &= ~(WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE);
    if (!SetWindowLongPtrChecked(child, GWL_EXSTYLE, ex)) return false;

    SetLastError(ERROR_SUCCESS);
    HWND oldParent = SetParent(child, parent);
    DWORD err = GetLastError();
    if (!oldParent && err != ERROR_SUCCESS) return false;

    SetWindowPos(child, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                 SWP_NOZORDER | SWP_FRAMECHANGED);
    return true;
}

bool WindowRectInParentClient(HWND parent, HWND child, RECT& out) {
    RECT rc{};
    if (!parent || !child || !GetWindowRect(child, &rc)) return false;
    POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
    if (!ScreenToClient(parent, &tl) || !ScreenToClient(parent, &br)) return false;
    out = RECT{ tl.x, tl.y, br.x, br.y };
    return true;
}

bool IsTaskbarCenterAligned() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
        L"TaskbarAl",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status != ERROR_SUCCESS || value != 0;
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

bool MainWindow::IsTaskbarBandAttached() const {
    return taskbarInserted_ &&
           taskbarHwnd_ && taskbarParent_ &&
           IsWindow(taskbarHwnd_) && IsWindow(taskbarParent_);
}

bool MainWindow::IsTaskbarBandReady() const {
    if (!IsTaskbarBandAttached()) return false;
    if (!IsWindowVisible(taskbarHwnd_)) return false;
    RECT wr{};
    if (!GetWindowRect(taskbarHwnd_, &wr)) return false;
    return wr.right > wr.left && wr.bottom > wr.top;
}

void MainWindow::TraceTaskbarBand(const wchar_t* tag) const {
#ifdef _DEBUG
    RECT pr{}, wr{}, cr{};
    if (taskbarParent_ && IsWindow(taskbarParent_)) GetWindowRect(taskbarParent_, &pr);
    if (taskbarHwnd_ && IsWindow(taskbarHwnd_)) {
        GetWindowRect(taskbarHwnd_, &wr);
        GetClientRect(taskbarHwnd_, &cr);
    }
    LONG_PTR style = taskbarHwnd_ && IsWindow(taskbarHwnd_)
        ? GetWindowLongPtrW(taskbarHwnd_, GWL_STYLE) : 0;
    LONG_PTR exStyle = taskbarHwnd_ && IsWindow(taskbarHwnd_)
        ? GetWindowLongPtrW(taskbarHwnd_, GWL_EXSTYLE) : 0;

    wchar_t buf[1024]{};
    swprintf_s(buf,
        L"[X-TODO taskbar] %s parent=%p band=%p inserted=%d attachErr=%lu "
        L"gp=%p ga=%p visible=%d style=%p ex=%p parentRect=(%ld,%ld,%ld,%ld) "
        L"bandRect=(%ld,%ld,%ld,%ld) client=(%ld,%ld)\n",
        tag ? tag : L"",
        reinterpret_cast<void*>(taskbarParent_),
        reinterpret_cast<void*>(taskbarHwnd_),
        taskbarInserted_ ? 1 : 0,
        taskbarAttachError_,
        reinterpret_cast<void*>(taskbarHwnd_ ? GetParent(taskbarHwnd_) : nullptr),
        reinterpret_cast<void*>(taskbarHwnd_ ? GetAncestor(taskbarHwnd_, GA_PARENT) : nullptr),
        taskbarHwnd_ ? IsWindowVisible(taskbarHwnd_) : 0,
        reinterpret_cast<void*>(style),
        reinterpret_cast<void*>(exStyle),
        pr.left, pr.top, pr.right, pr.bottom,
        wr.left, wr.top, wr.right, wr.bottom,
        cr.right - cr.left, cr.bottom - cr.top);
    OutputDebugStringW(buf);
#else
    (void)tag;
#endif
}

TaskbarLayoutResult MainWindow::EnsureTaskbarBand() {
    if (!RegisterTaskbarBandClass()) return TaskbarLayoutResult::Fatal;

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
    if (hosts.empty()) return TaskbarLayoutResult::TransientUnavailable;

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

    // 任务栏 host 变化（含 Explorer 重启）：销毁旧 band 后重建。
    if (taskbarHwnd_ && taskbarParent_ != host) DestroyTaskbarBand();

    taskbarParent_ = host;
    if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) {
        taskbarInserted_ = false;
        taskbarAttachError_ = ERROR_SUCCESS;
        taskbarHwnd_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kTaskbarBandClass, L"",
            WS_POPUP | WS_SYSMENU | WS_CLIPSIBLINGS,
            0, 0, 10, 10,
            hwnd_, nullptr, GetModuleHandleW(nullptr), this);
        if (!taskbarHwnd_) { taskbarParent_ = nullptr; return TaskbarLayoutResult::Fatal; }
        TraceTaskbarBand(L"after-create");
    }
    if (!AttachToTaskbarParent(taskbarHwnd_, taskbarParent_)) {
        taskbarInserted_ = false;
        taskbarAttachError_ = GetLastError();
        TraceTaskbarBand(L"attach-failed");
        DestroyTaskbarBand();
        return TaskbarLayoutResult::TransientUnavailable;
    }
    taskbarInserted_ = true;
    taskbarAttachError_ = ERROR_SUCCESS;
    TraceTaskbarBand(L"after-setparent");

    TaskbarLayoutResult r = LayoutTaskbarBand();
    if (r == TaskbarLayoutResult::Fatal) { DestroyTaskbarBand(); return TaskbarLayoutResult::TransientUnavailable; }
    TraceTaskbarBand(L"after-layout");

    for (auto& h : hosts) if (h.hwnd == host) { ui_.taskbarMonitor = WToUtf8(h.dev); break; }
    if (r == TaskbarLayoutResult::TransientUnavailable) {
        // 自动隐藏 / Explorer 过渡：不显示未布局的 band，由调用方决定是否重试。
        KillTimer(hwnd_, kTaskbarRefreshTimerId);
        ShowWindow(taskbarHwnd_, SW_HIDE);
    } else {
        ShowWindow(taskbarHwnd_, SW_SHOW);
        SetWindowPos(taskbarHwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(hwnd_, kTaskbarRefreshTimerId, 1000, nullptr);
        TraceTaskbarBand(L"after-show");
    }
    InvalidateTaskbarBand();
    return r;
}

TaskbarLayoutResult MainWindow::LayoutTaskbarBand() {
    if (!taskbarParent_ || !IsWindow(taskbarParent_) || !taskbarHwnd_ || !IsWindow(taskbarHwnd_))
        return TaskbarLayoutResult::TransientUnavailable;
    if (!IsTaskbarBandAttached())
        return TaskbarLayoutResult::TransientUnavailable;
    RECT pc{};
    if (!GetClientRect(taskbarParent_, &pc)) return TaskbarLayoutResult::TransientUnavailable;
    UINT dpi = GetDpiForWindow(taskbarParent_); if (!dpi) dpi = 96;
    auto SC = [&](int v) { return MulDiv(v, dpi, 96); };
    int parentW = pc.right - pc.left, parentH = pc.bottom - pc.top;
    bool horizontal = parentH <= parentW;
    int margin = SC(4);
    if (parentW <= 2 * margin || parentH <= 2 * margin)
        return TaskbarLayoutResult::TransientUnavailable;

    if (horizontal) {
        int minW = SC(96), maxW = SC(520);
        int maxAllowed = parentW - 2 * margin;
        if (maxAllowed > maxW) maxAllowed = maxW;
        if (maxAllowed < SC(48)) return TaskbarLayoutResult::TransientUnavailable;
        int maxH = parentH - 2 * margin;
        if (maxH < SC(16)) return TaskbarLayoutResult::TransientUnavailable;
        int h = maxH < SC(24) ? maxH : ClampI(SC(32), SC(24), maxH);

        HWND start = FindWindowExW(taskbarParent_, nullptr, L"Start", nullptr);
        RECT startRc{};
        bool hasStart = start && WindowRectInParentClient(taskbarParent_, start, startRc) &&
                        startRc.right > startRc.left && startRc.bottom > startRc.top;

        int startH = hasStart ? (startRc.bottom - startRc.top) : 0;
        int y = (hasStart && startH > 0)
              ? (startH - h) / 2 + (parentH - startH)
              : (parentH - h) / 2;
        y = ClampI(y, margin, parentH - margin - h);

        int w = SC(ui_.taskbarWidth);
        if (w < minW) w = minW;
        if (w > maxAllowed) w = maxAllowed;

        bool requestLeft = ui_.taskbarDockT < 0.5;
        int x = 0;
        if (requestLeft && IsTaskbarCenterAligned()) {
            x = hasStart ? (startRc.left - w - SC(2)) : SC(2);
        } else {
            HWND notify = FindWindowExW(taskbarParent_, nullptr, L"TrayNotifyWnd", nullptr);
            RECT notifyRc{};
            if (notify &&
                WindowRectInParentClient(taskbarParent_, notify, notifyRc) &&
                notifyRc.right > notifyRc.left) {
                x = notifyRc.left - w + SC(2);
            } else {
                x = parentW - SC(88) - w;
            }
        }
        if (x + w > parentW - margin) x = parentW - margin - w;
        if (x < margin) x = margin;
        taskbarBandRect_ = RECT{ x, y, x + w, y + h };
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
    int bw = taskbarBandRect_.right - taskbarBandRect_.left;
    int bh = taskbarBandRect_.bottom - taskbarBandRect_.top;
    int radius = (bh < SC(28)) ? SC(6) : SC(10);
    HRGN rgn = CreateRoundRectRgn(0, 0, bw + 1, bh + 1, radius, radius);
    if (rgn && !SetWindowRgn(taskbarHwnd_, rgn, TRUE)) DeleteObject(rgn);

    SetWindowPos(taskbarHwnd_, HWND_TOP,
        taskbarBandRect_.left, taskbarBandRect_.top, bw, bh,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return TaskbarLayoutResult::Ok;
}

void MainWindow::PaintTaskbarBand(HWND hwnd) {
    TraceTaskbarBand(L"paint");

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

#ifdef _DEBUG
    HPEN dbgPen = CreatePen(PS_SOLID, 2, RGB(255, 0, 255));
    HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
    HPEN oldPen = (HPEN)SelectObject(mem, dbgPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(mem, hollow);
    Rectangle(mem, 0, 0, w, h);
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBrush);
    DeleteObject(dbgPen);
#endif

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
            taskbarInserted_ = false;
            taskbarAttachError_ = ERROR_SUCCESS;
            taskbarHover_ = taskbarPressed_ = taskbarDragging_ = false;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::DestroyTaskbarBand() {
    KillTimer(hwnd_, kTaskbarRefreshTimerId);
    if (taskbarHwnd_ && IsWindow(taskbarHwnd_)) {
        HWND h = taskbarHwnd_;
        taskbarHwnd_ = nullptr; // 先清，避免 WM_NCDESTROY 重入
        DestroyWindow(h);
    }
    taskbarHwnd_ = nullptr;
    taskbarParent_ = nullptr;
    taskbarInserted_ = false;
    taskbarAttachError_ = ERROR_SUCCESS;
    taskbarHover_ = taskbarPressed_ = taskbarDragging_ = false;
}

bool MainWindow::TryEnterTaskbarMode(bool /*userInitiated*/) {
    if (editing()) CommitEdit(false);
    if (mountMode_ == MountMode::Normal && IsWindowVisible(hwnd_) && !IsIconic(hwnd_))
        CaptureVisibleGeometry(); // 记住完整窗口几何，供 ShowFromTaskbarBand 复用
    KillTimer(hwnd_, kAnimTimerId);
    animActive_ = false;
    capsuleExpanded_ = false;
    TaskbarLayoutResult r = EnsureTaskbarBand();
    mountMode_ = MountMode::Taskbar;
    ui_.mountMode = "taskbar";
    if (r == TaskbarLayoutResult::Ok && IsTaskbarBandReady()) {
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        TraceTaskbarBand(L"enter-not-ready");
        ShowWindow(hwnd_, SW_SHOW);
        ScheduleTaskbarRetry(true);
    }
    ScheduleSave();
    return true;
}

void MainWindow::LeaveTaskbarMode() {
    KillTimer(hwnd_, kTaskbarRetryTimerId);
    KillTimer(hwnd_, kTaskbarRefreshTimerId);
    taskbarRetryHideMain_ = false;
    DestroyTaskbarBand();
}

void MainWindow::ScheduleTaskbarRetry(bool hideMainOnOk) {
    taskbarRetryHideMain_ = hideMainOnOk;
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
