#include "MainWindow.h"
#include "Autostart.h"
#include "Theme.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <cwchar>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
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

COLORREF ToColorRef(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

uint32_t BlendColor(uint32_t fg, uint32_t bg, float a) {
    int fr = (fg >> 16) & 0xFF, fg2 = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    int br = (bg >> 16) & 0xFF, bg2 = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int r = br + (int)((fr - br) * a + 0.5f);
    int g = bg2 + (int)((fg2 - bg2) * a + 0.5f);
    int b = bb + (int)((fb - bb) * a + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
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

HFONT CreateUiFont(HWND owner, float size, bool bold = false) {
    LOGFONTW lf{};
    lf.lfHeight = -DpiPx(owner, size);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
    return CreateFontIndirectW(&lf);
}

void FillRound(HDC dc, const RECT& r, int radius, uint32_t color) {
    HBRUSH brush = CreateSolidBrush(ToColorRef(color));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void StrokeRound(HDC dc, const RECT& r, int radius, uint32_t color) {
    HPEN pen = CreatePen(PS_SOLID, 1, ToColorRef(color));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawTextInRect(HDC dc, const std::wstring& text, RECT r, HFONT font,
                    uint32_t color, UINT flags) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ToColorRef(color));
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, flags);
    SelectObject(dc, oldFont);
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

void ApplyRoundRegion(HWND hwnd, int w, int h, int radius) {
    HRGN rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
    if (!rgn) return;
    if (!SetWindowRgn(hwnd, rgn, TRUE)) DeleteObject(rgn);
}

enum class ConfirmButton { None, Ok, Cancel };

struct ConfirmState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring message;
    Lang lang = Lang::Zh;
    bool danger = true;
    bool done = false;
    bool result = false;
    ConfirmButton hover = ConfirmButton::None;
    ConfirmButton pressed = ConfirmButton::None;
    int w = 0;
    int h = 0;
};

RECT ConfirmOkRect(const ConfirmState& s) {
    int bw = DpiPx(s.owner, 82), bh = DpiPx(s.owner, 30);
    int gap = DpiPx(s.owner, 8), pad = DpiPx(s.owner, 18);
    return RECT{ s.w - pad - bw, s.h - pad - bh, s.w - pad, s.h - pad };
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

void DrawButton(HDC dc, HWND owner, const RECT& r, const std::wstring& label,
                bool primary, bool hover, bool pressed) {
    int radius = DpiPx(owner, 9);
    uint32_t fill = primary ? Theme::kDanger : Theme::kPaper;
    if (primary && hover) fill = BlendColor(Theme::kDanger, 0xA95745, pressed ? 0.55f : 0.25f);
    if (!primary && hover) fill = BlendColor(Theme::kHover, Theme::kPaper, pressed ? 0.10f : 0.05f);
    FillRound(dc, r, radius, fill);
    StrokeRound(dc, r, radius, primary ? Theme::kDanger : Theme::kPaperEdge);
    HFONT font = CreateUiFont(owner, 12.0f, primary);
    RECT tr = r;
    DrawTextInRect(dc, label, tr, font, primary ? 0xFFFFFF : Theme::kText,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DeleteObject(font);
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
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);
        FillRound(dc, rc, DpiPx(s->owner, 16), Theme::kPaper);
        StrokeRound(dc, RECT{ 0, 0, rc.right, rc.bottom }, DpiPx(s->owner, 16), Theme::kPaperEdge);

        int pad = DpiPx(s->owner, 20);
        int icon = DpiPx(s->owner, 30);
        uint32_t accent = s->danger ? Theme::kDanger : Theme::kCheckFill;
        RECT iconRect{ pad, DpiPx(s->owner, 38), pad + icon, DpiPx(s->owner, 38) + icon };
        FillRound(dc, iconRect, icon, BlendColor(accent, Theme::kPaper, 0.12f));
        StrokeRound(dc, iconRect, icon, accent);
        HFONT titleFont = CreateUiFont(s->owner, 13.0f, true);
        HFONT textFont = CreateUiFont(s->owner, 13.0f, false);
        HFONT iconFont = CreateUiFont(s->owner, 15.0f, true);
        RECT ir = iconRect;
        DrawTextInRect(dc, L"!", ir, iconFont, accent,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT title{ pad + icon + DpiPx(s->owner, 12), DpiPx(s->owner, 26),
                    rc.right - pad, DpiPx(s->owner, 48) };
        DrawTextInRect(dc, L"X-TODO", title, titleFont, Theme::kText,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT msgRect{ title.left, DpiPx(s->owner, 52), rc.right - pad, DpiPx(s->owner, 112) };
        DrawTextInRect(dc, s->message, msgRect, textFont, Theme::kText,
                       DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        DeleteObject(iconFont);
        DeleteObject(textFont);
        DeleteObject(titleFont);

        RECT cancel = ConfirmCancelRect(*s);
        RECT ok = ConfirmOkRect(*s);
        DrawButton(dc, s->owner, cancel, T(Str::ConfirmCancel, s->lang),
                   false, s->hover == ConfirmButton::Cancel, s->pressed == ConfirmButton::Cancel);
        DrawButton(dc, s->owner, ok, T(Str::ConfirmOk, s->lang),
                   true, s->hover == ConfirmButton::Ok, s->pressed == ConfirmButton::Ok);
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowThemedConfirm(HWND owner, const wchar_t* text, Lang lang, bool danger) {
    const wchar_t* cls = L"XTodoConfirmPopup";
    if (!RegisterPopupClass(cls, ConfirmProc)) return false;
    ConfirmState state{};
    state.owner = owner;
    state.message = text ? text : L"";
    state.lang = lang;
    state.danger = danger;
    state.w = ClampPopupWidthToOwner(owner, DpiPx(owner, 300),
                                     DpiPx(owner, 220), DpiPx(owner, 300));
    state.h = DpiPx(owner, 168);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, cls, L"",
                                WS_POPUP, 0, 0, state.w, state.h,
                                owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return false;
    ApplyRoundRegion(hwnd, state.w, state.h, DpiPx(owner, 16));
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
};

struct PopupMenuState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    const std::vector<PopupMenuItem>* items = nullptr;
    bool done = false;
    UINT result = 0;
    int hover = -1;
    int w = 0;
    int h = 0;
    int rowH = 0;
    int sepH = 0;
};

int MenuItemAt(const PopupMenuState& s, int y) {
    if (!s.items) return -1;
    int top = DpiPx(s.owner, 6);
    for (size_t i = 0; i < s.items->size(); ++i) {
        const PopupMenuItem& item = (*s.items)[i];
        int h = item.separator ? s.sepH : s.rowH;
        if (y >= top && y < top + h)
            return (!item.separator && item.enabled) ? (int)i : -1;
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

void DrawCheckMark(HDC dc, const RECT& box, uint32_t color) {
    HPEN pen = CreatePen(PS_SOLID, 2, ToColorRef(color));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    int w = box.right - box.left;
    int h = box.bottom - box.top;
    MoveToEx(dc, box.left + w / 4, box.top + h / 2, nullptr);
    LineTo(dc, box.left + w / 2, box.top + h * 3 / 4);
    LineTo(dc, box.right - w / 5, box.top + h / 4);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

int MeasurePopupMenuWidth(HWND owner, const std::vector<PopupMenuItem>& items) {
    HDC dc = GetDC(owner);
    HFONT font = CreateUiFont(owner, 13.0f);
    HGDIOBJ oldFont = SelectObject(dc, font);
    int maxText = 0;
    for (const auto& item : items) {
        if (item.separator) continue;
        SIZE sz{};
        GetTextExtentPoint32W(dc, item.text.c_str(), (int)item.text.size(), &sz);
        int textW = sz.cx + DpiPx(owner, 50 + item.indent * 14);
        if (textW > maxText) maxText = textW;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(owner, dc);
    int minW = DpiPx(owner, 112);
    int preferred = maxText + DpiPx(owner, 4);
    return ClampPopupWidthToOwner(owner, preferred, minW, DpiPx(owner, 220));
}

int MeasurePopupMenuHeight(HWND owner, const std::vector<PopupMenuItem>& items) {
    int rowH = DpiPx(owner, 26), sepH = DpiPx(owner, 7);
    int h = DpiPx(owner, 12);
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
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);
        FillRound(dc, rc, DpiPx(s->owner, 12), Theme::kPaper);
        StrokeRound(dc, RECT{ 0, 0, rc.right, rc.bottom }, DpiPx(s->owner, 12), Theme::kPaperEdge);
        HFONT font = CreateUiFont(s->owner, 13.0f);
        int y = DpiPx(s->owner, 6);
        int pad = DpiPx(s->owner, 10);
        for (size_t i = 0; i < s->items->size(); ++i) {
            const PopupMenuItem& item = (*s->items)[i];
            if (item.separator) {
                int mid = y + s->sepH / 2;
                HPEN pen = CreatePen(PS_SOLID, 1, ToColorRef(Theme::kDivider));
                HGDIOBJ oldPen = SelectObject(dc, pen);
                MoveToEx(dc, pad, mid, nullptr);
                LineTo(dc, rc.right - pad, mid);
                SelectObject(dc, oldPen);
                DeleteObject(pen);
                y += s->sepH;
                continue;
            }
            RECT row{ DpiPx(s->owner, 6), y, rc.right - DpiPx(s->owner, 6), y + s->rowH };
            if ((int)i == s->hover)
                FillRound(dc, row, DpiPx(s->owner, 8), BlendColor(Theme::kHover, Theme::kPaper, 0.06f));
            if (item.checked) {
                RECT ck{ row.left + DpiPx(s->owner, 8), row.top + DpiPx(s->owner, 7),
                         row.left + DpiPx(s->owner, 20), row.top + DpiPx(s->owner, 19) };
                DrawCheckMark(dc, ck, Theme::kCheckFill);
            }
            RECT textR{ row.left + DpiPx(s->owner, 28 + item.indent * 14), row.top,
                        row.right - DpiPx(s->owner, 8), row.bottom };
            uint32_t textColor = item.enabled ? (item.danger ? Theme::kDanger : Theme::kText) : Theme::kTextWeak;
            DrawTextInRect(dc, item.text, textR, font, textColor,
                           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += s->rowH;
        }
        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
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
                    if (!(*s->items)[idx].separator && (*s->items)[idx].enabled) {
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
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

UINT ShowPopupMenu(HWND owner, POINT pt, const std::vector<PopupMenuItem>& items, bool alignRight) {
    const wchar_t* cls = L"XTodoPopupMenu";
    if (!RegisterPopupClass(cls, PopupMenuProc)) return 0;
    PopupMenuState state{};
    state.owner = owner;
    state.items = &items;
    state.rowH = DpiPx(owner, 26);
    state.sepH = DpiPx(owner, 7);
    state.w = MeasurePopupMenuWidth(owner, items);
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
    if (!hwnd) return 0;
    ApplyRoundRegion(hwnd, state.w, state.h, DpiPx(owner, 12));
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetCapture(hwnd);

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

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    lang_ = ui_.lang == "zh" ? Lang::Zh
          : ui_.lang == "en" ? Lang::En
          : SystemDefaultLang();
    capsuleStyle_ = ui_.capsuleStyle == "dot" ? CapsuleStyle::Dot : CapsuleStyle::Slim;
    mountMode_ = ui_.mountMode == "desktop" ? MountMode::Desktop
               : ui_.mountMode == "capsule" ? MountMode::Capsule
               : ui_.mountMode == "taskbar" ? MountMode::Taskbar
               : MountMode::Normal;
    ApplyMountMode(); // 应用持久化形态（含置顶 / 布局）

    if (loadResult == LoadResult::Failed) // 数据读取失败：告知用户（原文件已备份）
        MessageBoxW(hwnd_, T(Str::LoadFailedMsg, lang_), L"X-TODO", MB_OK | MB_ICONWARNING);
    return true;
}

void MainWindow::Show(bool expandCapsule) {
    if (mountMode_ == MountMode::Taskbar) { ShowFromTaskbarBand(); return; } // 任务栏模式：唤起完整窗口
    ShowWindow(hwnd_, SW_SHOW);
    if (expandCapsule && mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_)
        StartCapsuleAnim(true); // 主动唤起（托盘 / 菜单 / 第二实例）时滑出
    SetForegroundWindow(hwnd_);
}

void MainWindow::InitialShow() {
    // 冷启动：任务栏模式且状态条已就绪时只留状态条；绑定失败已回退 Normal 则正常显示。
    if (mountMode_ == MountMode::Taskbar && taskbarHwnd_ && IsWindow(taskbarHwnd_)) return;
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
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        AddTrayIcon(); // Explorer 重启后重新登记托盘图标
        if (mountMode_ == MountMode::Taskbar) {
            DestroyTaskbarBand();
            if (!EnsureTaskbarBand()) ScheduleTaskbarRetry();
        }
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
        if (mountMode_ == MountMode::Taskbar) {
            DestroyTaskbarBand();
            if (!EnsureTaskbarBand()) ScheduleTaskbarRetry();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (mountMode_ == MountMode::Taskbar) {
            if (!taskbarHwnd_ || !IsWindow(taskbarHwnd_)) {
                if (!EnsureTaskbarBand()) ScheduleTaskbarRetry();
            } else { LayoutTaskbarBand(); InvalidateTaskbarBand(); }
        }
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
        if (mountMode_ == MountMode::Taskbar && !IsWindowVisible(hwnd_)) { LayoutTaskbarBand(); InvalidateTaskbarBand(); return 0; }
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
        if (mountMode_ == MountMode::Capsule && capsuleExpanded_ && !animActive_ && !editing() && !menuOpen_)
            StartCapsuleAnim(false); // 鼠标离开展开的便签：滑回胶囊（菜单存活期间不收缩）
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
        } else if (wp == kTaskbarRetryTimerId) {
            KillTimer(hwnd_, kTaskbarRetryTimerId);
            if (mountMode_ == MountMode::Taskbar) {
                if (EnsureTaskbarBand()) {
                    taskbarRetryCount_ = 0; // 成功（含 transient 已自排重试）：清失败计数
                } else if (taskbarRetryCount_++ < 3) {
                    SetTimer(hwnd_, kTaskbarRetryTimerId, 1500, nullptr);
                } else {
                    taskbarRetryCount_ = 0;
                    mountMode_ = MountMode::Normal; ui_.mountMode = "normal";
                    ShowWindow(hwnd_, SW_SHOW); ApplyMountMode();
                    MessageBoxW(hwnd_, T(Str::TaskbarEmbedFailed, lang_), L"x-todo", MB_OK | MB_ICONWARNING);
                }
            }
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
        case 12: SetMountMode(MountMode::Taskbar);    break;
        case 30: SetCapsuleStyle(CapsuleStyle::Slim);
                 if (mountMode_ != MountMode::Capsule) SetMountMode(MountMode::Capsule); break;
        case 31: SetCapsuleStyle(CapsuleStyle::Dot);
                 if (mountMode_ != MountMode::Capsule) SetMountMode(MountMode::Capsule); break;
        case 20: SetLanguage(lang_ == Lang::Zh ? Lang::En : Lang::Zh); break;
        default: break;
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
        PopupMenuItem{ 12, T(Str::ModeTaskbar, lang_), false, mountMode_ == MountMode::Taskbar },
        PopupMenuItem{ 0, T(Str::ModeCapsule, lang_), false, false, false, false },
        PopupMenuItem{ 30, T(Str::StyleSlim, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Slim, false, true, 1 },
        PopupMenuItem{ 31, T(Str::StyleDot, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Dot, false, true, 1 },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 20, T(Str::ToggleLang, lang_) },
        PopupMenuItem{ 2, T(Str::Autostart, lang_), false, Autostart::IsEnabled() },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 3, T(Str::Exit, lang_), false, false, true }
    };

    POINT pt;
    GetCursorPos(&pt);
    menuOpen_ = true;
    UINT cmd = ShowPopupMenu(hwnd_, pt, items, false);
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
        PopupMenuItem{ 12, T(Str::ModeTaskbar, lang_), false, mountMode_ == MountMode::Taskbar },
        PopupMenuItem{ 0, T(Str::ModeCapsule, lang_), false, false, false, false },
        PopupMenuItem{ 30, T(Str::StyleSlim, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Slim, false, true, 1 },
        PopupMenuItem{ 31, T(Str::StyleDot, lang_), false, inCapsule && capsuleStyle_ == CapsuleStyle::Dot, false, true, 1 },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 20, T(Str::ToggleLang, lang_) },
        PopupMenuItem{ 2, T(Str::Autostart, lang_), false, Autostart::IsEnabled() },
        PopupMenuItem{ 0, L"", true },
        PopupMenuItem{ 3, T(Str::Exit, lang_), false, false, true }
    };

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    POINT pt{ rc.right - (LONG)S(6), (LONG)menuRect_.bottom }; // 贴近窗口右侧弹出
    ClientToScreen(hwnd_, &pt);
    menuOpen_ = true;
    UINT cmd = ShowPopupMenu(hwnd_, pt, items, true);
    menuOpen_ = false;                 // 必须在 HandleMenuCommand 前清，且无论返回值
    HandleMenuCommand(cmd);
    if (cmd != 3) MaybeCollapseCapsule(); // (R1-F001) 非退出：按真实光标位置补判（cmd==3 已 DestroyWindow）
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

    // 任务栏模式可能创建失败：成功才提交模式，失败保持原模式并提示一次
    if (m == MountMode::Taskbar) {
        if (!TryEnterTaskbarMode(true))
            MessageBoxW(hwnd_, T(Str::TaskbarEmbedFailed, lang_), L"x-todo", MB_OK | MB_ICONWARNING);
        return;
    }
    // 从任务栏模式切到其它模式：先销毁状态条，再走常规流程
    if (mountMode_ == MountMode::Taskbar) LeaveTaskbarMode();

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

    if (mountMode_ == MountMode::Taskbar) {
        if (EnsureTaskbarBand()) {
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        // 绑定失败（含冷启动 mount=taskbar）：回退 Normal 并提示一次
        DestroyTaskbarBand();
        mountMode_ = MountMode::Normal;
        ui_.mountMode = "normal";
        MessageBoxW(hwnd_, T(Str::TaskbarEmbedFailed, lang_), L"x-todo", MB_OK | MB_ICONWARNING);
    }

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
    UpdateCapsuleRegion(); // 形态 / 样式 / 折叠变化时同步窗口区域与 DWM 边界属性
}

// Dot 折叠态用 window region 定形成圆点；Slim 折叠态交给 DWM 圆角合成，避免 GDI region 硬边。
// 折叠态压制 DWM 边框线；其余形态恢复矩形 + ROUND。
void MainWindow::UpdateCapsuleRegion() {
    const bool slimShrunk = mountMode_ == MountMode::Capsule
                            && capsuleStyle_ == CapsuleStyle::Slim && capsuleShrunk();
    const bool dotShrunk  = mountMode_ == MountMode::Capsule
                            && capsuleStyle_ == CapsuleStyle::Dot  && capsuleShrunk();
    const bool suppressBorder = slimShrunk || dotShrunk;
    const bool regionShrunk = dotShrunk;

    int corner = dotShrunk ? 1 : 2; // 1=DWMWCP_DONOTROUND, 2=DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    COLORREF border = suppressBorder ? 0xFFFFFFFE /* DWMWA_COLOR_NONE */
                                     : 0xFFFFFFFF /* DWMWA_COLOR_DEFAULT */;
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));

    HRGN rgn = nullptr;
    if (dotShrunk) {
        RECT rc; GetClientRect(hwnd_, &rc);
        rgn = CreateEllipticRgn(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1);
    }

    if (regionShrunk) {
        // 创建失败：显式清旧 region（退化矩形），不残留上一样式的椭圆
        if (!rgn) { SetWindowRgn(hwnd_, nullptr, TRUE); return; }
        // SetWindowRgn 失败时系统未接管，调用方仍拥有 HRGN，须释放；并清旧 region 退化矩形
        if (!SetWindowRgn(hwnd_, rgn, TRUE)) { DeleteObject(rgn); SetWindowRgn(hwnd_, nullptr, TRUE); }
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
    return ShowThemedConfirm(hwnd_, T(message, lang_), lang_, true);
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
    DestroyTaskbarBand(); // 任务栏状态条是 Explorer 子窗口，退出前显式销毁
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
    if (mountMode_ == MountMode::Taskbar) CaptureTaskbarDockFromBand();
    if (!Store::Save(model_, geom_, ui_)) {
        // 保存失败（磁盘满 / 占用 / 权限）：保留待存标记并延迟重试，不静默丢改动
        savePending_ = true;
        SetTimer(hwnd_, kSaveTimerId, 3000, nullptr);
    }
}

void MainWindow::CaptureGeometry() {
    // 任务栏模式下主窗口隐藏，不能用其矩形污染 geom_（AC-12）
    if (mountMode_ == MountMode::Taskbar && !IsWindowVisible(hwnd_)) return;
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
