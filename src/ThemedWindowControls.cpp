#include "ThemedWindowControls.h"

#include <dwmapi.h>
#include <cwchar>
#include <utility>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace ThemedWindow {

int Px(HWND hwnd, float value) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return static_cast<int>(value * dpi / 96.0f + 0.5f);
}

HFONT CreateTextFont(HWND hwnd, float size, bool bold) {
    LOGFONTW lf{};
    lf.lfHeight  = -Px(hwnd, size);
    lf.lfWeight  = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
    return CreateFontIndirectW(&lf);
}

void FillColor(HDC dc, RECT rect, uint32_t color) {
    HBRUSH brush = CreateSolidBrush(Theme::GdiColor(color));
    ::FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void FillRoundColor(HDC dc, RECT rect, int radius, uint32_t color) {
    HBRUSH brush = CreateSolidBrush(Theme::GdiColor(color));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

void StrokeRoundColor(HDC dc, RECT rect, int radius, uint32_t color) {
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::GdiColor(color));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawTextInRect(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                    uint32_t color, UINT flags) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, Theme::GdiColor(color));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, oldFont);
}

std::wstring ElideMiddle(HDC dc, HFONT font, const std::wstring& text, int maxWidth) {
    if (text.empty() || maxWidth <= 0) return text;

    HGDIOBJ oldFont = SelectObject(dc, font);
    auto fits = [&](const std::wstring& s) {
        SIZE size{};
        GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &size);
        return size.cx <= maxWidth;
    };

    if (fits(text)) {
        SelectObject(dc, oldFont);
        return text;
    }

    const std::wstring marker = L"...";
    if (!fits(marker)) {
        SelectObject(dc, oldFont);
        return marker;
    }

    size_t left = 0;
    size_t right = 0;
    std::wstring best = marker;
    while (left + right < text.size()) {
        const bool takeLeft = left <= right;
        if (takeLeft) ++left;
        else ++right;

        std::wstring candidate = text.substr(0, left) + marker +
                                 text.substr(text.size() - right);
        if (!fits(candidate)) {
            if (takeLeft) --left;
            else --right;
            break;
        }
        best = std::move(candidate);
    }

    SelectObject(dc, oldFont);
    return best;
}

void ApplyPopupRoundShape(HWND hwnd, int width, int height, int regionRadius) {
    int corner = 3; // DWMWCP_ROUNDSMALL
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &corner, sizeof(corner));
    if (SUCCEEDED(hr)) {
        COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        return;
    }

    HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, regionRadius, regionRadius);
    if (rgn && !SetWindowRgn(hwnd, rgn, TRUE)) DeleteObject(rgn);
}

} // namespace ThemedWindow
