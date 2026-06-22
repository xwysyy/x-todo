#include "ThemeManagerWindow.h"
#include "ThemedWindowControls.h"

#include <windowsx.h>
#include <cwchar>
#include <string>
#include <vector>

namespace Theme {
namespace {

constexpr wchar_t kManagerClass[] = L"XTodoThemeManagerWindow";
namespace Ui = ThemedWindow;

template <class T> void SafeRelease(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

enum class RowKind { Header, ThemeItem, Text, Button };
enum class BtnAction { None, Reload, OpenFolder, Export, SetLight, SetDark, Close };

struct Row {
    RowKind      kind;
    std::wstring text;
    std::string  themeId;            // ThemeItem
    bool         current = false;    // ThemeItem：是否当前主题
    BtnAction    action = BtnAction::None;
    int          top = 0, height = 0; // 文档坐标
    bool clickable() const { return kind == RowKind::ThemeItem || kind == RowKind::Button; }
};

struct ManagerState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    ManagerHost* host = nullptr;
    std::vector<Row> rows;
    int hover = -1;
    int scroll = 0;
    int contentH = 0;
    int w = 0, h = 0;
    bool done = false;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* headFmt = nullptr;
    IDWriteTextFormat* itemFmt = nullptr;
    IDWriteTextFormat* textFmt = nullptr;
    IDWriteTextFormat* buttonFmt = nullptr;
};

const wchar_t* ModeLabel(const ManagerHost& h) {
    if (h.currentMode == "follow_system") return T(Str::ThemeFollowSystem, h.lang);
    if (h.currentMode == "custom")        return T(Str::ThemeCustom, h.lang);
    return T(Str::ThemeHeader, h.lang);
}

void BuildRows(ManagerState& s) {
    s.rows.clear();
    ManagerHost& h = *s.host;
    auto add = [&](RowKind k, std::wstring t) -> Row& { s.rows.push_back(Row{ k, std::move(t) }); return s.rows.back(); };
    auto label = [&](const ThemeVisual& t) { return h.lang == Lang::Zh ? t.name.zh : t.name.en; };

    // 当前模式 + 解析出的 id
    std::wstring head = std::wstring(T(Str::ThemeManager, h.lang)) + L"  ·  " + ModeLabel(h);
    add(RowKind::Header, head);
    {
        std::wstring idw(h.currentId.begin(), h.currentId.end());
        add(RowKind::Text, L"id: " + idw);
    }

    // 内置主题
    add(RowKind::Header, T(Str::ThemeHeader, h.lang));
    for (const auto& t : h.builtins) {
        Row& r = add(RowKind::ThemeItem, label(t));
        r.themeId = t.id;
        r.current = (t.id == h.currentId);
    }

    // 自定义主题
    add(RowKind::Header, T(Str::ThemeCustom, h.lang));
    if (h.customs.empty()) {
        add(RowKind::Text, h.lang == Lang::Zh ? L"（无）" : L"(none)");
    } else {
        for (const auto& t : h.customs) {
            Row& r = add(RowKind::ThemeItem, label(t));
            r.themeId = t.id;
            r.current = (t.id == h.currentId);
        }
    }

    // 加载 issue
    if (!h.issues.empty()) {
        add(RowKind::Header, T(Str::ThemeIssues, h.lang));
        for (const auto& is : h.issues)
            add(RowKind::Text, is.fileName + L"：" + is.detail);
    }
    // 运行时 notice
    if (!h.notices.empty()) {
        add(RowKind::Header, T(Str::ThemeNotices, h.lang));
        for (const auto& n : h.notices)
            add(RowKind::Text, n.message);
    }

    // 操作按钮
    add(RowKind::Header, L"");
    { Row& r = add(RowKind::Button, T(Str::ThemeReload, h.lang));        r.action = BtnAction::Reload; }
    { Row& r = add(RowKind::Button, T(Str::ThemeOpenFolder, h.lang));    r.action = BtnAction::OpenFolder; }
    { Row& r = add(RowKind::Button, T(Str::ThemeExportCurrent, h.lang)); r.action = BtnAction::Export; }
    { Row& r = add(RowKind::Button, T(Str::ThemeSetLightFollow, h.lang));r.action = BtnAction::SetLight; }
    { Row& r = add(RowKind::Button, T(Str::ThemeSetDarkFollow, h.lang)); r.action = BtnAction::SetDark; }
    { Row& r = add(RowKind::Button, h.lang == Lang::Zh ? L"关闭" : L"Close"); r.action = BtnAction::Close; }
}

void LayoutRows(ManagerState& s) {
    int pad   = Ui::Px(s.hwnd, 8);
    int itemH = Ui::Px(s.hwnd, 24);
    int headH = Ui::Px(s.hwnd, 22);
    int txtH  = Ui::Px(s.hwnd, 18);
    int btnH  = Ui::Px(s.hwnd, 26);
    int y = pad;
    for (auto& r : s.rows) {
        r.top = y;
        switch (r.kind) {
            case RowKind::ThemeItem: r.height = itemH; break;
            case RowKind::Button:    r.height = btnH;  break;
            case RowKind::Text:      r.height = txtH;  break;
            default:                 r.height = headH; break;
        }
        y += r.height;
    }
    s.contentH = y + pad;
}

void ClampScroll(ManagerState& s) {
    int maxScroll = s.contentH - s.h;
    if (maxScroll < 0) maxScroll = 0;
    if (s.scroll < 0) s.scroll = 0;
    if (s.scroll > maxScroll) s.scroll = maxScroll;
}

int HitRow(ManagerState& s, int y) {
    int docY = y + s.scroll;
    for (size_t i = 0; i < s.rows.size(); ++i) {
        const Row& r = s.rows[i];
        if (r.clickable() && docY >= r.top && docY < r.top + r.height) return (int)i;
    }
    return -1;
}

bool CreateTextFormats(ManagerState& s) {
    return Ui::CreateTextFormat(s.dwrite, s.hwnd, 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.headFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.itemFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 9.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.textFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_CENTER,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.buttonFmt);
}

void ReleaseDrawingResources(ManagerState& s) {
    SafeRelease(&s.buttonFmt);
    SafeRelease(&s.textFmt);
    SafeRelease(&s.itemFmt);
    SafeRelease(&s.headFmt);
    SafeRelease(&s.brush);
    SafeRelease(&s.rt);
}

void Paint(ManagerState& s) {
    PAINTSTRUCT ps{};
    BeginPaint(s.hwnd, &ps);
    if ((!s.rt || !s.brush) &&
        !Ui::CreateDeviceResources(s.hwnd, s.d2dFactory, &s.rt, &s.brush)) {
        EndPaint(s.hwnd, &ps);
        return;
    }
    RECT rc{}; GetClientRect(s.hwnd, &rc);

    const ColorSet& c = s.host->current.colors;
    s.rt->BeginDraw();
    s.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    Ui::FillColor(s.rt, c.paperElevated);

    int padX = Ui::Px(s.hwnd, 12);

    for (size_t i = 0; i < s.rows.size(); ++i) {
        const Row& r = s.rows[i];
        int top = r.top - s.scroll;
        if (top + r.height < 0 || top > rc.bottom) continue; // 视口外跳过
        RECT rr{ padX, top, rc.right - padX, top + r.height };

        if (r.kind == RowKind::Header) {
            if (!r.text.empty())
                Ui::RenderText(s.rt, s.brush, r.text, rr, s.headFmt, c.text);
        } else if (r.kind == RowKind::Text) {
            Ui::RenderText(s.rt, s.brush, r.text, rr, s.textFmt, c.textWeak);
        } else if (r.kind == RowKind::ThemeItem) {
            RECT row{ Ui::Px(s.hwnd, 8), top + 1, rc.right - Ui::Px(s.hwnd, 8), top + r.height - 1 };
            if ((int)i == s.hover)
                Ui::FillRoundedRect(s.rt, s.brush, row, static_cast<float>(Ui::Px(s.hwnd, 6)), c.menuHover);
            int dotR = Ui::Px(s.hwnd, 4);
            int cx = padX + dotR, cy = top + r.height / 2;
            RECT dot{ cx - dotR, cy - dotR, cx + dotR, cy + dotR };
            Ui::FillRoundedRect(s.rt, s.brush, dot, static_cast<float>(dotR),
                              r.current ? c.checkFill : c.paperElevated);
            Ui::StrokeRoundedRect(s.rt, s.brush, dot, static_cast<float>(dotR),
                                r.current ? c.checkFill : c.checkBorder);
            RECT tr{ padX + dotR * 2 + Ui::Px(s.hwnd, 8), top, rc.right - padX, top + r.height };
            Ui::RenderText(s.rt, s.brush, r.text, tr, s.itemFmt, c.text);
        } else if (r.kind == RowKind::Button) {
            RECT btn{ padX, top + 2, rc.right - padX, top + r.height - 2 };
            uint32_t fill = ((int)i == s.hover) ? c.buttonHover : c.paperElevated;
            const float radius = static_cast<float>(Ui::Px(s.hwnd, 7));
            Ui::FillRoundedRect(s.rt, s.brush, btn, radius, fill);
            Ui::StrokeRoundedRect(s.rt, s.brush, btn, radius, c.paperEdge);
            Ui::RenderText(s.rt, s.brush, r.text, btn, s.buttonFmt, c.text);
        }
    }

    Ui::StrokeRoundedRect(s.rt, s.brush, RECT{ 0, 0, rc.right, rc.bottom },
                        static_cast<float>(Ui::Px(s.hwnd, 10)), c.paperEdge);

    HRESULT hr = s.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(&s.brush);
        SafeRelease(&s.rt);
    }
    EndPaint(s.hwnd, &ps);
}

void DoAction(ManagerState& s, const Row& r) {
    ManagerHost& h = *s.host;
    if (r.kind == RowKind::ThemeItem) {
        if (h.applyTheme) h.applyTheme(r.themeId);
    } else if (r.kind == RowKind::Button) {
        switch (r.action) {
            case BtnAction::Reload:     if (h.reload) h.reload(); break;
            case BtnAction::OpenFolder: if (h.openFolder) h.openFolder(); break;
            case BtnAction::Export:     if (h.exportCurrent) h.exportCurrent(); break;
            case BtnAction::SetLight:   if (h.setLightFollow) h.setLightFollow(h.currentId); break;
            case BtnAction::SetDark:    if (h.setDarkFollow) h.setDarkFollow(h.currentId); break;
            case BtnAction::Close:
                s.done = true;
                DestroyWindow(s.hwnd);
                return;
            default: break;
        }
    }
    // 操作后宿主已刷新 host：重建行模型并重绘（实时反映主题变化）
    BuildRows(s);
    LayoutRows(s);
    ClampScroll(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

LRESULT CALLBACK ManagerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ManagerState* s = reinterpret_cast<ManagerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = static_cast<ManagerState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: Paint(*s); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (s->rt) {
            UINT width = LOWORD(lp);
            UINT height = HIWORD(lp);
            if (width > 0 && height > 0)
                s->rt->Resize(D2D1::SizeU(width, height));
        }
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        int hit = HitRow(*s, GET_Y_LPARAM(lp));
        if (hit != s->hover) { s->hover = hit; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (s->hover != -1) { s->hover = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        s->scroll -= (delta / 120) * Ui::Px(hwnd, 36);
        ClampScroll(*s);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        int hit = HitRow(*s, GET_Y_LPARAM(lp));
        if (hit >= 0) DoAction(*s, s->rows[hit]);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { s->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_CLOSE:
        s->done = true; DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        ReleaseDrawingResources(*s);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterManagerClass() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ManagerProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kManagerClass;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

void ShowThemeManagerWindow(HWND owner, ManagerHost& host,
                            ID2D1Factory* d2dFactory, IDWriteFactory* dwrite) {
    if (!RegisterManagerClass() || !d2dFactory || !dwrite) return;

    ManagerState state{};
    state.owner = owner;
    state.host = &host;
    state.d2dFactory = d2dFactory;
    state.dwrite = dwrite;

    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    state.w = MulDiv(300, dpi, 96);
    state.h = MulDiv(440, dpi, 96);

    // 居中于 owner 所在显示器
    int x = MulDiv(120, dpi, 96), y = MulDiv(80, dpi, 96);
    HMONITOR mon = MonitorFromWindow(owner ? owner : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - state.w) / 2;
        y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - state.h) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kManagerClass,
                                L"X-TODO", WS_POPUP | WS_CLIPCHILDREN,
                                x, y, state.w, state.h,
                                owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return;
    Ui::ApplyPopupRoundShape(hwnd, state.w, state.h, Ui::Px(hwnd, 14));
    if (!Ui::CreateDeviceResources(hwnd, d2dFactory, &state.rt, &state.brush) ||
        !CreateTextFormats(state)) {
        ReleaseDrawingResources(state);
        DestroyWindow(hwnd);
        return;
    }

    BuildRows(state);
    LayoutRows(state);
    ClampScroll(state);

    if (owner) EnableWindow(owner, FALSE);
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
    if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
}

} // namespace Theme
