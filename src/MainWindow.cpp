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
    mountMode_ = ui_.mountMode == "desktop" ? MountMode::Desktop
               : ui_.mountMode == "capsule" ? MountMode::Capsule
               : MountMode::Normal;
    ApplyMountMode(); // 应用持久化形态（含置顶 / 布局）

    if (loadResult == LoadResult::Failed) // 数据读取失败：告知用户（原文件已备份）
        MessageBoxW(hwnd_, T(Str::LoadFailedMsg, lang_), L"X-TODO", MB_OK | MB_ICONWARNING);
    return true;
}

void MainWindow::Show() {
    ShowWindow(hwnd_, SW_SHOW);
    if (mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_)
        StartCapsuleAnim(true); // 胶囊形态被叫回时滑出
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
        RECT* prc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, prc->left, prc->top,
                     prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        DiscardDeviceResources(); // 下次绘制按新 DPI 重建文本格式
        RebuildLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN:
        SetCapture(hwnd_);
        OnLButtonDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        OnLButtonUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&tme);
        if (mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_) {
            StartCapsuleAnim(true); // 折叠胶囊：鼠标移入即滑出
            return 0;
        }
        OnMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), (wp & MK_LBUTTON) != 0);
        return 0;
    }

    case WM_MOUSELEAVE:
        OnMouseLeave();
        if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_ && !editing())
            StartCapsuleAnim(false); // 鼠标离开展开的便签：滑回胶囊
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp));
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
    AppendMenuW(menu, MF_STRING | (mountMode_ == MountMode::Capsule ? MF_CHECKED : 0), 12, T(Str::ModeCapsule, lang_));
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
        case 12: SetMountMode(MountMode::Capsule);    break;
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
    if (mountMode_ == MountMode::Normal) CaptureGeometry(); // 切走前固化当前几何，避免丢失
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
        RECT c = CapsuleTargetRect();
        SetWindowPos(hwnd_, HWND_TOPMOST, c.left, c.top,
                     c.right - c.left, c.bottom - c.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else { // Normal
        SetWindowPos(hwnd_, ui_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     geom_.x, geom_.y, geom_.w, geom_.h,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    RebuildLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT MainWindow::CapsuleTargetRect() const {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int cw = (int)S(18), ch = (int)S(74);
    int x = wa.right - cw;                       // 贴右缘
    int y = (wa.top + wa.bottom) / 2 - ch / 2;   // 垂直居中
    return RECT{ x, y, x + cw, y + ch };
}

RECT MainWindow::ExpandedTargetRect() const {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int w = geom_.valid ? geom_.w : 300;
    int h = geom_.valid ? geom_.h : 380;
    int x = wa.right - w;                         // 贴右缘展开
    int y = (wa.top + wa.bottom) / 2 - h / 2;
    if (y < wa.top) y = wa.top;
    if (y + h > wa.bottom) y = wa.bottom - h;
    return RECT{ x, y, x + w, y + h };
}

void MainWindow::StartCapsuleAnim(bool expand) {
    GetWindowRect(hwnd_, &animFrom_);
    animTo_ = expand ? ExpandedTargetRect() : CapsuleTargetRect();
    animStep_ = 0;
    animActive_ = true;
    capsuleExpanded_ = expand; // 立即置目标态，绘制据此选择胶囊或完整内容
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    int r = MessageBoxW(hwnd_, T(Str::ClearAllMsg, lang_),
                        L"X-TODO", MB_OKCANCEL | MB_ICONWARNING);
    if (r != IDOK) return;
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
