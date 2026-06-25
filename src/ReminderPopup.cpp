#include "ReminderPopup.h"

#include "ReminderPopupPolicy.h"
#include "ReminderText.h"
#include "ThemedWindowControls.h"

#include <windowsx.h>

namespace ReminderPopup {
namespace {

constexpr wchar_t kPopupClass[] = L"XTodoReminderPopup";
constexpr UINT_PTR kDismissTimerId = 1;
namespace Ui = ThemedWindow;

template <class T>
void SafeRelease(T** ptr) {
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

struct State {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    UINT openMessage = 0;
    int blockId = -1;
    Lang lang = Lang::Zh;
    Theme::ThemeVisual theme;
    ReminderDisplayText text;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* titleFmt = nullptr;
    IDWriteTextFormat* bodyFmt = nullptr;
    IDWriteTextFormat* buttonFmt = nullptr;
    RECT openRect{};
    RECT dismissRect{};
    bool hoverOpen = false;
    bool hoverDismiss = false;
};

void ReleaseDrawingResources(State& s) {
    SafeRelease(&s.buttonFmt);
    SafeRelease(&s.bodyFmt);
    SafeRelease(&s.titleFmt);
    SafeRelease(&s.brush);
    SafeRelease(&s.rt);
}

bool EnsureResources(State& s) {
    if ((!s.rt || !s.brush) &&
        !Ui::CreateDeviceResources(s.hwnd, s.d2dFactory, &s.rt, &s.brush)) {
        return false;
    }
    if (!s.titleFmt &&
        !Ui::CreateTextFormat(s.dwrite, s.hwnd, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_TEXT_ALIGNMENT_LEADING,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.titleFmt)) {
        return false;
    }
    if (!s.bodyFmt &&
        !Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_TEXT_ALIGNMENT_LEADING,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.bodyFmt)) {
        return false;
    }
    if (!s.buttonFmt &&
        !Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              DWRITE_TEXT_ALIGNMENT_CENTER,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.buttonFmt)) {
        return false;
    }
    return true;
}

bool PtInRectLocal(const RECT& rect, int x, int y) {
    POINT pt{ x, y };
    return PtInRect(&rect, pt) != FALSE;
}

void LayoutButtons(State& s, const RECT& rc) {
    const int pad = Ui::Px(s.hwnd, 14);
    const int buttonW = Ui::Px(s.hwnd, 74);
    const int buttonH = Ui::Px(s.hwnd, 28);
    const int gap = Ui::Px(s.hwnd, 8);
    const int bottom = rc.bottom - pad;
    s.dismissRect = RECT{ rc.right - pad - buttonW, bottom - buttonH,
                          rc.right - pad, bottom };
    s.openRect = RECT{ s.dismissRect.left - gap - buttonW, s.dismissRect.top,
                       s.dismissRect.left - gap, s.dismissRect.bottom };
}

void DrawButton(State& s, RECT rect, const wchar_t* label, bool primary, bool hovered) {
    const Theme::ColorSet& c = s.theme.colors;
    const uint32_t fill = primary ? c.checkFill
                          : (hovered ? c.buttonHover : c.paperElevated);
    const uint32_t edge = primary ? c.checkFill : c.paperEdge;
    const uint32_t text = primary ? c.checkMark : c.text;
    const float radius = static_cast<float>(Ui::Px(s.hwnd, 7));
    Ui::FillRoundedRect(s.rt, s.brush, rect, radius, fill);
    Ui::StrokeRoundedRect(s.rt, s.brush, rect, radius, edge);
    Ui::RenderText(s.rt, s.brush, label, rect, s.buttonFmt, text);
}

void Paint(State& s) {
    PAINTSTRUCT ps{};
    BeginPaint(s.hwnd, &ps);
    if (!EnsureResources(s)) {
        EndPaint(s.hwnd, &ps);
        return;
    }

    RECT rc{};
    GetClientRect(s.hwnd, &rc);
    LayoutButtons(s, rc);

    const Theme::ColorSet& c = s.theme.colors;
    s.rt->BeginDraw();
    s.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    Ui::FillColor(s.rt, c.paperElevated);

    const float radius = static_cast<float>(Ui::Px(s.hwnd, 12));
    RECT frame{ 0, 0, rc.right, rc.bottom };
    Ui::StrokeRoundedRect(s.rt, s.brush, frame, radius, c.paperEdge);

    RECT accent{ Ui::Px(s.hwnd, 10), Ui::Px(s.hwnd, 12),
                 Ui::Px(s.hwnd, 14), rc.bottom - Ui::Px(s.hwnd, 12) };
    Ui::FillRoundedRect(s.rt, s.brush, accent,
                        static_cast<float>(Ui::Px(s.hwnd, 2)), c.focusRing);

    RECT title{ Ui::Px(s.hwnd, 24), Ui::Px(s.hwnd, 12),
                rc.right - Ui::Px(s.hwnd, 14), Ui::Px(s.hwnd, 38) };
    Ui::RenderText(s.rt, s.brush, s.text.title, title, s.titleFmt, c.text);

    const int lineH = Ui::Px(s.hwnd, 22);
    const int maxLines = 3;
    for (size_t i = 0; i < s.text.lines.size() && i < static_cast<size_t>(maxLines); ++i) {
        RECT line{ Ui::Px(s.hwnd, 24), Ui::Px(s.hwnd, 42) + static_cast<int>(i) * lineH,
                   rc.right - Ui::Px(s.hwnd, 14),
                   Ui::Px(s.hwnd, 42) + static_cast<int>(i + 1) * lineH };
        Ui::RenderText(s.rt, s.brush, s.text.lines[i], line, s.bodyFmt, c.textWeak);
    }

    DrawButton(s, s.openRect, T(Str::ReminderOpen, s.lang), true, s.hoverOpen);
    DrawButton(s, s.dismissRect, T(Str::ReminderDismiss, s.lang), false, s.hoverDismiss);

    HRESULT hr = s.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(&s.brush);
        SafeRelease(&s.rt);
    }
    EndPaint(s.hwnd, &ps);
}

bool RegisterPopupClass() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            s = static_cast<State*>(cs->lpCreateParams);
            s->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            return TRUE;
        }
        if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, kDismissTimerId, 10000, nullptr);
            return 0;
        case WM_TIMER:
            if (wp == kDismissTimerId) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            const bool open = PtInRectLocal(s->openRect, x, y);
            const bool dismiss = PtInRectLocal(s->dismissRect, x, y);
            if (open != s->hoverOpen || dismiss != s->hoverDismiss) {
                s->hoverOpen = open;
                s->hoverDismiss = dismiss;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            SetCursor(LoadCursorW(nullptr, (open || dismiss) ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            s->hoverOpen = false;
            s->hoverDismiss = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            if (PtInRectLocal(s->openRect, x, y) && s->owner && s->openMessage && s->blockId > 0) {
                PostMessageW(s->owner, s->openMessage, static_cast<WPARAM>(s->blockId), 0);
                DestroyWindow(hwnd);
                return 0;
            }
            if (PtInRectLocal(s->dismissRect, x, y)) {
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        case WM_RBUTTONUP:
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_PAINT:
            Paint(*s);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kDismissTimerId);
            ReleaseDrawingResources(*s);
            delete s;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPopupClass;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

bool Show(HWND owner, const std::vector<ReminderCandidate>& reminders, Lang lang,
          const Theme::ThemeVisual& theme, ID2D1Factory* d2dFactory,
          IDWriteFactory* dwriteFactory, UINT openMessage) {
    if (!owner || reminders.empty() || !d2dFactory || !dwriteFactory || !RegisterPopupClass())
        return false;

    auto* state = new State();
    state->owner = owner;
    state->openMessage = openMessage;
    state->blockId = reminders.front().blockId;
    state->lang = lang;
    state->theme = theme;
    state->text = FormatReminderText(reminders, lang);
    state->d2dFactory = d2dFactory;
    state->dwrite = dwriteFactory;
    if (state->text.title.empty()) {
        delete state;
        return false;
    }

    UINT dpi = GetDpiForWindow(owner);
    RECT work{};
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    const ReminderPopupPlacement placement = ComputeReminderPopupPlacement(
        ReminderPopupWorkArea{work.left, work.top, work.right, work.bottom}, dpi);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                kPopupClass, L"", WS_POPUP | WS_CLIPCHILDREN,
                                placement.x, placement.y, placement.width, placement.height,
                                nullptr, nullptr,
                                GetModuleHandleW(nullptr), state);
    if (!hwnd) {
        delete state;
        return false;
    }
    Ui::ApplyPopupRoundShape(hwnd, placement.width, placement.height,
                             ReminderPopupScale(20, dpi));
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return true;
}

} // namespace ReminderPopup
