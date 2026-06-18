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
constexpr UINT kTaskbarAppbarCallbackMsg = WM_APP + 64;

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

enum class TaskbarEmbedStrategy {
    PopupShellNoOwner = 0,
    PopupShellOwner,
    ChildShellSetParent,
    CreateChildShell,
    PopupTrayNotify,
    ChildTrayNotify,
    PopupTaskHost,
    TopmostOverlay,
    AppbarEdge,
    TrafficMonitorLayeredShell
};

struct TaskbarStrategyDef {
    TaskbarEmbedStrategy strategy;
    const char* id;
    const wchar_t* tag;
};

constexpr TaskbarStrategyDef kTaskbarStrategies[] = {
    { TaskbarEmbedStrategy::PopupShellNoOwner, "popup_shell_noowner", L"TB1" },
    { TaskbarEmbedStrategy::PopupShellOwner, "popup_shell_owner", L"TB2" },
    { TaskbarEmbedStrategy::ChildShellSetParent, "child_shell_setparent", L"TB3" },
    { TaskbarEmbedStrategy::CreateChildShell, "create_child_shell", L"TB4" },
    { TaskbarEmbedStrategy::PopupTrayNotify, "popup_traynotify", L"TB5" },
    { TaskbarEmbedStrategy::ChildTrayNotify, "child_traynotify", L"TB6" },
    { TaskbarEmbedStrategy::PopupTaskHost, "popup_taskhost", L"TB7" },
    { TaskbarEmbedStrategy::TopmostOverlay, "topmost_overlay", L"TB8" },
    { TaskbarEmbedStrategy::AppbarEdge, "appbar_edge", L"TB9" },
    { TaskbarEmbedStrategy::TrafficMonitorLayeredShell, "trafficmonitor_layered_shell", L"TB10" }
};

TaskbarEmbedStrategy TaskbarStrategyFromId(const std::string& id) {
    for (const auto& def : kTaskbarStrategies) {
        if (id == def.id) return def.strategy;
    }
    return TaskbarEmbedStrategy::TrafficMonitorLayeredShell;
}

TaskbarEmbedStrategy TaskbarStrategyFromIndex(int index) {
    if (index >= 0 && index < (int)(sizeof(kTaskbarStrategies) / sizeof(kTaskbarStrategies[0])))
        return kTaskbarStrategies[index].strategy;
    return TaskbarEmbedStrategy::TrafficMonitorLayeredShell;
}

const char* TaskbarStrategyId(TaskbarEmbedStrategy strategy) {
    for (const auto& def : kTaskbarStrategies) {
        if (def.strategy == strategy) return def.id;
    }
    return "trafficmonitor_layered_shell";
}

const wchar_t* TaskbarStrategyTag(TaskbarEmbedStrategy strategy) {
    for (const auto& def : kTaskbarStrategies) {
        if (def.strategy == strategy) return def.tag;
    }
    return L"TB10";
}

int TaskbarStrategyIndex(TaskbarEmbedStrategy strategy) {
    for (int i = 0; i < (int)(sizeof(kTaskbarStrategies) / sizeof(kTaskbarStrategies[0])); ++i) {
        if (kTaskbarStrategies[i].strategy == strategy) return i;
    }
    return (int)(sizeof(kTaskbarStrategies) / sizeof(kTaskbarStrategies[0])) - 1;
}

bool StrategyUsesSetParent(TaskbarEmbedStrategy s) {
    return s == TaskbarEmbedStrategy::PopupShellNoOwner ||
           s == TaskbarEmbedStrategy::PopupShellOwner ||
           s == TaskbarEmbedStrategy::ChildShellSetParent ||
           s == TaskbarEmbedStrategy::PopupTrayNotify ||
           s == TaskbarEmbedStrategy::ChildTrayNotify ||
           s == TaskbarEmbedStrategy::PopupTaskHost ||
           s == TaskbarEmbedStrategy::TrafficMonitorLayeredShell;
}

bool StrategyUsesTrueChild(TaskbarEmbedStrategy s) {
    return s == TaskbarEmbedStrategy::ChildShellSetParent ||
           s == TaskbarEmbedStrategy::CreateChildShell ||
           s == TaskbarEmbedStrategy::ChildTrayNotify;
}

bool StrategyUsesScreenCoords(TaskbarEmbedStrategy s) {
    return s == TaskbarEmbedStrategy::TopmostOverlay ||
           s == TaskbarEmbedStrategy::AppbarEdge;
}

bool StrategyUsesLayeredRender(TaskbarEmbedStrategy s) {
    return s == TaskbarEmbedStrategy::TrafficMonitorLayeredShell;
}

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

bool AttachToTaskbarParent(HWND child, HWND parent, TaskbarEmbedStrategy strategy) {
    if (!child || !parent || !IsWindow(child) || !IsWindow(parent)) return false;

    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    if (StrategyUsesTrueChild(strategy)) {
        style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME |
                   WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        style |= WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    } else {
        style &= ~WS_CHILD;
        style |= WS_POPUP | WS_SYSMENU | WS_CLIPSIBLINGS;
    }
    if (!SetWindowLongPtrChecked(child, GWL_STYLE, style)) return false;

    LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;
    if (StrategyUsesLayeredRender(strategy))
        ex |= WS_EX_LAYERED;
    else
        ex &= ~WS_EX_LAYERED;
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

HWND FindFirstChild(HWND parent, const wchar_t* cls) {
    return parent ? FindWindowExW(parent, nullptr, cls, nullptr) : nullptr;
}

HWND FindTaskHost(HWND shell) {
    if (!shell) return nullptr;
    if (HWND bridge = FindFirstChild(shell, L"Windows.UI.Composition.DesktopWindowContentBridge"))
        return bridge;
    HWND bar = FindFirstChild(shell, L"ReBarWindow32");
    if (!bar) bar = FindFirstChild(shell, L"WorkerW");
    if (!bar) return nullptr;
    if (HWND min = FindFirstChild(bar, L"MSTaskSwWClass")) return min;
    if (HWND list = FindFirstChild(bar, L"MSTaskListWClass")) return list;
    return bar;
}

HWND ResolveStrategyParent(HWND shell, TaskbarEmbedStrategy strategy) {
    switch (strategy) {
    case TaskbarEmbedStrategy::PopupTrayNotify:
    case TaskbarEmbedStrategy::ChildTrayNotify:
        return FindFirstChild(shell, L"TrayNotifyWnd");
    case TaskbarEmbedStrategy::PopupTaskHost:
        return FindTaskHost(shell);
    default:
        return shell;
    }
}

bool EnsureAppbarRegistered(HWND hwnd, bool& registered) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    if (registered) return true;
    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = hwnd;
    abd.uCallbackMessage = kTaskbarAppbarCallbackMsg;
    registered = SHAppBarMessage(ABM_NEW, &abd) != FALSE;
    return registered;
}

void UnregisterAppbar(HWND hwnd, bool& registered) {
    if (!registered) return;
    if (hwnd && IsWindow(hwnd)) {
        APPBARDATA abd{};
        abd.cbSize = sizeof(abd);
        abd.hWnd = hwnd;
        SHAppBarMessage(ABM_REMOVE, &abd);
    }
    registered = false;
}

bool TaskbarEdgeFromRect(const RECT& tb, const RECT& mon, UINT& edge) {
    int w = tb.right - tb.left;
    int h = tb.bottom - tb.top;
    if (w >= h) {
        int topDist = abs(tb.top - mon.top);
        int bottomDist = abs(mon.bottom - tb.bottom);
        edge = topDist <= bottomDist ? ABE_TOP : ABE_BOTTOM;
        return true;
    }
    int leftDist = abs(tb.left - mon.left);
    int rightDist = abs(mon.right - tb.right);
    edge = leftDist <= rightDist ? ABE_LEFT : ABE_RIGHT;
    return true;
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
    if (wr.right <= wr.left || wr.bottom <= wr.top) return false;
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);
    if (StrategyUsesLayeredRender(strategy) && !taskbarLayeredReady_) return false;
    return true;
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
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);
    swprintf_s(buf,
        L"[X-TODO taskbar] %s strategy=%s parent=%p band=%p inserted=%d attachErr=%lu "
        L"gp=%p ga=%p visible=%d style=%p ex=%p parentRect=(%ld,%ld,%ld,%ld) "
        L"bandRect=(%ld,%ld,%ld,%ld) client=(%ld,%ld)\n",
        tag ? tag : L"",
        TaskbarStrategyTag(strategy),
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
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromId(ui_.taskbarStrategy);
    ui_.taskbarStrategy = TaskbarStrategyId(strategy);
    int strategyIndex = TaskbarStrategyIndex(strategy);

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
    HWND shellHost = nullptr;
    if (!ui_.taskbarMonitor.empty()) {
        std::wstring want = Utf8ToW(ui_.taskbarMonitor);
        for (auto& h : hosts) if (h.dev == want) { shellHost = h.hwnd; break; }
    }
    if (!shellHost && geom_.valid) {
        POINT c{ geom_.x + geom_.w / 2, geom_.y + geom_.h / 2 };
        HMONITOR m = MonitorFromPoint(c, MONITOR_DEFAULTTONULL);
        if (m) for (auto& h : hosts) if (h.mon == m) { shellHost = h.hwnd; break; }
    }
    if (!shellHost) {
        POINT mp;
        if (GetCursorPos(&mp)) {
            HMONITOR m = MonitorFromPoint(mp, MONITOR_DEFAULTTONULL);
            if (m) for (auto& h : hosts) if (h.mon == m) { shellHost = h.hwnd; break; }
        }
    }
    if (!shellHost && primary)
        for (auto& h : hosts) if (h.hwnd == primary) { shellHost = h.hwnd; break; }
    if (!shellHost) shellHost = hosts.front().hwnd;

    HWND strategyParent = ResolveStrategyParent(shellHost, strategy);
    if (!strategyParent) return TaskbarLayoutResult::TransientUnavailable;

    // 任务栏 host 变化（含 Explorer 重启）：销毁旧 band 后重建。
    if (taskbarHwnd_ &&
        (taskbarParent_ != strategyParent || taskbarActiveStrategy_ != strategyIndex))
        DestroyTaskbarBand();

    taskbarParent_ = strategyParent;
    taskbarActiveStrategy_ = strategyIndex;
    if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) {
        taskbarInserted_ = false;
        taskbarLayeredReady_ = false;
        taskbarAttachError_ = ERROR_SUCCESS;
        DWORD exStyle = WS_EX_TOOLWINDOW;
        DWORD style = WS_POPUP | WS_SYSMENU | WS_CLIPSIBLINGS;
        HWND createParent = nullptr;

        if (strategy == TaskbarEmbedStrategy::PopupShellOwner)
            createParent = hwnd_;
        if (strategy == TaskbarEmbedStrategy::TrafficMonitorLayeredShell) {
            exStyle |= WS_EX_LAYERED;
            createParent = hwnd_;
        }
        if (strategy == TaskbarEmbedStrategy::CreateChildShell) {
            style = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
            createParent = taskbarParent_;
        }
        if (strategy == TaskbarEmbedStrategy::TopmostOverlay) {
            exStyle |= WS_EX_TOPMOST | WS_EX_NOACTIVATE;
            createParent = nullptr;
        }
        if (strategy == TaskbarEmbedStrategy::AppbarEdge) {
            exStyle |= WS_EX_TOPMOST;
            createParent = nullptr;
        }

        taskbarHwnd_ = CreateWindowExW(
            exStyle,
            kTaskbarBandClass, L"",
            style,
            0, 0, 10, 10,
            createParent, nullptr, GetModuleHandleW(nullptr), this);
        if (!taskbarHwnd_) { taskbarParent_ = nullptr; return TaskbarLayoutResult::Fatal; }
        TraceTaskbarBand(L"after-create");
    }

    if (StrategyUsesSetParent(strategy) &&
        !AttachToTaskbarParent(taskbarHwnd_, taskbarParent_, strategy)) {
        taskbarInserted_ = false;
        taskbarAttachError_ = GetLastError();
        TraceTaskbarBand(L"attach-failed");
        DestroyTaskbarBand();
        return TaskbarLayoutResult::TransientUnavailable;
    }
    if (strategy == TaskbarEmbedStrategy::AppbarEdge &&
        !EnsureAppbarRegistered(taskbarHwnd_, taskbarAppbarRegistered_)) {
        taskbarInserted_ = false;
        taskbarAttachError_ = GetLastError();
        TraceTaskbarBand(L"appbar-new-failed");
        DestroyTaskbarBand();
        return TaskbarLayoutResult::TransientUnavailable;
    }
    taskbarInserted_ = true;
    taskbarAttachError_ = ERROR_SUCCESS;
    TraceTaskbarBand(L"after-setparent");

    TaskbarLayoutResult r = LayoutTaskbarBand();
    if (r == TaskbarLayoutResult::Fatal) { DestroyTaskbarBand(); return TaskbarLayoutResult::TransientUnavailable; }
    TraceTaskbarBand(L"after-layout");
    if (r == TaskbarLayoutResult::Ok && StrategyUsesLayeredRender(strategy) &&
        !RenderLayeredTaskbarBand()) {
        TraceTaskbarBand(L"layered-render-failed");
        ShowWindow(taskbarHwnd_, SW_HIDE);
        return TaskbarLayoutResult::TransientUnavailable;
    }

    for (auto& h : hosts) if (h.hwnd == shellHost) { ui_.taskbarMonitor = WToUtf8(h.dev); break; }
    if (r == TaskbarLayoutResult::TransientUnavailable) {
        // 自动隐藏 / Explorer 过渡：不显示未布局的 band，由调用方决定是否重试。
        KillTimer(hwnd_, kTaskbarRefreshTimerId);
        ShowWindow(taskbarHwnd_, SW_HIDE);
    } else {
        int showCmd = StrategyUsesScreenCoords(strategy) ? SW_SHOWNOACTIVATE : SW_SHOW;
        HWND z = strategy == TaskbarEmbedStrategy::TopmostOverlay ||
                 strategy == TaskbarEmbedStrategy::AppbarEdge ? HWND_TOPMOST : HWND_TOP;
        ShowWindow(taskbarHwnd_, showCmd);
        SetWindowPos(taskbarHwnd_, z, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (StrategyUsesLayeredRender(strategy))
            RenderLayeredTaskbarBand();
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
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);

    if (strategy == TaskbarEmbedStrategy::AppbarEdge) {
        RECT tb{};
        if (!GetWindowRect(taskbarParent_, &tb)) return TaskbarLayoutResult::TransientUnavailable;
        HMONITOR mon = MonitorFromWindow(taskbarParent_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (!mon || !GetMonitorInfoW(mon, &mi)) return TaskbarLayoutResult::TransientUnavailable;
        UINT edge = ABE_BOTTOM;
        TaskbarEdgeFromRect(tb, mi.rcMonitor, edge);

        UINT dpi = GetDpiForWindow(taskbarParent_); if (!dpi) dpi = 96;
        auto SC = [&](int v) { return MulDiv(v, dpi, 96); };
        int w = ClampI(SC(ui_.taskbarWidth), SC(120), SC(520));
        int h = SC(34);

        RECT rc{};
        if (edge == ABE_TOP || edge == ABE_BOTTOM) {
            int rightReserve = SC(88);
            int right = mi.rcMonitor.right - rightReserve;
            if (right - w < mi.rcMonitor.left) right = mi.rcMonitor.right;
            int left = ClampI(right - w, mi.rcMonitor.left, mi.rcMonitor.right - w);
            rc = RECT{ left, edge == ABE_TOP ? mi.rcMonitor.top : mi.rcMonitor.bottom - h,
                       left + w, edge == ABE_TOP ? mi.rcMonitor.top + h : mi.rcMonitor.bottom };
        } else {
            int x = edge == ABE_LEFT ? mi.rcMonitor.left : mi.rcMonitor.right - h;
            int y = mi.rcMonitor.bottom - SC(220);
            if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;
            rc = RECT{ x, y, x + h, y + SC(180) };
        }

        APPBARDATA abd{};
        abd.cbSize = sizeof(abd);
        abd.hWnd = taskbarHwnd_;
        abd.uEdge = edge;
        abd.rc = rc;
        SHAppBarMessage(ABM_QUERYPOS, &abd);
        if (edge == ABE_TOP) abd.rc.bottom = abd.rc.top + h;
        else if (edge == ABE_BOTTOM) abd.rc.top = abd.rc.bottom - h;
        else if (edge == ABE_LEFT) abd.rc.right = abd.rc.left + h;
        else if (edge == ABE_RIGHT) abd.rc.left = abd.rc.right - h;
        SHAppBarMessage(ABM_SETPOS, &abd);
        SetWindowPos(taskbarHwnd_, HWND_TOPMOST,
            abd.rc.left, abd.rc.top, abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);

        POINT tl{ abd.rc.left, abd.rc.top }, br{ abd.rc.right, abd.rc.bottom };
        ScreenToClient(taskbarParent_, &tl);
        ScreenToClient(taskbarParent_, &br);
        taskbarBandRect_ = RECT{ tl.x, tl.y, br.x, br.y };
        return TaskbarLayoutResult::Ok;
    }

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
        bool parentIsShell = strategy == TaskbarEmbedStrategy::PopupShellNoOwner ||
                             strategy == TaskbarEmbedStrategy::PopupShellOwner ||
                             strategy == TaskbarEmbedStrategy::ChildShellSetParent ||
                             strategy == TaskbarEmbedStrategy::CreateChildShell ||
                             strategy == TaskbarEmbedStrategy::TopmostOverlay;
        int minW = parentIsShell ? SC(96) : SC(28);
        int maxW = SC(520);
        int maxAllowed = parentW - 2 * margin;
        if (maxAllowed > maxW) maxAllowed = maxW;
        if (maxAllowed < SC(20)) return TaskbarLayoutResult::TransientUnavailable;
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

        int x = 0;
        if (!parentIsShell) {
            x = ClampI((parentW - w) / 2, margin, parentW - margin - w);
        } else {
            bool requestLeft = ui_.taskbarDockT < 0.5;
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

    int x = taskbarBandRect_.left;
    int y = taskbarBandRect_.top;
    HWND z = HWND_TOP;
    if (StrategyUsesScreenCoords(strategy)) {
        POINT origin{ 0, 0 };
        if (!ClientToScreen(taskbarParent_, &origin)) return TaskbarLayoutResult::TransientUnavailable;
        x += origin.x;
        y += origin.y;
        z = HWND_TOPMOST;
    }

    SetWindowPos(taskbarHwnd_, z, x, y, bw, bh,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return TaskbarLayoutResult::Ok;
}

void MainWindow::DrawTaskbarBandContents(HDC mem, const RECT& rc) {
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (!mem || w <= 0 || h <= 0) return;

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
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);
    std::wstring prefix = L"[";
    prefix += TaskbarStrategyTag(strategy);
    prefix += L"] ";
    if (active > 0) {
        int idx = ClampI(taskbarPreviewIndex_, 0, active - 1);
        text = prefix + L"(" + std::to_wstring(active) + L") " + model_.Items()[idx].text;
    } else {
        text = prefix + T(Str::TaskbarAllDone, lang_);
    }

    UINT dpi = GetDpiForWindow(taskbarParent_ ? taskbarParent_ : (taskbarHwnd_ ? taskbarHwnd_ : hwnd_));
    if (!dpi) dpi = 96;
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
}

void MainWindow::PaintTaskbarBand(HWND hwnd) {
    TraceTaskbarBand(L"paint");

    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);
    if (StrategyUsesLayeredRender(strategy)) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        RenderLayeredTaskbarBand();
        return;
    }

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    HDC mem = CreateCompatibleDC(hdc);
    if (!mem) {
        EndPaint(hwnd, &ps);
        return;
    }
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    if (!bmp) {
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    DrawTaskbarBandContents(mem, rc);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

bool MainWindow::RenderLayeredTaskbarBand() {
    taskbarLayeredReady_ = false;
    if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) return false;

    RECT rc{};
    if (!GetClientRect(taskbarHwnd_, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, dib);
    RECT drawRc{ 0, 0, w, h };
    DrawTaskbarBandContents(mem, drawRc);

    BYTE* px = static_cast<BYTE*>(bits);
    int pixelCount = w * h;
    for (int i = 0; i < pixelCount; ++i)
        px[i * 4 + 3] = 255;

    POINT src{ 0, 0 };
    SIZE size{ w, h };
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UPDATELAYEREDWINDOWINFO info{};
    info.cbSize = sizeof(info);
    info.hdcDst = screen;
    info.psize = &size;
    info.hdcSrc = mem;
    info.pptSrc = &src;
    info.pblend = &blend;
    info.dwFlags = ULW_ALPHA;

    SetLastError(ERROR_SUCCESS);
    BOOL ok = UpdateLayeredWindowIndirect(taskbarHwnd_, &info);
    if (ok) {
        taskbarLayeredReady_ = true;
        taskbarAttachError_ = ERROR_SUCCESS;
    } else {
        taskbarAttachError_ = GetLastError();
    }

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok != FALSE;
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
            InvalidateTaskbarBand();
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
        InvalidateTaskbarBand();
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        taskbarPressed_ = true;
        taskbarDragging_ = false;
        GetCursorPos(&taskbarPressScreen_);
        taskbarPressOffset_ = POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        InvalidateTaskbarBand();
        return 0;
    case WM_LBUTTONUP: {
        bool wasDrag = taskbarDragging_;
        if (GetCapture() == hwnd) ReleaseCapture();
        taskbarPressed_ = false;
        taskbarDragging_ = false;
        InvalidateTaskbarBand();
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
        InvalidateTaskbarBand();
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        InvalidateTaskbarBand();
        return 0;
    case WM_NCDESTROY:
        if (hwnd == taskbarHwnd_) {
            taskbarHwnd_ = nullptr;
            taskbarParent_ = nullptr;
            taskbarInserted_ = false;
            taskbarAttachError_ = ERROR_SUCCESS;
            taskbarAppbarRegistered_ = false;
            taskbarActiveStrategy_ = -1;
            taskbarLayeredReady_ = false;
            taskbarHover_ = taskbarPressed_ = taskbarDragging_ = false;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MainWindow::DestroyTaskbarBand() {
    KillTimer(hwnd_, kTaskbarRefreshTimerId);
    UnregisterAppbar(taskbarHwnd_, taskbarAppbarRegistered_);
    if (taskbarHwnd_ && IsWindow(taskbarHwnd_)) {
        HWND h = taskbarHwnd_;
        taskbarHwnd_ = nullptr; // 先清，避免 WM_NCDESTROY 重入
        DestroyWindow(h);
    }
    taskbarHwnd_ = nullptr;
    taskbarParent_ = nullptr;
    taskbarInserted_ = false;
    taskbarAttachError_ = ERROR_SUCCESS;
    taskbarActiveStrategy_ = -1;
    taskbarLayeredReady_ = false;
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

void MainWindow::SetTaskbarStrategy(const std::string& strategy) {
    TaskbarEmbedStrategy parsed = TaskbarStrategyFromId(strategy);
    const char* id = TaskbarStrategyId(parsed);
    if (ui_.taskbarStrategy == id && mountMode_ != MountMode::Taskbar) return;
    ui_.taskbarStrategy = id;

    if (mountMode_ == MountMode::Taskbar) {
        bool mainWasHidden = !IsWindowVisible(hwnd_);
        DestroyTaskbarBand();
        TaskbarLayoutResult r = EnsureTaskbarBand();
        if (r == TaskbarLayoutResult::Ok && IsTaskbarBandReady()) {
            ShowWindow(hwnd_, SW_HIDE);
            taskbarRetryHideMain_ = false;
        } else {
            ShowWindow(hwnd_, SW_SHOW);
            ScheduleTaskbarRetry(mainWasHidden);
        }
    }
    ScheduleSave();
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
    if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) return;
    TaskbarEmbedStrategy strategy = TaskbarStrategyFromIndex(taskbarActiveStrategy_);
    if (StrategyUsesLayeredRender(strategy))
        RenderLayeredTaskbarBand();
    else
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
