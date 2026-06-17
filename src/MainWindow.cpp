#include "MainWindow.h"
#include "Autostart.h"
#include "Theme.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <cwchar>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {
template <class T> void SafeRelease(T** p) {
    if (*p) { (*p)->Release(); *p = nullptr; }
}

// 显示器 szDevice（宽字符）转 UTF-8（size-then-resize，无越界）
std::string MonitorDeviceUtf8(const wchar_t* szDevice) {
    int n = WideCharToMultiByte(CP_UTF8, 0, szDevice, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string dev((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, szDevice, -1, dev.data(), n, nullptr, nullptr);
    dev.resize((size_t)n - 1);
    return dev;
}

int ClampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void CenterWindowOverOwner(HWND dialog, HWND owner) {
    RECT dr{}, ownerRect{};
    if (!GetWindowRect(dialog, &dr) || !GetWindowRect(owner, &ownerRect)) return;

    int dw = dr.right - dr.left;
    int dh = dr.bottom - dr.top;
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - dw) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - dh) / 2;

    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        x = ClampInt(x, mi.rcWork.left, mi.rcWork.right - dw);
        y = ClampInt(y, mi.rcWork.top, mi.rcWork.bottom - dh);
    }
    SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

struct MessageBoxCentering {
    HWND owner = nullptr;
    HHOOK hook = nullptr;
};

thread_local MessageBoxCentering* g_messageBoxCentering = nullptr;

LRESULT CALLBACK CenterMessageBoxHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE && g_messageBoxCentering && g_messageBoxCentering->owner) {
        CenterWindowOverOwner(reinterpret_cast<HWND>(wp), g_messageBoxCentering->owner);
        if (g_messageBoxCentering->hook) {
            UnhookWindowsHookEx(g_messageBoxCentering->hook);
            g_messageBoxCentering->hook = nullptr;
        }
        return 0;
    }
    return CallNextHookEx(g_messageBoxCentering ? g_messageBoxCentering->hook : nullptr, code, wp, lp);
}

int CenteredMessageBox(HWND owner, const wchar_t* text, const wchar_t* caption, UINT flags) {
    MessageBoxCentering centering{ owner, nullptr };
    MessageBoxCentering* previous = g_messageBoxCentering;
    g_messageBoxCentering = &centering;
    centering.hook = SetWindowsHookExW(WH_CBT, CenterMessageBoxHook, nullptr, GetCurrentThreadId());
    int result = MessageBoxW(owner, text, caption, flags);
    if (centering.hook) UnhookWindowsHookEx(centering.hook);
    g_messageBoxCentering = previous;
    return result;
}
} // namespace

// ——————————————————————————— 生命周期 ———————————————————————————

bool MainWindow::RegisterWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc   = WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // 全程 Direct2D 绘制
    wc.lpszClassName = kWindowClass;
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create() {
    if (!RegisterWindowClass()) return false;

    LoadResult loadResult = Store::Load(model_, geom_, ui_);

    // 校验持久化几何：尺寸合理且至少落在某个显示器上，否则回退默认位置（防离屏/零尺寸找不回）
    int w = 300, h = 380;
    bool geomOk = false;
    if (geom_.valid && geom_.w >= 220 && geom_.w <= 4000 && geom_.h >= 160 && geom_.h <= 4000) {
        RECT wr{ geom_.x, geom_.y, geom_.x + geom_.w, geom_.y + geom_.h };
        if (MonitorFromRect(&wr, MONITOR_DEFAULTTONULL) != nullptr) {
            w = geom_.w; h = geom_.h;
            geomOk = true;
        }
    }
    int x, y;
    if (geomOk) {
        x = geom_.x; y = geom_.y;
    } else {
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        x = wa.right - w - 40;
        y = wa.top + 60;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW, kWindowClass, L"x-todo", WS_POPUP | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    dpi_ = GetDpiForWindow(hwnd_);
    geom_ = { x, y, w, h, true }; // 回写校验后的几何，供形态切换/保存复用

    if (!CreateDeviceIndependentResources()) return false;

    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    lang_ = ui_.lang == "zh" ? Lang::Zh
          : ui_.lang == "en" ? Lang::En
          : SystemDefaultLang();
    capsuleStyle_ = ui_.capsuleStyle == "dot" ? CapsuleStyle::Dot : CapsuleStyle::Slim;
    mountMode_ = ui_.mountMode == "desktop" ? MountMode::Desktop
               : ui_.mountMode == "capsule" ? MountMode::Capsule
               : MountMode::Normal;
    ApplyMountMode(); // 应用持久化形态（含置顶 / 布局）

    if (loadResult == LoadResult::Failed) // 数据读取失败：告知用户（原文件已备份）
        MessageBoxW(hwnd_, T(Str::LoadFailedMsg, lang_), L"X-TODO", MB_OK | MB_ICONWARNING);
    return true;
}

void MainWindow::Show(bool expandCapsule) {
    ShowWindow(hwnd_, SW_SHOW);
    if (expandCapsule && mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_)
        StartCapsuleAnim(true); // 主动唤起（托盘 / 菜单 / 第二实例）时滑出
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        AddTrayIcon(); // Explorer 重启后重新登记托盘图标
        return 0;
    }
    switch (msg) {
    case WM_PAINT:
        if (Render()) {
            ValidateRect(hwnd_, nullptr);
        } else {
            // 设备丢失：不校验绘制区域，促使下一次 WM_PAINT 重建并重绘，避免留白
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;

    case WM_DISPLAYCHANGE:
        if (mountMode_ == MountMode::Capsule && !capsuleDragging_) {
            MONITORINFOEXW mi{};
            if (DockMonitorInfo(mi)) { // 原显示器可能失效已回退，持久化实际所在
                std::string dev = MonitorDeviceUtf8(mi.szDevice);
                if (!dev.empty() && dev != ui_.capsuleMonitor) { ui_.capsuleMonitor = dev; ScheduleSave(); }
            }
            RECT t = capsuleExpanded_ ? ExpandedTargetRect() : CapsuleTargetRect();
            SetWindowPos(hwnd_, HWND_TOPMOST, t.left, t.top,
                         t.right - t.left, t.bottom - t.top, SWP_NOACTIVATE);
            UpdateLayeredState(); // 顺带刷新 alpha 与圆点区域
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_SIZE:
        Resize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_EXITSIZEMOVE:
        ScheduleSave(); // 移动 / 缩放结束后持久化窗口几何
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        RECT target{};
        if (mountMode_ == MountMode::Capsule && !capsuleDragging_) {
            target = capsuleExpanded_ ? ExpandedTargetRect() : CapsuleTargetRect();
        } else {
            RECT* prc = reinterpret_cast<RECT*>(lp);
            target = *prc;
        }
        SetWindowPos(hwnd_, nullptr, target.left, target.top,
                     target.right - target.left, target.bottom - target.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        DiscardDeviceResources(); // 下次绘制按新 DPI 重建文本格式
        RebuildLayout();
        if (editing()) { // 编辑中：按新 DPI 重建编辑字体并重定位
            if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
            LOGFONTW lf{};
            lf.lfHeight  = -(LONG)S(Theme::kFontSize);
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
            editFont_ = CreateFontIndirectW(&lf);
            SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
            LayoutEditBox();
        }
        UpdateLayeredState();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        OnLButtonDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        if (GetCapture() == hwnd_) ReleaseCapture(); // 先处理逻辑松手再释放，避免自触发 WM_CAPTURECHANGED 误清状态
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&tme);
        if (capsuleShrunk() && !capsulePressing_ && !capsuleHover_) {
            capsuleHover_ = true; // 折叠胶囊：鼠标进入仅视觉提示，不展开
            UpdateLayeredState();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        OnMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_LBUTTON) != 0);
        return 0;
    }

    case WM_MOUSELEAVE:
        OnMouseLeave();
        if (capsuleHover_) { // 折叠胶囊：离开则取消视觉提示
            capsuleHover_ = false;
            UpdateLayeredState();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_ && !editing())
            StartCapsuleAnim(false); // 鼠标离开展开的便签：滑回胶囊
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
        return 0;

    case WM_CAPTURECHANGED: // capture 被夺：收尾未完成的胶囊按压 / 行拖动
        if ((HWND)lp != hwnd_) {
            if (capsulePressing_) CancelCapsulePress();
            if (dragging_) { dragging_ = false; dragFrom_ = dragInsert_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); }
        }
        return 0;

    case WM_NCHITTEST:
        return OnNcHitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));

    case WM_GETMINMAXINFO: {
        auto mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = (LONG)S(220);
        mmi->ptMinTrackSize.y = (LONG)S(160);
        return 0;
    }

    case WM_WINDOWPOSCHANGING:
        if (mountMode_ == MountMode::Desktop) // 挂桌面：始终保持在最底层
            reinterpret_cast<WINDOWPOS*>(lp)->hwndInsertAfter = HWND_BOTTOM;
        break;

    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONDBLCLK) Show();
        else if (LOWORD(lp) == WM_RBUTTONUP) ShowTrayMenu();
        return 0;

    case WM_APP_SHOW:
        Show();
        return 0;

    case WM_TIMER:
        if (wp == kSaveTimerId) {
            KillTimer(hwnd_, kSaveTimerId);
            savePending_ = false;
            SaveNow();
        } else if (wp == kAnimTimerId) {
            OnAnimTick();
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(0xFB, 0xF7, 0xEC));
        SetTextColor(hdc, RGB(0x33, 0x31, 0x2C));
        if (!editBg_) editBg_ = CreateSolidBrush(RGB(0xFB, 0xF7, 0xEC));
        return (LRESULT)editBg_;
    }

    case WM_CLOSE:
        HideToTray();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ——————————————————————————— Direct2D 资源 ———————————————————————————

bool MainWindow::CreateDeviceIndependentResources() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&dwrite_))))
        return false;
    return true;
}

bool MainWindow::CreateDeviceResources() {
    if (rt_) return true;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    if (FAILED(d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, size),
            &rt_))) {
        return false;
    }
    // 固定为 96 DPI：1 DIP = 1 物理像素，配合 S() 手动按窗口 DPI 缩放，避免双重缩放
    rt_->SetDpi(96.0f, 96.0f);

    // 逐项检查并在失败时整体回滚，避免半初始化的 rt_ 残留导致后续空指针解引用
    if (FAILED(rt_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush_))) {
        DiscardDeviceResources();
        return false;
    }
    if (FAILED(dwrite_->CreateTextFormat(
            Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            S(Theme::kFontSize), L"", &textFormat_))) {
        DiscardDeviceResources();
        return false;
    }
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (FAILED(dwrite_->CreateTextFormat(
            Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            S(Theme::kSmallFont), L"", &smallFormat_))) {
        DiscardDeviceResources();
        return false;
    }
    smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return true;
}

void MainWindow::DiscardDeviceResources() {
    for (auto& r : rows_) SafeRelease(&r.strikeLayout); // 删除线布局随设备资源一并失效，下次绘制重建
    SafeRelease(&smallFormat_);
    SafeRelease(&textFormat_);
    SafeRelease(&brush_);
    SafeRelease(&rt_);
}

void MainWindow::Resize(UINT w, UINT h) {
    if (rt_) rt_->Resize(D2D1::SizeU(w, h));
    RebuildLayout();
    ClampScroll();
    if (editing()) LayoutEditBox();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ——————————————————————————— 托盘 ———————————————————————————

HICON MainWindow::CreateTrayIconHandle() {
    const int sz = 32;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = sz;
    bi.bmiHeader.biHeight      = -sz; // 自上而下
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldColor = (HBITMAP)SelectObject(dc, color);

    RECT full{ 0, 0, sz, sz };
    HBRUSH paper = CreateSolidBrush(RGB(0xFB, 0xF7, 0xEC));
    ::FillRect(dc, &full, paper); // 显式 GDI 版（避免与成员 FillRect 撞名）
    DeleteObject(paper);

    HPEN pen = CreatePen(PS_SOLID, 3, RGB(0x5F, 0x7A, 0x4F)); // 对勾
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, 9, 17, nullptr);
    LineTo(dc, 14, 22);
    LineTo(dc, 23, 11);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    SelectObject(dc, oldColor);

    // 单色掩码：圆角矩形区域不透明，其余透明
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, nullptr);
    HDC mdc = CreateCompatibleDC(screen);
    HBITMAP oldMask = (HBITMAP)SelectObject(mdc, mask);
    PatBlt(mdc, 0, 0, sz, sz, WHITENESS); // 全 1 = 透明
    SelectObject(mdc, GetStockObject(BLACK_BRUSH));
    SelectObject(mdc, GetStockObject(BLACK_PEN));
    RoundRect(mdc, 3, 3, sz - 3, sz - 3, 9, 9); // 置 0 = 不透明
    SelectObject(mdc, oldMask);
    DeleteDC(mdc);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

bool MainWindow::AddTrayIcon() {
    if (nid_.hIcon) DestroyIcon(nid_.hIcon); // 重建（Explorer 重启）前清理旧图标
    nid_ = {};
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = hwnd_;
    nid_.uID              = 1;
    nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY;
    nid_.hIcon            = CreateTrayIconHandle();
    wcscpy_s(nid_.szTip, L"X-TODO");
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (!trayAdded_ && nid_.hIcon) { // 添加失败立即释放图标，避免泄漏
        DestroyIcon(nid_.hIcon);
        nid_.hIcon = nullptr;
    }
    return trayAdded_;
}

void MainWindow::RemoveTrayIcon() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        trayAdded_ = false;
    }
    if (nid_.hIcon) {
        DestroyIcon(nid_.hIcon);
        nid_.hIcon = nullptr;
    }
}

void MainWindow::AppendMenuItems(HMENU menu) {
    AppendMenuW(menu, MF_STRING | (mountMode_ == MountMode::Normal  ? MF_CHECKED : 0), 10, T(Str::ModeNormal, lang_));
    AppendMenuW(menu, MF_STRING | (mountMode_ == MountMode::Desktop ? MF_CHECKED : 0), 11, T(Str::ModeDesktop, lang_));

    // 侧边胶囊：子菜单选外观样式；选任一即进入胶囊模式并设样式
    HMENU styleMenu = CreatePopupMenu();
    const bool inCapsule = (mountMode_ == MountMode::Capsule);
    AppendMenuW(styleMenu, MF_STRING | (inCapsule && capsuleStyle_ == CapsuleStyle::Slim ? MF_CHECKED : 0), 30, T(Str::StyleSlim, lang_));
    AppendMenuW(styleMenu, MF_STRING | (inCapsule && capsuleStyle_ == CapsuleStyle::Dot  ? MF_CHECKED : 0), 31, T(Str::StyleDot, lang_));
    AppendMenuW(menu, MF_POPUP | (inCapsule ? MF_CHECKED : 0), (UINT_PTR)styleMenu, T(Str::ModeCapsule, lang_));

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 20, T(Str::ToggleLang, lang_));
    AppendMenuW(menu, MF_STRING | (Autostart::IsEnabled() ? MF_CHECKED : 0), 2, T(Str::Autostart, lang_));
}

void MainWindow::HandleMenuCommand(UINT cmd) {
    switch (cmd) {
        case 1:  Show();                              break;
        case 2:  ToggleAutostart();                   break;
        case 3:  ExitApp();                           break;
        case 10: SetMountMode(MountMode::Normal);     break;
        case 11: SetMountMode(MountMode::Desktop);    break;
        case 30: SetCapsuleStyle(CapsuleStyle::Slim);
                 if (mountMode_ != MountMode::Capsule) SetMountMode(MountMode::Capsule); break;
        case 31: SetCapsuleStyle(CapsuleStyle::Dot);
                 if (mountMode_ != MountMode::Capsule) SetMountMode(MountMode::Capsule); break;
        case 20: SetLanguage(lang_ == Lang::Zh ? Lang::En : Lang::Zh); break;
        default: break;
    }
}

void MainWindow::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, T(Str::Show, lang_));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuItems(menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, T(Str::Exit, lang_));

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_); // 经典坑：否则菜单不会自动关闭
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    HandleMenuCommand(cmd);
}

void MainWindow::ShowTitleMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuItems(menu);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, T(Str::Exit, lang_));

    POINT pt{ (LONG)menuRect_.left, (LONG)menuRect_.bottom }; // 弹在菜单按钮下方
    ClientToScreen(hwnd_, &pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    HandleMenuCommand(cmd);
}

void MainWindow::SetLanguage(Lang lang) {
    if (lang == lang_) return;
    lang_ = lang;
    ui_.lang = (lang == Lang::Zh) ? "zh" : "en";
    RebuildLayout();
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ——————————————————————————— 挂载形态 ———————————————————————————

void MainWindow::SetMountMode(MountMode m) {
    if (m == mountMode_) return;
    if (editing()) CommitEdit(false);
    if (!capsuleShrunk()) CaptureVisibleGeometry();
    mountMode_ = m;
    ui_.mountMode = (m == MountMode::Desktop) ? "desktop"
                  : (m == MountMode::Capsule) ? "capsule" : "normal";
    ApplyMountMode();
    ScheduleSave();
}

void MainWindow::ApplyMountMode() {
    KillTimer(hwnd_, kAnimTimerId);
    animActive_ = false;
    capsuleExpanded_ = false;

    if (mountMode_ == MountMode::Desktop) {
        // 沉到最底层贴桌面：不挡工作窗口、看桌面时可见，且不依赖脆弱又挑版本的 WorkerW 嵌入
        SetWindowPos(hwnd_, HWND_BOTTOM, geom_.x, geom_.y, geom_.w, geom_.h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (mountMode_ == MountMode::Capsule) {
        capsuleExpanded_ = false; // 进入胶囊即折叠（冷启动 / 切换都收起）
        RECT c = CapsuleTargetRect();
        SetWindowPos(hwnd_, HWND_TOPMOST, c.left, c.top,
                     c.right - c.left, c.bottom - c.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else { // Normal
        SetWindowPos(hwnd_, ui_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     geom_.x, geom_.y, geom_.w, geom_.h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    UpdateLayeredState();
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT MainWindow::CapsuleTargetRect() const {
    MONITORINFOEXW mi{};
    RECT wa{};
    if (DockMonitorInfo(mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int cw = (int)S(capsuleStyle_ == CapsuleStyle::Dot ? Theme::kCapsuleDot : Theme::kCapsuleSlimW);
    int ch = (int)S(capsuleStyle_ == CapsuleStyle::Dot ? Theme::kCapsuleDot : Theme::kCapsuleSlimH);
    int workW = wa.right - wa.left, workH = wa.bottom - wa.top;
    if (workW > 0 && cw > workW) cw = workW; // 防胶囊大于工作区致 ClampInt 边界反转
    if (workH > 0 && ch > workH) ch = workH;
    int centerY = wa.top + (int)(CapsuleDockT() * workH + 0.5);
    int y = ClampInt(centerY - ch / 2, wa.top, wa.bottom - ch);
    int x = (CapsuleDockEdge() == DockEdge::Left) ? wa.left : wa.right - cw;
    return RECT{ x, y, x + cw, y + ch };
}

RECT MainWindow::ExpandedTargetRect() const {
    MONITORINFOEXW mi{};
    RECT wa{};
    if (DockMonitorInfo(mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    int w = geom_.valid ? geom_.w : 300;
    int h = geom_.valid ? geom_.h : 380;
    int workW = wa.right - wa.left, workH = wa.bottom - wa.top;
    if (workW > 0 && w > workW) w = workW;
    if (workH > 0 && h > workH) h = workH;
    int centerY = wa.top + (int)(CapsuleDockT() * workH + 0.5);
    int y = ClampInt(centerY - h / 2, wa.top, wa.bottom - h);
    int x = (CapsuleDockEdge() == DockEdge::Left) ? wa.left : wa.right - w;
    return RECT{ x, y, x + w, y + h };
}

DockEdge MainWindow::CapsuleDockEdge() const {
    return ui_.capsuleDockEdge == "left" ? DockEdge::Left : DockEdge::Right;
}

double MainWindow::CapsuleDockT() const {
    double t = ui_.capsuleDockT;
    if (!(t >= 0.0)) t = 0.0; // NaN 兜底（NaN 比较恒 false）
    if (t > 1.0) t = 1.0;
    return t;
}

// 按存档 szDevice 查显示器；找不到返回 nullptr（交由 DockMonitorInfo 回退就近）
HMONITOR MainWindow::FindMonitorByDevice(const std::string& device) const {
    if (device.empty()) return nullptr;
    struct Ctx { const std::string* want; HMONITOR found; } ctx{ &device, nullptr };
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(h, (MONITORINFO*)&mi)) {
                if (MonitorDeviceUtf8(mi.szDevice) == *c->want) { c->found = h; return FALSE; }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

bool MainWindow::DockMonitorInfo(MONITORINFOEXW& mi) const {
    HMONITOR mon = FindMonitorByDevice(ui_.capsuleMonitor);
    if (!mon) mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    mi = {};
    mi.cbSize = sizeof(mi);
    return mon && GetMonitorInfoW(mon, (MONITORINFO*)&mi);
}

void MainWindow::SetCapsuleStyle(CapsuleStyle s) {
    if (s == capsuleStyle_) return;
    capsuleStyle_ = s;
    ui_.capsuleStyle = (s == CapsuleStyle::Dot) ? "dot" : "slim";
    if (mountMode_ == MountMode::Capsule && !animActive_) {
        RECT t = capsuleExpanded_ ? ExpandedTargetRect() : CapsuleTargetRect();
        SetWindowPos(hwnd_, HWND_TOPMOST, t.left, t.top,
                     t.right - t.left, t.bottom - t.top, SWP_NOACTIVATE);
        UpdateLayeredState(); // resize 后再算 region/alpha，避免用旧样式尺寸的椭圆
    }
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// 按当前样式 / 折叠 / hover 维护整窗 alpha：Slim 折叠静止半透明，hover/展开/动画/Dot 不透明
void MainWindow::UpdateLayeredState() {
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const bool wantLayered = (mountMode_ == MountMode::Capsule && capsuleStyle_ == CapsuleStyle::Slim);
    const bool hasLayered = (ex & WS_EX_LAYERED) != 0;

    if (wantLayered) {
        if (!hasLayered) {
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        BYTE alpha = (capsuleShrunk() && !capsuleHover_)
                     ? (BYTE)(Theme::kCapsuleSlimAlpha * 255.0f) : 255;
        SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
    } else if (hasLayered) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    UpdateCapsuleRegion(); // 形态 / 样式 / 折叠变化时同步圆点的椭圆区域
}

// 圆点折叠态用椭圆窗口区域确保圆形（确定裁剪，不依赖 DWM 圆角 hint）
void MainWindow::UpdateCapsuleRegion() {
    const bool wantCircle = (mountMode_ == MountMode::Capsule
                             && capsuleStyle_ == CapsuleStyle::Dot && capsuleShrunk());
    if (wantCircle) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        SetWindowRgn(hwnd_, CreateEllipticRgn(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1), TRUE);
    } else {
        SetWindowRgn(hwnd_, nullptr, TRUE); // 其余形态恢复矩形窗口
    }
}

void MainWindow::BeginCapsulePress(int x, int y) {
    capsulePressing_ = true;
    capsuleDragging_ = false;
    capsulePressClient_ = POINT{ x, y };
    GetCursorPos(&capsulePressScreen_);
    // 复用 WM_LBUTTONDOWN 已建立的 capture，不重复 SetCapture
}

void MainWindow::UpdateCapsulePress(bool lButton) {
    if (!capsulePressing_) return;
    if (!lButton) { FinishCapsulePress(); return; } // 异常释放也走收尾（吸附）
    POINT cur{};
    GetCursorPos(&cur);
    int adx = cur.x - capsulePressScreen_.x; if (adx < 0) adx = -adx;
    int ady = cur.y - capsulePressScreen_.y; if (ady < 0) ady = -ady;
    if (!capsuleDragging_ &&
        (adx >= GetSystemMetrics(SM_CXDRAG) || ady >= GetSystemMetrics(SM_CYDRAG)))
        capsuleDragging_ = true;
    if (!capsuleDragging_) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, cur.x - capsulePressClient_.x, cur.y - capsulePressClient_.y,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainWindow::FinishCapsulePress() {
    if (!capsulePressing_) return;
    const bool wasDrag = capsuleDragging_;
    capsulePressing_ = false;
    capsuleDragging_ = false;
    // 不 ReleaseCapture：由 WM_LBUTTONUP 释放
    if (wasDrag) SnapCapsuleToNearestEdge();
    else if (capsuleShrunk()) StartCapsuleAnim(true); // 点击展开
}

void MainWindow::CancelCapsulePress() {
    const bool wasDrag = capsuleDragging_;
    capsulePressing_ = false;
    capsuleDragging_ = false;
    if (wasDrag) SnapCapsuleToNearestEdge(); // 被夺 capture 也吸附，避免飘在边外
}

void MainWindow::SnapCapsuleToNearestEdge() {
    RECT wr{};
    if (!GetWindowRect(hwnd_, &wr)) return;
    POINT center{ (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, (MONITORINFO*)&mi)) return;
    RECT wa = mi.rcWork;
    int distLeft = center.x - wa.left;
    int distRight = wa.right - center.x;
    DockEdge edge = (distLeft <= distRight) ? DockEdge::Left : DockEdge::Right;
    int workH = wa.bottom - wa.top;
    double t = workH > 0 ? (double)(center.y - wa.top) / workH : 0.5;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    ui_.capsuleDockEdge = (edge == DockEdge::Left) ? "left" : "right";
    ui_.capsuleDockT = t;
    ui_.capsuleMonitor = MonitorDeviceUtf8(mi.szDevice);
    RECT target = CapsuleTargetRect();
    SetWindowPos(hwnd_, HWND_TOPMOST, target.left, target.top,
                 target.right - target.left, target.bottom - target.top, SWP_NOACTIVATE);
    capsuleHover_ = false; // 吸附到新位后清 hover，由后续鼠标移动重新判定
    UpdateLayeredState();  // 按最终尺寸刷新 alpha 与圆点区域
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CaptureVisibleGeometry() {
    if (mountMode_ != MountMode::Normal) return; // (R1-F003) 非普通态不写回 geom_
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < (int)S(220) || h < (int)S(160)) return;
    geom_.x = rc.left;
    geom_.y = rc.top;
    geom_.w = w;
    geom_.h = h;
    geom_.valid = true;
}

void MainWindow::StartCapsuleAnim(bool expand) {
    GetWindowRect(hwnd_, &animFrom_);
    animTo_ = expand ? ExpandedTargetRect() : CapsuleTargetRect();
    animStep_ = 0;
    animActive_ = true;
    capsuleExpanded_ = expand; // 立即置目标态，绘制据此选择胶囊或完整内容
    capsuleHover_ = false;     // 形态切换：清 hover，折叠后由鼠标移动重新触发
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateLayeredState(); // 动画期间（animActive_）保持不透明
    SetTimer(hwnd_, kAnimTimerId, 15, nullptr);
}

void MainWindow::OnAnimTick() {
    animStep_++;
    float t = (float)animStep_ / kAnimSteps;
    if (t > 1.0f) t = 1.0f;
    float e = 1.0f - (1.0f - t) * (1.0f - t); // ease-out
    auto lerp = [&](int a, int b) { return a + (int)((b - a) * e); };
    int x = lerp(animFrom_.left, animTo_.left);
    int y = lerp(animFrom_.top, animTo_.top);
    int w = lerp(animFrom_.right - animFrom_.left, animTo_.right - animTo_.left);
    int h = lerp(animFrom_.bottom - animFrom_.top, animTo_.bottom - animTo_.top);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    if (animStep_ >= kAnimSteps) {
        KillTimer(hwnd_, kAnimTimerId);
        animActive_ = false;
        UpdateLayeredState(); // 折叠静止 → Slim 恢复半透明
        InvalidateRect(hwnd_, nullptr, FALSE); // 定型后按最终态（胶囊/完整）重绘
    }
}

// ——————————————————————————— 行为 ———————————————————————————

void MainWindow::ApplyTopmost() {
    SetWindowPos(hwnd_, ui_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void MainWindow::TogglePin() {
    ui_.alwaysOnTop = !ui_.alwaysOnTop;
    ApplyTopmost();
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ToggleCompletedExpanded() {
    ui_.completedExpanded = !ui_.completedExpanded;
    RebuildLayout();
    ClampScroll();
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ToggleAutostart() {
    Autostart::SetEnabled(!Autostart::IsEnabled());
}

bool MainWindow::Confirm(Str message, UINT icon) {
    return CenteredMessageBox(hwnd_, T(message, lang_), L"X-TODO",
                              MB_OKCANCEL | icon) == IDOK;
}

void MainWindow::DeleteItem(int itemIndex) {
    if (editing() && editIndex_ == itemIndex) CancelEdit();
    model_.Remove(itemIndex);
    RebuildLayout();
    ClampScroll();
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ClearCompletedConfirm() {
    if (model_.CompletedCount() == 0) return;
    if (!Confirm(Str::ClearAllMsg, MB_ICONWARNING)) return;
    model_.ClearCompleted();
    RebuildLayout();
    ClampScroll();
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HideToTray() {
    if (editing()) CommitEdit(false);
    if (savePending_) { // 收起前把待保存改动立即落盘
        KillTimer(hwnd_, kSaveTimerId);
        savePending_ = false;
        SaveNow();
    }
    if (!trayAdded_) { // 托盘不可用时若隐藏将彻底失去入口，改为真正退出
        ExitApp();
        return;
    }
    ShowWindow(hwnd_, SW_HIDE);
    DiscardDeviceResources(); // 收进托盘后释放 D2D 渲染资源，降低后台驻留内存；下次显示按需重建
}

void MainWindow::ExitApp() {
    if (editing()) CommitEdit(false); // 退出前把编辑中的文本落入 model，避免丢失
    SaveNow();
    RemoveTrayIcon();
    if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
    if (editBg_)   { DeleteObject(editBg_);   editBg_   = nullptr; }
    DiscardDeviceResources();
    SafeRelease(&dwrite_);
    SafeRelease(&d2dFactory_);
    DestroyWindow(hwnd_);
}

// ——————————————————————————— 保存 ———————————————————————————

void MainWindow::ScheduleSave() {
    savePending_ = true;
    SetTimer(hwnd_, kSaveTimerId, 800, nullptr); // 去抖：重复调用重置计时
}

void MainWindow::SaveNow() {
    CaptureGeometry();
    if (!Store::Save(model_, geom_, ui_)) {
        // 保存失败（磁盘满 / 占用 / 权限）：保留待存标记并延迟重试，不静默丢改动
        savePending_ = true;
        SetTimer(hwnd_, kSaveTimerId, 3000, nullptr);
    }
}

void MainWindow::CaptureGeometry() {
    if (mountMode_ != MountMode::Normal) return; // 仅普通形态写回，胶囊/桌面尺寸不污染几何
    RECT rc;
    if (GetWindowRect(hwnd_, &rc)) {
        geom_.x = rc.left;
        geom_.y = rc.top;
        geom_.w = rc.right - rc.left;
        geom_.h = rc.bottom - rc.top;
        geom_.valid = true;
    }
}
