#include "MainWindow.h"
#include "Autostart.h"
#include "Theme.h"
#include "ThemeCatalog.h"
#include "ThemeLoader.h"
#include "ThemeManagerWindow.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <cwchar>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_NCRENDERING_POLICY
#define DWMWA_NCRENDERING_POLICY 2
#endif
#ifndef DWMNCRP_USEWINDOWSTYLE
#define DWMNCRP_USEWINDOWSTYLE 0
#endif
#ifndef DWMNCRP_DISABLED
#define DWMNCRP_DISABLED 1
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace {
constexpr int kAppIconResourceId = 1; // resources/resource.rc embeds app.ico with ID 1.
constexpr int kDefaultWindowW = 260;
constexpr int kDefaultWindowH = 340;
constexpr int kMinWindowW = 220;
constexpr int kMinWindowH = 160;

// 主题菜单命令分区（1000..1999；避免与挂载 / 语言命令冲突）
constexpr UINT kCmdThemeFollowSystem = 1000;
constexpr UINT kCmdThemeBuiltinBase  = 1100; // + 内置主题索引
constexpr UINT kCmdThemeCustomBase   = 1300; // + 自定义主题索引
constexpr UINT kCmdThemeManager      = 1900;

template <class T> void SafeRelease(T** p) {
    if (*p) { (*p)->Release(); *p = nullptr; }
}

HICON LoadSharedAppIcon(int cx, int cy) {
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kAppIconResourceId),
                             IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR | LR_SHARED);
}

HICON LoadOwnedAppIcon(int cx, int cy) {
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kAppIconResourceId),
                             IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
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

uint32_t BlendColor(uint32_t fg, uint32_t bg, float a) {
    int fr = (fg >> 16) & 0xFF, fg2 = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    int br = (bg >> 16) & 0xFF, bg2 = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int r = br + (int)((fr - br) * a + 0.5f);
    int g = bg2 + (int)((fg2 - bg2) * a + 0.5f);
    int b = bb + (int)((fb - bb) * a + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

float DpiScale(HWND owner) {
    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    return (float)dpi / 96.0f;
}

void ApplyPopupRoundShape(HWND hwnd, int w, int h, int regionRadius) {
    int corner = 3; // DWMWCP_ROUNDSMALL
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    if (SUCCEEDED(hr)) {
        COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    } else {
        HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, regionRadius, regionRadius);
        if (rgn && !SetWindowRgn(hwnd, rgn, TRUE)) DeleteObject(rgn);
    }
}

void SetMainWindowDropShadow(HWND hwnd, bool enabled) {
    LONG_PTR style = GetClassLongPtrW(hwnd, GCL_STYLE);
    LONG_PTR wanted = enabled ? (style | CS_DROPSHADOW)
                              : (style & ~((LONG_PTR)CS_DROPSHADOW));
    if (wanted == style) return;

    SetClassLongPtrW(hwnd, GCL_STYLE, wanted);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

int DpiPx(HWND owner, float v) {
    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    return (int)(v * (float)dpi / 96.0f + 0.5f);
}

int OwnerClientWidth(HWND owner) {
    RECT rc{};
    if (!owner || !GetClientRect(owner, &rc)) return 0;
    return rc.right - rc.left;
}

int ClampPopupWidthToOwner(HWND owner, int preferred, int minW, int fallbackMaxW) {
    int maxW = OwnerClientWidth(owner);
    if (maxW <= 0) maxW = fallbackMaxW;
    if (maxW < minW) maxW = minW;
    return ClampInt(preferred, minW, maxW);
}

bool RegisterPopupClass(const wchar_t* className, WNDPROC proc) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = className;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

enum class ConfirmButton { None, Ok, Cancel };

struct ConfirmState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring message;
    Lang lang = Lang::Zh;
    Theme::ThemeVisual theme; // 打开时持有的主题快照
    bool danger = true;
    bool done = false;
    bool result = false;
    ConfirmButton hover = ConfirmButton::None;
    ConfirmButton pressed = ConfirmButton::None;
    int w = 0;
    int h = 0;
    int msgW = 0;
    int msgH = 0;
    int btnW = 0;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* textFmt = nullptr;
    IDWriteTextFormat* btnFmt = nullptr;
    IDWriteTextFormat* iconFmt = nullptr;
};

RECT ConfirmOkRect(const ConfirmState& s) {
    int bw = s.btnW, bh = DpiPx(s.owner, 24);
    int gap = DpiPx(s.owner, 8), pad = DpiPx(s.owner, 12);
    int left = (s.w - (bw * 2 + gap)) / 2 + bw + gap; // 居中按钮组的右按钮（确认）
    int top = s.h - pad - bh;
    return RECT{ left, top, left + bw, top + bh };
}

RECT ConfirmCancelRect(const ConfirmState& s) {
    RECT ok = ConfirmOkRect(s);
    int bw = ok.right - ok.left, gap = DpiPx(s.owner, 8);
    return RECT{ ok.left - gap - bw, ok.top, ok.left - gap, ok.bottom };
}

ConfirmButton ConfirmHit(const ConfirmState& s, int x, int y) {
    POINT p{ x, y };
    RECT ok = ConfirmOkRect(s), cancel = ConfirmCancelRect(s);
    if (PtInRect(&ok, p)) return ConfirmButton::Ok;
    if (PtInRect(&cancel, p)) return ConfirmButton::Cancel;
    return ConfirmButton::None;
}

void EndConfirm(ConfirmState* s, bool result) {
    s->result = result;
    s->done = true;
    if (s->hwnd && IsWindow(s->hwnd)) DestroyWindow(s->hwnd);
}

void D2DDrawButton(ConfirmState* s, const D2D1_RECT_F& r, const std::wstring& label,
                   bool primary, bool hover, bool pressed) {
    const Theme::ColorSet& c = s->theme.colors;
    float scale = DpiScale(s->owner);
    float radius = 9.0f * scale;
    uint32_t fill = primary ? c.danger : c.paperElevated;
    if (primary && hover) fill = c.dangerHover;
    if (!primary && hover) fill = pressed ? c.buttonPressed : c.buttonHover;
    D2D1_ROUNDED_RECT rr{ r, radius, radius };
    s->brush->SetColor(Theme::Color(fill));
    s->rt->FillRoundedRectangle(rr, s->brush);
    s->brush->SetColor(Theme::Color(primary ? c.danger : c.paperEdge));
    s->rt->DrawRoundedRectangle(rr, s->brush, 1.0f);
    // primary 按钮文字用 checkMark（各主题均与 danger 块高对比），普通按钮用 text
    s->brush->SetColor(Theme::Color(primary ? c.checkMark : c.text));
    s->rt->DrawTextW(label.c_str(), (UINT32)label.size(), s->btnFmt, r, s->brush);
}

LRESULT CALLBACK ConfirmProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ConfirmState* s = reinterpret_cast<ConfirmState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = static_cast<ConfirmState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (!s->rt) { EndPaint(hwnd, &ps); return 0; }
        float scale = DpiScale(s->owner);
        auto S = [scale](float v) { return v * scale; };
        RECT rc{}; GetClientRect(hwnd, &rc);
        float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);

        s->rt->BeginDraw();
        s->rt->SetTransform(D2D1::Matrix3x2F::Identity());
        s->rt->Clear(Theme::Color(s->theme.colors.paperElevated));

        float padX = S(14), padTop = S(14), iconSz = S(14), iconGap = S(9);
        uint32_t accent = s->danger ? s->theme.colors.danger : s->theme.colors.checkFill;

        float rowH = (float)s->msgH > iconSz ? (float)s->msgH : iconSz;
        float blockW = iconSz + iconGap + (float)s->msgW;
        float blockLeft = (W - blockW) / 2.0f;
        if (blockLeft < padX) blockLeft = padX;

        float iconTop = padTop + (rowH - iconSz) / 2.0f;
        D2D1_RECT_F iconRect = D2D1::RectF(blockLeft, iconTop, blockLeft + iconSz, iconTop + iconSz);
        float iconR = iconSz * 0.5f;
        D2D1_ROUNDED_RECT iconRR{ iconRect, iconR, iconR };
        s->brush->SetColor(Theme::Color(BlendColor(accent, s->theme.colors.paperElevated, 0.12f)));
        s->rt->FillRoundedRectangle(iconRR, s->brush);
        s->brush->SetColor(Theme::Color(accent));
        s->rt->DrawRoundedRectangle(iconRR, s->brush, 1.0f);
        s->rt->DrawTextW(L"!", 1, s->iconFmt, iconRect, s->brush);

        float msgLeft = blockLeft + iconSz + iconGap;
        float msgTop = padTop + (rowH - (float)s->msgH) / 2.0f;
        D2D1_RECT_F msgRect = D2D1::RectF(msgLeft, msgTop, msgLeft + (float)s->msgW, msgTop + (float)s->msgH);
        s->brush->SetColor(Theme::Color(s->theme.colors.text));
        s->rt->DrawTextW(s->message.c_str(), (UINT32)s->message.size(), s->textFmt, msgRect, s->brush);

        RECT cancelI = ConfirmCancelRect(*s), okI = ConfirmOkRect(*s);
        D2D1_RECT_F cancelR = D2D1::RectF((float)cancelI.left, (float)cancelI.top, (float)cancelI.right, (float)cancelI.bottom);
        D2D1_RECT_F okR = D2D1::RectF((float)okI.left, (float)okI.top, (float)okI.right, (float)okI.bottom);
        D2DDrawButton(s, cancelR, T(Str::ConfirmCancel, s->lang),
                      false, s->hover == ConfirmButton::Cancel, s->pressed == ConfirmButton::Cancel);
        D2DDrawButton(s, okR, T(Str::ConfirmOk, s->lang),
                      true, s->hover == ConfirmButton::Ok, s->pressed == ConfirmButton::Ok);

        s->rt->EndDraw();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        ConfirmButton h = ConfirmHit(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        if (h != s->hover) { s->hover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN:
        s->pressed = ConfirmHit(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        ConfirmButton pressed = s->pressed;
        if (GetCapture() == hwnd) ReleaseCapture();
        ConfirmButton hit = ConfirmHit(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        s->pressed = ConfirmButton::None;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (hit == pressed && hit == ConfirmButton::Ok) EndConfirm(s, true);
        else if (hit == pressed && hit == ConfirmButton::Cancel) EndConfirm(s, false);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (!s->done && (HWND)lp != hwnd && s->pressed != ConfirmButton::None) {
            s->pressed = ConfirmButton::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_RETURN) { EndConfirm(s, true); return 0; }
        if (wp == VK_ESCAPE) { EndConfirm(s, false); return 0; }
        break;
    case WM_CLOSE:
        EndConfirm(s, false);
        return 0;
    case WM_DESTROY:
        SafeRelease(&s->iconFmt);
        SafeRelease(&s->btnFmt);
        SafeRelease(&s->textFmt);
        SafeRelease(&s->brush);
        SafeRelease(&s->rt);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void MeasureConfirm(ConfirmState& s) {
    int padX    = DpiPx(s.owner, 14);
    int icon    = DpiPx(s.owner, 14);
    int iconGap = DpiPx(s.owner, 9);
    int avail   = s.w - padX * 2 - icon - iconGap;
    int minAvail = DpiPx(s.owner, 40);
    if (avail < minAvail) avail = minAvail;

    IDWriteTextLayout* layout = nullptr;
    s.dwrite->CreateTextLayout(s.message.c_str(), (UINT32)s.message.size(),
                               s.textFmt, (float)avail, 10000.0f, &layout);
    if (layout) {
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        s.msgW = (int)(tm.width + 0.5f);
        s.msgH = (int)(tm.height + 0.5f);
        layout->Release();
    }

    int btnW = DpiPx(s.owner, 54);
    const wchar_t* labels[2] = { T(Str::ConfirmOk, s.lang), T(Str::ConfirmCancel, s.lang) };
    for (const wchar_t* txt : labels) {
        IDWriteTextLayout* bl = nullptr;
        s.dwrite->CreateTextLayout(txt, (UINT32)wcslen(txt), s.btnFmt, 10000.0f, 100.0f, &bl);
        if (bl) {
            DWRITE_TEXT_METRICS bm{};
            bl->GetMetrics(&bm);
            int need = (int)(bm.width + 0.5f) + DpiPx(s.owner, 24);
            if (need > btnW) btnW = need;
            bl->Release();
        }
    }
    s.btnW = btnW;
}

int ConfirmHeight(const ConfirmState& s) {
    int padTop    = DpiPx(s.owner, 14);
    int padBottom = DpiPx(s.owner, 12);
    int icon      = DpiPx(s.owner, 14);
    int msgBtnGap = DpiPx(s.owner, 12);
    int btnH      = DpiPx(s.owner, 24);
    int rowH = s.msgH > icon ? s.msgH : icon;
    return padTop + rowH + msgBtnGap + btnH + padBottom;
}

bool ShowThemedConfirm(HWND owner, const wchar_t* text, Lang lang, bool danger,
                       const Theme::ThemeVisual& theme,
                       ID2D1Factory* d2dFactory, IDWriteFactory* dwrite) {
    const wchar_t* cls = L"XTodoConfirmPopup";
    if (!RegisterPopupClass(cls, ConfirmProc)) return false;
    float scale = DpiScale(owner);
    ConfirmState state{};
    state.owner = owner;
    state.message = text ? text : L"";
    state.lang = lang;
    state.theme = theme;
    state.danger = danger;
    state.d2dFactory = d2dFactory;
    state.dwrite = dwrite;

    dwrite->CreateTextFormat(Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                             12.0f * scale, L"", &state.textFmt);
    dwrite->CreateTextFormat(Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                             11.0f * scale, L"", &state.btnFmt);
    if (state.btnFmt) {
        state.btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        state.btnFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    dwrite->CreateTextFormat(Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                             8.0f * scale, L"", &state.iconFmt);
    if (state.iconFmt) {
        state.iconFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        state.iconFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    state.w = ClampPopupWidthToOwner(owner, DpiPx(owner, 188),
                                     DpiPx(owner, 160), DpiPx(owner, 188));
    MeasureConfirm(state);
    state.h = ConfirmHeight(state);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, cls, L"",
                                WS_POPUP, 0, 0, state.w, state.h,
                                owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) {
        SafeRelease(&state.iconFmt);
        SafeRelease(&state.btnFmt);
        SafeRelease(&state.textFmt);
        return false;
    }
    ApplyPopupRoundShape(hwnd, state.w, state.h, DpiPx(owner, 16));

    D2D1_SIZE_U sz = D2D1::SizeU(state.w, state.h);
    if (FAILED(d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                   D2D1::HwndRenderTargetProperties(hwnd, sz),
                                                   &state.rt))) {
        DestroyWindow(hwnd);
        return false;
    }
    state.rt->SetDpi(96.0f, 96.0f);
    if (FAILED(state.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &state.brush))) {
        DestroyWindow(hwnd);
        return false;
    }

    CenterWindowOverOwner(hwnd, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg{};
    BOOL got = TRUE;
    while (!state.done && (got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (got == 0) PostQuitMessage((int)msg.wParam);

    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    return state.result;
}

struct PopupMenuItem {
    UINT cmd = 0;
    std::wstring text;
    bool separator = false;
    bool checked = false;
    bool danger = false;
    bool enabled = true;
    int indent = 0;
    bool header = false; // 分组标题：加粗、不可点击
};

struct PopupMenuState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    const std::vector<PopupMenuItem>* items = nullptr;
    Theme::ThemeVisual theme; // 打开时持有的主题快照
    bool done = false;
    UINT result = 0;
    int hover = -1;
    int w = 0;
    int h = 0;
    int rowH = 0;
    int sepH = 0;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* textFmt = nullptr;
};

int MenuItemAt(const PopupMenuState& s, int y) {
    if (!s.items) return -1;
    int top = DpiPx(s.owner, 5);
    for (size_t i = 0; i < s.items->size(); ++i) {
        const PopupMenuItem& item = (*s.items)[i];
        int h = item.separator ? s.sepH : s.rowH;
        if (y >= top && y < top + h)
            return (!item.separator && item.enabled && !item.header) ? (int)i : -1;
        top += h;
    }
    return -1;
}

void EndPopupMenu(PopupMenuState* s, UINT cmd) {
    s->result = cmd;
    s->done = true;
    if (s->hwnd && GetCapture() == s->hwnd) ReleaseCapture();
    if (s->hwnd && IsWindow(s->hwnd)) DestroyWindow(s->hwnd);
}

int MeasurePopupMenuWidth(HWND owner, const std::vector<PopupMenuItem>& items,
                          IDWriteFactory* dwrite, IDWriteTextFormat* fmt) {
    int maxText = 0;
    for (const auto& item : items) {
        if (item.separator) continue;
        IDWriteTextLayout* layout = nullptr;
        dwrite->CreateTextLayout(item.text.c_str(), (UINT32)item.text.size(),
                                 fmt, 10000.0f, 100.0f, &layout);
        if (layout) {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            int textW = (int)(tm.width + 0.5f) + DpiPx(owner, 46 + item.indent * 14);
            if (textW > maxText) maxText = textW;
            layout->Release();
        }
    }
    int minW = DpiPx(owner, 100);
    int preferred = maxText + DpiPx(owner, 4);
    return ClampPopupWidthToOwner(owner, preferred, minW, DpiPx(owner, 300));
}

int MeasurePopupMenuHeight(HWND owner, const std::vector<PopupMenuItem>& items) {
    int rowH = DpiPx(owner, 22), sepH = DpiPx(owner, 6);
    int h = DpiPx(owner, 10);
    for (const auto& item : items) h += item.separator ? sepH : rowH;
    return h;
}

LRESULT CALLBACK PopupMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PopupMenuState* s = reinterpret_cast<PopupMenuState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = static_cast<PopupMenuState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (!s->rt) { EndPaint(hwnd, &ps); return 0; }
        float scale = DpiScale(s->owner);
        auto Sf = [scale](float v) { return v * scale; };
        RECT rc{}; GetClientRect(hwnd, &rc);
        float W = (float)(rc.right - rc.left);

        s->rt->BeginDraw();
        s->rt->SetTransform(D2D1::Matrix3x2F::Identity());
        s->rt->Clear(Theme::Color(s->theme.colors.paperElevated));

        float y = Sf(5);
        float pad = Sf(10);
        for (size_t i = 0; i < s->items->size(); ++i) {
            const PopupMenuItem& item = (*s->items)[i];
            if (item.separator) {
                float mid = y + (float)s->sepH / 2.0f;
                s->brush->SetColor(Theme::Color(s->theme.colors.divider));
                s->rt->DrawLine(D2D1::Point2F(pad, mid), D2D1::Point2F(W - pad, mid), s->brush, 1.0f);
                y += (float)s->sepH;
                continue;
            }
            D2D1_RECT_F row = D2D1::RectF(Sf(6), y, W - Sf(6), y + (float)s->rowH);
            if (item.header) { // 分组标题：textWeak、不可点击、无 hover / 勾选
                D2D1_RECT_F hr = D2D1::RectF(row.left + Sf(2), row.top, row.right, row.bottom);
                s->brush->SetColor(Theme::Color(s->theme.colors.textWeak));
                s->rt->DrawTextW(item.text.c_str(), (UINT32)item.text.size(), s->textFmt, hr, s->brush,
                                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
                y += (float)s->rowH;
                continue;
            }
            if ((int)i == s->hover) {
                D2D1_ROUNDED_RECT rr{ row, Sf(8), Sf(8) };
                s->brush->SetColor(Theme::Color(s->theme.colors.menuHover));
                s->rt->FillRoundedRectangle(rr, s->brush);
            }
            if (item.checked) {
                float ckL = row.left + Sf(6), ckT = row.top + Sf(5);
                float ckR = row.left + Sf(18), ckB = row.top + Sf(17);
                float cw = ckR - ckL, ch = ckB - ckT;
                s->brush->SetColor(Theme::Color(s->theme.colors.checkFill));
                s->rt->DrawLine(D2D1::Point2F(ckL + cw / 4, ckT + ch / 2),
                                D2D1::Point2F(ckL + cw / 2, ckT + ch * 3 / 4), s->brush, 2.0f);
                s->rt->DrawLine(D2D1::Point2F(ckL + cw / 2, ckT + ch * 3 / 4),
                                D2D1::Point2F(ckR - cw / 5, ckT + ch / 4), s->brush, 2.0f);
            }
            D2D1_RECT_F textR = D2D1::RectF(row.left + Sf(24.0f + item.indent * 14.0f), row.top,
                                             row.right - Sf(8), row.bottom);
            uint32_t textColor = item.enabled ? (item.danger ? s->theme.colors.danger : s->theme.colors.text)
                                              : s->theme.colors.disabledText;
            s->brush->SetColor(Theme::Color(textColor));
            s->rt->DrawTextW(item.text.c_str(), (UINT32)item.text.size(), s->textFmt, textR, s->brush,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
            y += (float)s->rowH;
        }

        s->rt->EndDraw();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        int h = MenuItemAt(*s, GET_Y_LPARAM(lp));
        if (h != s->hover) { s->hover = h; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x < 0 || x >= s->w || y < 0 || y >= s->h) { EndPopupMenu(s, 0); return 0; }
        int idx = MenuItemAt(*s, y);
        EndPopupMenu(s, idx >= 0 ? (*s->items)[idx].cmd : 0);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { EndPopupMenu(s, 0); return 0; }
        if (wp == VK_RETURN && s->hover >= 0) {
            EndPopupMenu(s, (*s->items)[s->hover].cmd);
            return 0;
        }
        if (wp == VK_DOWN || wp == VK_UP) {
            int count = s->items ? (int)s->items->size() : 0;
            if (count > 0) {
                int idx = s->hover;
                for (int tries = 0; tries < count; ++tries) {
                    idx = (wp == VK_DOWN) ? (idx + 1 + count) % count : (idx - 1 + count) % count;
                    if (!(*s->items)[idx].separator && !(*s->items)[idx].header && (*s->items)[idx].enabled) {
                        s->hover = idx;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        break;
                    }
                }
            }
            return 0;
        }
        break;
    case WM_CANCELMODE:
    case WM_CLOSE:
        EndPopupMenu(s, 0);
        return 0;
    case WM_CAPTURECHANGED:
        if (!s->done && (HWND)lp != hwnd) EndPopupMenu(s, 0);
        return 0;
    case WM_DESTROY:
        SafeRelease(&s->textFmt);
        SafeRelease(&s->brush);
        SafeRelease(&s->rt);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

UINT ShowPopupMenu(HWND owner, POINT pt, const std::vector<PopupMenuItem>& items, bool alignRight,
                   const Theme::ThemeVisual& theme,
                   ID2D1Factory* d2dFactory, IDWriteFactory* dwrite) {
    const wchar_t* cls = L"XTodoPopupMenu";
    if (!RegisterPopupClass(cls, PopupMenuProc)) return 0;
    float scale = DpiScale(owner);
    PopupMenuState state{};
    state.owner = owner;
    state.items = &items;
    state.theme = theme;
    state.d2dFactory = d2dFactory;
    state.dwrite = dwrite;
    state.rowH = DpiPx(owner, 22);
    state.sepH = DpiPx(owner, 6);

    dwrite->CreateTextFormat(Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                             11.0f * scale, L"", &state.textFmt);
    if (state.textFmt) {
        state.textFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        state.textFmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }

    state.w = MeasurePopupMenuWidth(owner, items, dwrite, state.textFmt);
    state.h = MeasurePopupMenuHeight(owner, items);

    if (alignRight) pt.x -= state.w;
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        pt.x = ClampInt(pt.x, mi.rcWork.left, mi.rcWork.right - state.w);
        pt.y = ClampInt(pt.y, mi.rcWork.top, mi.rcWork.bottom - state.h);
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, cls, L"",
                                WS_POPUP, pt.x, pt.y, state.w, state.h,
                                nullptr, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) {
        SafeRelease(&state.textFmt);
        return 0;
    }
    ApplyPopupRoundShape(hwnd, state.w, state.h, DpiPx(owner, 12));

    D2D1_SIZE_U sz = D2D1::SizeU(state.w, state.h);
    if (FAILED(d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                                   D2D1::HwndRenderTargetProperties(hwnd, sz),
                                                   &state.rt))) {
        DestroyWindow(hwnd);
        return 0;
    }
    state.rt->SetDpi(96.0f, 96.0f);
    if (FAILED(state.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &state.brush))) {
        DestroyWindow(hwnd);
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetCapture(hwnd);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    MSG msg{};
    BOOL got = TRUE;
    while (!state.done && (got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (got == 0) PostQuitMessage((int)msg.wParam);
    if (GetCapture() == hwnd) ReleaseCapture();
    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    SetForegroundWindow(owner);
    return state.result;
}

// 构造标题栏皮肤菜单。
void AppendThemeMenu(std::vector<PopupMenuItem>& items, Lang lang,
                     const std::string& mode, const std::string& curId,
                     const std::vector<Theme::ThemeVisual>& customs) {
    items.push_back(PopupMenuItem{ 0, T(Str::ThemeHeader, lang), false, false, false, true, 0, true });
    items.push_back(PopupMenuItem{ kCmdThemeFollowSystem, T(Str::ThemeFollowSystem, lang), false,
                                   mode == "follow_system", false, true, 1 });
    static const struct { const char* id; Str name; } kBuiltinItems[] = {
        { "paper", Str::ThemePaper }, { "mint", Str::ThemeMint }, { "sky", Str::ThemeSky },
        { "rose", Str::ThemeRose }, { "sand", Str::ThemeSand }, { "graphite", Str::ThemeGraphite },
        { "ink", Str::ThemeInk }, { "contrast", Str::ThemeContrast }
    };
    for (UINT i = 0; i < 8; i++) {
        bool cur = (mode != "follow_system") && (curId == kBuiltinItems[i].id);
        items.push_back(PopupMenuItem{ kCmdThemeBuiltinBase + i, T(kBuiltinItems[i].name, lang),
                                       false, cur, false, true, 1 });
    }
    UINT shown = (UINT)customs.size(); if (shown > 8) shown = 8; // 超出 8 个在管理窗口看全部
    for (UINT i = 0; i < shown; i++) {
        bool cur = (mode == "custom") && (curId == customs[i].id);
        std::wstring nm = (lang == Lang::Zh) ? customs[i].name.zh : customs[i].name.en;
        items.push_back(PopupMenuItem{ kCmdThemeCustomBase + i, nm, false, cur, false, true, 1 });
    }
    items.push_back(PopupMenuItem{ kCmdThemeManager, T(Str::ThemeCustom, lang), false, false, false, true, 1 });
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
    wc.hIcon         = LoadSharedAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm       = LoadSharedAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.hbrBackground = nullptr; // 全程 Direct2D 绘制
    wc.lpszClassName = kWindowClass;
    return RegisterClassExW(&wc) != 0;
}

bool MainWindow::Create() {
    if (!RegisterWindowClass()) return false;

    LoadResult loadResult = Store::Load(model_, geom_, ui_);

    // 校验持久化几何：尺寸合理且至少落在某个显示器上，否则回退默认位置（防离屏/零尺寸找不回）
    int w = kDefaultWindowW, h = kDefaultWindowH;
    bool geomOk = false;
    if (geom_.valid && geom_.w >= kMinWindowW && geom_.w <= 4000 &&
        geom_.h >= kMinWindowH && geom_.h <= 4000) {
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
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadSharedAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadSharedAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)));

    dpi_ = GetDpiForWindow(hwnd_);
    geom_ = { x, y, w, h, true }; // 回写校验后的几何，供形态切换/保存复用

    if (!CreateDeviceIndependentResources()) return false;

    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    explorerRestartMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    lang_ = ui_.lang == "zh" ? Lang::Zh
          : ui_.lang == "en" ? Lang::En
          : SystemDefaultLang();
    capsuleStyle_ = ui_.capsuleStyle == "dot" ? CapsuleStyle::Dot : CapsuleStyle::Slim;
    mountMode_ = ui_.mountMode == "desktop" ? MountMode::Desktop
               : ui_.mountMode == "capsule" ? MountMode::Capsule
               : MountMode::Normal;
    // 主题快照必须在 AddTrayIcon（按主题生成图标）与首帧渲染前就绪
    ReloadThemes();
    ApplyResolvedTheme(false);
    AddTrayIcon();
    ApplyMountMode(); // 应用持久化形态（含置顶 / 布局）

    if (loadResult == LoadResult::Failed) // 数据读取失败：告知用户（原文件已备份）
        MessageBoxW(hwnd_, T(Str::LoadFailedMsg, lang_), L"X-TODO", MB_OK | MB_ICONWARNING);
    return true;
}

// ——————————————————————————— 主题 ———————————————————————————

void MainWindow::ReloadThemes() {
    Theme::LoadResult lr = Theme::LoadCustomThemes();
    customThemes_ = std::move(lr.themes);
    themeIssues_  = std::move(lr.issues);
}

void MainWindow::ApplyResolvedTheme(bool persist) {
    themeNotices_.clear(); // notice 反映最近一次解析状态，不累积

    bool darkOk = false, hcOk = false;
    Theme::ResolveInput in;
    in.mode               = ui_.themeMode;
    in.themeId            = ui_.themeId;
    in.lightThemeId       = ui_.lightThemeId;
    in.darkThemeId        = ui_.darkThemeId;
    in.systemDark         = Theme::SystemUsesDarkMode(&darkOk);
    in.systemHighContrast = Theme::SystemHighContrastOn(&hcOk);
    in.customThemes       = &customThemes_;

    Theme::ResolveResult rr = Theme::ResolveTheme(in);
    theme_ = rr.theme;

    // 系统状态读取失败：显式记录（不静默回退到浅色 / 非高对比）
    if (!darkOk)
        themeNotices_.push_back({ lang_ == Lang::Zh ? L"无法读取系统明暗模式，已按浅色处理"
                                                    : L"Could not read system light/dark mode; using light" });
    if (!hcOk)
        themeNotices_.push_back({ lang_ == Lang::Zh ? L"无法读取系统高对比状态，已按未开启处理"
                                                    : L"Could not read system high-contrast state; assuming off" });

    if (rr.fellBack) {
        std::wstring msg = T(Str::ThemeFallbackNotice, lang_);
        if (!rr.message.empty()) msg += L"（" + rr.message + L"）";
        themeNotices_.push_back({ msg });
    }
    // 系统高对比但用户显式选了非高对比主题：提示当前主题可能不可访问
    if (in.systemHighContrast && ui_.themeMode != "follow_system" && theme_.id != "contrast")
        themeNotices_.push_back({ T(Str::ThemeHighContrastNotice, lang_) });

    // 编辑框背景刷随主题失效，下次 WM_CTLCOLOREDIT 按新主题重建
    if (editBg_) { DeleteObject(editBg_); editBg_ = nullptr; }
    if (edit_ && IsWindow(edit_) && IsWindowVisible(edit_))
        InvalidateRect(edit_, nullptr, TRUE);

    UpdateLayeredState();        // Slim 胶囊透明度使用新主题 slimAlpha
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    if (persist) ScheduleSave();
}

void MainWindow::SetThemeMode(const std::string& mode) {
    if (mode != "builtin" && mode != "custom" && mode != "follow_system") return;
    ui_.themeMode = mode;
    ApplyResolvedTheme(true);
}

void MainWindow::SetThemeId(const std::string& id) {
    // 点击具体主题：退出 follow_system，按 id 前缀进入 custom 或 builtin
    ui_.themeMode = (id.rfind("custom.", 0) == 0) ? "custom" : "builtin";
    ui_.themeId = id;
    ApplyResolvedTheme(true);
}

void MainWindow::SetFollowSystemThemes(const std::string& lightId, const std::string& darkId) {
    if (!lightId.empty()) ui_.lightThemeId = lightId;
    if (!darkId.empty())  ui_.darkThemeId = darkId;
    ApplyResolvedTheme(true);
}

void MainWindow::ShowThemeManager() {
    Theme::ManagerHost host;
    auto refresh = [this, &host]() {
        host.currentMode  = ui_.themeMode;
        host.currentId    = theme_.id;
        host.lightThemeId = ui_.lightThemeId;
        host.darkThemeId  = ui_.darkThemeId;
        host.current      = theme_;
        host.builtins     = Theme::BuiltInThemes();
        host.customs      = customThemes_;
        host.issues       = themeIssues_;
        host.notices      = themeNotices_;
        host.lang         = lang_;
    };
    refresh();
    host.applyTheme = [this, &refresh](const std::string& id) { SetThemeId(id); refresh(); };
    host.reload     = [this, &refresh]() { ReloadThemes(); ApplyResolvedTheme(true); refresh(); };
    host.openFolder = [this]() {
        std::wstring dir = Theme::ThemeDirectory();
        if (!dir.empty()) {
            CreateDirectoryW(dir.c_str(), nullptr); // 打开时才创建目录
            ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    };
    host.exportCurrent = [this, &refresh]() {
        Theme::ThemeIssue err;
        if (!Theme::ExportTheme(theme_, Theme::ThemeDirectory(), &err))
            themeNotices_.push_back({ (lang_ == Lang::Zh ? L"导出失败：" : L"Export failed: ") + err.detail });
        ReloadThemes(); // 导出后刷新列表
        refresh();
    };
    host.setLightFollow = [this, &refresh](const std::string& id) { SetFollowSystemThemes(id, ""); refresh(); };
    host.setDarkFollow  = [this, &refresh](const std::string& id) { SetFollowSystemThemes("", id); refresh(); };
    Theme::ShowThemeManagerWindow(hwnd_, host);
}

void MainWindow::Show(bool expandCapsule) {
    ShowWindow(hwnd_, SW_SHOW);
    if (expandCapsule && mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_)
        StartCapsuleAnim(true); // 主动唤起（托盘 / 菜单 / 第二实例）时滑出
    SetForegroundWindow(hwnd_);
}

void MainWindow::InitialShow() {
    Show(false);
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
    if (msg == explorerRestartMsg_ && explorerRestartMsg_ != 0) {
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

    case WM_SETTINGCHANGE:
        // follow_system：系统明暗 / 高对比变化时，仅当解析出的主题 id 变化才重新应用，避免抖动
        if (ui_.themeMode == "follow_system") {
            bool sysDark = Theme::SystemUsesDarkMode(nullptr);
            bool sysHC   = Theme::SystemHighContrastOn(nullptr);
            std::string wantId = sysHC ? "contrast" : (sysDark ? ui_.darkThemeId : ui_.lightThemeId);
            if (wantId != theme_.id) ApplyResolvedTheme(false);
        }
        break; // 落到 DefWindowProc，保留系统默认处理

    case WM_SYSCOLORCHANGE:
        // 高对比是系统可访问性状态，参与主题解析：重读并重新应用
        ApplyResolvedTheme(false);
        break;

    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        break; // 落到 DefWindowProc，保留系统默认处理

    case WM_SIZE:
        Resize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_EXITSIZEMOVE:
        CaptureVisibleGeometry();
        if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_) {
            RECT target = ExpandedTargetRect();
            SetWindowPos(hwnd_, HWND_TOPMOST, target.left, target.top,
                         target.right - target.left, target.bottom - target.top,
                         SWP_NOACTIVATE);
            UpdateLayeredState();
            RebuildLayout();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
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
        { // 仅当光标确实在客户区内才取消折叠宽限：捕获态下拖到窗口外仍会收到 WM_MOUSEMOVE
            RECT crc{}; GetClientRect(hwnd_, &crc);
            if (PtInRect(&crc, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }))
                KillTimer(hwnd_, kCollapseTimerId);
        }
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
        if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_ && !editing() && !menuOpen_)
            SetTimer(hwnd_, kCollapseTimerId, kCollapseDelayMs, nullptr); // 离开不立即收：宽限期内移回即取消
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
        mmi->ptMinTrackSize.x = (LONG)S(kMinWindowW);
        mmi->ptMinTrackSize.y = (LONG)S(kMinWindowH);
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
        } else if (wp == kCollapseTimerId) {
            KillTimer(hwnd_, kCollapseTimerId);
            if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_ && !editing() && !menuOpen_) {
                POINT cur{}; GetCursorPos(&cur);
                RECT wr{}; GetWindowRect(hwnd_, &wr);
                bool lbtn = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (lbtn) SetTimer(hwnd_, kCollapseTimerId, kCollapseDelayMs, nullptr); // 拖动/缩放中：顺延再判
                else if (!PtInRect(&wr, cur)) StartCapsuleAnim(false); // 仍在窗口外才收回
            }
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, Theme::GdiColor(theme_.colors.paper));
        SetTextColor(hdc, Theme::GdiColor(theme_.colors.text));
        if (!editBg_) editBg_ = CreateSolidBrush(Theme::GdiColor(theme_.colors.paper));
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
    textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    if (FAILED(dwrite_->CreateTextFormat(
            Theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            S(Theme::kSmallFont), L"", &smallFormat_))) {
        DiscardDeviceResources();
        return false;
    }
    smallFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    smallFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    RebuildLayout();
    ClampScroll();
    if (editing()) LayoutEditBox();
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
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 32;
    if (cy <= 0) cy = 32;
    return LoadOwnedAppIcon(cx, cy);
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
        default:
            // 主题命令分区（1000..1999）
            if (cmd == kCmdThemeFollowSystem) {
                SetThemeMode("follow_system");
            } else if (cmd == kCmdThemeManager) {
                ShowThemeManager();
            } else if (cmd >= kCmdThemeBuiltinBase && cmd < kCmdThemeBuiltinBase + 100) {
                static const char* kBuiltinIds[] = {
                    "paper", "mint", "sky", "rose", "sand", "graphite", "ink", "contrast"
                };
                UINT idx = cmd - kCmdThemeBuiltinBase;
                if (idx < 8) SetThemeId(kBuiltinIds[idx]);
            } else if (cmd >= kCmdThemeCustomBase && cmd < kCmdThemeCustomBase + 200) {
                UINT idx = cmd - kCmdThemeCustomBase;
                if (idx < customThemes_.size()) SetThemeId(customThemes_[idx].id);
            }
            break;
    }
}

void MainWindow::ShowTrayMenu() {
    const bool wasShrunk = capsuleShrunk(); // (R1-F001) 记录弹出前是否折叠

    const bool inCapsule = mountMode_ == MountMode::Capsule;
    std::vector<PopupMenuItem> items{
        PopupMenuItem{ 1, T(Str::Show, lang_) },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 10, T(Str::ModeNormal, lang_), false, mountMode_ == MountMode::Normal },
        PopupMenuItem{ 11, T(Str::ModeDesktop, lang_), false, mountMode_ == MountMode::Desktop },
        PopupMenuItem{ 0, T(Str::ModeCapsule, lang_), false, false, false, false },
        PopupMenuItem{ 30, T(Str::StyleSlim, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Slim, false, true, 1 },
        PopupMenuItem{ 31, T(Str::StyleDot, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Dot, false, true, 1 },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 20, T(Str::ToggleLang, lang_) },
        PopupMenuItem{ 2, T(Str::Autostart, lang_), false, Autostart::IsEnabled() },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 3, T(Str::Exit, lang_), false, false, true },
    };

    POINT pt;
    GetCursorPos(&pt);
    menuOpen_ = true;
    UINT cmd = ShowPopupMenu(hwnd_, pt, items, false, theme_, d2dFactory_, dwrite_);
    menuOpen_ = false;                 // 必须在 HandleMenuCommand 前清，且无论返回值
    HandleMenuCommand(cmd);
    // (R1-F001) 补判收缩，排除：退出(3)；以及刚由 Show(1) 把折叠胶囊展开的情形（否则光标在窗外会被立刻收回）
    if (cmd != 3 && !(cmd == 1 && wasShrunk)) MaybeCollapseCapsule();
}

void MainWindow::ShowTitleMenu() {
    const bool inCapsule = mountMode_ == MountMode::Capsule;
    std::vector<PopupMenuItem> items{
        PopupMenuItem{ 10, T(Str::ModeNormal, lang_), false, mountMode_ == MountMode::Normal },
        PopupMenuItem{ 11, T(Str::ModeDesktop, lang_), false, mountMode_ == MountMode::Desktop },
        PopupMenuItem{ 0, T(Str::ModeCapsule, lang_), false, false, false, false },
        PopupMenuItem{ 30, T(Str::StyleSlim, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Slim, false, true, 1 },
        PopupMenuItem{ 31, T(Str::StyleDot, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Dot, false, true, 1 },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 20, T(Str::ToggleLang, lang_) },
        PopupMenuItem{ 2, T(Str::Autostart, lang_), false, Autostart::IsEnabled() },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 3, T(Str::Exit, lang_), false, false, true },
    };

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    POINT pt{ rc.right - (LONG)S(6), (LONG)menuRect_.bottom }; // 贴近窗口右侧弹出
    ClientToScreen(hwnd_, &pt);
    menuOpen_ = true;
    UINT cmd = ShowPopupMenu(hwnd_, pt, items, true, theme_, d2dFactory_, dwrite_);
    menuOpen_ = false;                 // 必须在 HandleMenuCommand 前清，且无论返回值
    HandleMenuCommand(cmd);
    if (cmd != 3) MaybeCollapseCapsule(); // (R1-F001) 非退出：按真实光标位置补判（cmd==3 已 DestroyWindow）
}

void MainWindow::ShowThemeMenu() {
    std::vector<PopupMenuItem> items;
    AppendThemeMenu(items, lang_, ui_.themeMode, theme_.id, customThemes_);

    POINT pt{ (LONG)themeRect_.right, (LONG)themeRect_.bottom };
    ClientToScreen(hwnd_, &pt);
    menuOpen_ = true;
    UINT cmd = ShowPopupMenu(hwnd_, pt, items, true, theme_, d2dFactory_, dwrite_);
    menuOpen_ = false;
    HandleMenuCommand(cmd);
    if (cmd != 3) MaybeCollapseCapsule();
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

void MainWindow::CaptureCapsuleDockFromRect(const RECT& wr) {
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
}

void MainWindow::SetCapsuleStyle(CapsuleStyle s) {
    if (s == capsuleStyle_) return;
    capsuleStyle_ = s;
    ui_.capsuleStyle = (s == CapsuleStyle::Dot) ? "dot" : "slim";
    if (mountMode_ == MountMode::Capsule) {
        if (animActive_) {                 // (R1-F005) 动画中切样式：先定格到终态，避免 region 落在旧样式尺寸
            KillTimer(hwnd_, kAnimTimerId);
            animActive_ = false;           // capsuleExpanded_ 已是动画目标态（StartCapsuleAnim 立即置）
        }
        RECT t = capsuleExpanded_ ? ExpandedTargetRect() : CapsuleTargetRect(); // 按新样式算尺寸
        SetWindowPos(hwnd_, HWND_TOPMOST, t.left, t.top,
                     t.right - t.left, t.bottom - t.top, SWP_NOACTIVATE);
        UpdateLayeredState(); // resize 后再算 region/alpha/corner/border，避免用旧样式尺寸的形状
    }
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// 按当前样式 / 折叠 / hover 维护 layered 模式。Slim 用整窗 alpha，Dot 折叠用 per-pixel alpha。
void MainWindow::UpdateLayeredState() {
    const bool wantSlimAlpha = (mountMode_ == MountMode::Capsule && capsuleStyle_ == CapsuleStyle::Slim);
    const bool wantDotAlpha  = (mountMode_ == MountMode::Capsule && capsuleStyle_ == CapsuleStyle::Dot && capsuleShrunk());
    const int wantedMode = wantDotAlpha ? 2 : (wantSlimAlpha ? 1 : 0);

    if (layeredMode_ != wantedMode) {
        const int oldMode = layeredMode_;
        LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        if ((ex & WS_EX_LAYERED) != 0) {
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        }
        if (wantedMode != 0) {
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        layeredMode_ = wantedMode;
        if (oldMode == 2 || wantedMode == 2) DiscardDeviceResources();
    }

    if (wantedMode == 1) {
        BYTE alpha = (capsuleShrunk() && !capsuleHover_)
                     ? (BYTE)(theme_.capsule.slimAlpha * 255.0f) : 255;
        SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
    }
    UpdateCapsuleRegion(); // 形态 / 样式 / 折叠变化时同步窗口区域与 DWM 边界属性
}

// Dot 折叠态由 per-pixel layered alpha 定形；Slim 折叠态交给 DWM 圆角合成，避免 GDI region 硬边。
// Dot 折叠态额外压制系统矩形阴影，避免透明圆点外露出方框。
// 折叠态压制 DWM 边框线；其余形态恢复矩形 + ROUND。
void MainWindow::UpdateCapsuleRegion() {
    const bool slimShrunk = mountMode_ == MountMode::Capsule
                            && capsuleStyle_ == CapsuleStyle::Slim && capsuleShrunk();
    const bool dotShrunk  = mountMode_ == MountMode::Capsule
                            && capsuleStyle_ == CapsuleStyle::Dot  && capsuleShrunk();
    const bool suppressBorder = slimShrunk || dotShrunk;

    SetMainWindowDropShadow(hwnd_, !dotShrunk);
    int ncPolicy = dotShrunk ? DWMNCRP_DISABLED : DWMNCRP_USEWINDOWSTYLE;
    DwmSetWindowAttribute(hwnd_, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

    int corner = dotShrunk ? 1 : 2; // 1=DWMWCP_DONOTROUND, 2=DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    COLORREF border = suppressBorder ? 0xFFFFFFFE /* DWMWA_COLOR_NONE */
                                     : 0xFFFFFFFF /* DWMWA_COLOR_DEFAULT */;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    SetWindowRgn(hwnd_, nullptr, TRUE);
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
    CaptureCapsuleDockFromRect(wr);
    RECT target = CapsuleTargetRect();
    SetWindowPos(hwnd_, HWND_TOPMOST, target.left, target.top,
                 target.right - target.left, target.bottom - target.top, SWP_NOACTIVATE);
    capsuleHover_ = false; // 吸附到新位后清 hover，由后续鼠标移动重新判定
    UpdateLayeredState();  // 按最终尺寸刷新 alpha 与圆点区域
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CaptureVisibleGeometry() {
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < (int)S(220) || h < (int)S(160)) return;

    if (mountMode_ == MountMode::Capsule) {
        if (!capsuleExpanded_ || animActive_) return;
        CaptureCapsuleDockFromRect(rc);
    } else {
        geom_.x = rc.left;
        geom_.y = rc.top;
    }
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
    if (!animActive_) return; // 已定格的动画：忽略 KillTimer 后仍滞留入队的 WM_TIMER（用旧 animFrom_/animTo_ 会错位）
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
    (void)icon;
    return ShowThemedConfirm(hwnd_, T(message, lang_), lang_, true, theme_, d2dFactory_, dwrite_);
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
    RECT rc;
    if (!GetWindowRect(hwnd_, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < (int)S(220) || h < (int)S(160)) return;

    if (mountMode_ == MountMode::Capsule) {
        if (!capsuleExpanded_ || animActive_) return;
        CaptureCapsuleDockFromRect(rc);
    } else {
        geom_.x = rc.left;
        geom_.y = rc.top;
    }
    geom_.w = w;
    geom_.h = h;
    geom_.valid = true;
}
