#include "ThemedWindowControls.h"

#include <dwmapi.h>
#include <cwchar>
#include <algorithm>
#include <utility>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

namespace ThemedWindow {
namespace {

template <class T> void SafeRelease(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

} // namespace

int Px(HWND hwnd, float value) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return static_cast<int>(value * dpi / 96.0f + 0.5f);
}

float DpiScale(HWND hwnd) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    return static_cast<float>(dpi) / 96.0f;
}

D2D1_RECT_F RectF(const RECT& rect) {
    return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                       static_cast<float>(rect.right), static_cast<float>(rect.bottom));
}

bool CreateDeviceResources(HWND hwnd, ID2D1Factory* factory,
                           ID2D1HwndRenderTarget** target,
                           ID2D1SolidColorBrush** brush) {
    if (!hwnd || !factory || !target || !brush) return false;
    SafeRelease(brush);
    SafeRelease(target);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const UINT width = static_cast<UINT>(rc.right - rc.left);
    const UINT height = static_cast<UINT>(rc.bottom - rc.top);
    if (width == 0 || height == 0) return false;

    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    if (FAILED(factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                               D2D1::HwndRenderTargetProperties(hwnd, size),
                                               target))) {
        return false;
    }
    (*target)->SetDpi(96.0f, 96.0f);
    if (FAILED((*target)->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), brush))) {
        SafeRelease(target);
        return false;
    }
    return true;
}

bool CreateTextFormat(IDWriteFactory* factory, HWND hwnd, float size,
                      DWRITE_FONT_WEIGHT weight,
                      DWRITE_TEXT_ALIGNMENT textAlignment,
                      DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment,
                      IDWriteTextFormat** format) {
    if (!factory || !format) return false;
    SafeRelease(format);
    if (FAILED(factory->CreateTextFormat(Theme::kFontFamily, nullptr, weight,
                                         DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL,
                                         size * DpiScale(hwnd), L"", format))) {
        return false;
    }
    (*format)->SetTextAlignment(textAlignment);
    (*format)->SetParagraphAlignment(paragraphAlignment);
    (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    IDWriteInlineObject* sign = nullptr;
    DWRITE_TRIMMING trimming{};
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(*format, &sign))) {
        (*format)->SetTrimming(&trimming, sign);
        sign->Release();
    }
    return true;
}

void FillColor(ID2D1RenderTarget* target, uint32_t color) {
    if (!target) return;
    target->Clear(Theme::Color(color));
}

void FillRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
              RECT rect, uint32_t color) {
    if (!target || !brush) return;
    brush->SetColor(Theme::Color(color));
    target->FillRectangle(RectF(rect), brush);
}

void FillRoundedRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                   RECT rect, float radius, uint32_t color) {
    if (!target || !brush) return;
    brush->SetColor(Theme::Color(color));
    D2D1_ROUNDED_RECT rr{ RectF(rect), radius, radius };
    target->FillRoundedRectangle(rr, brush);
}

void StrokeRoundedRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                     RECT rect, float radius, uint32_t color,
                     float strokeWidth) {
    if (!target || !brush) return;
    brush->SetColor(Theme::Color(color));
    D2D1_ROUNDED_RECT rr{ RectF(rect), radius, radius };
    target->DrawRoundedRectangle(rr, brush, strokeWidth);
}

void DrawLine(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
              float x1, float y1, float x2, float y2,
              uint32_t color, float strokeWidth) {
    if (!target || !brush) return;
    brush->SetColor(Theme::Color(color));
    target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, strokeWidth);
}

void RenderText(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                const std::wstring& text, RECT rect,
                IDWriteTextFormat* format, uint32_t color,
                D2D1_DRAW_TEXT_OPTIONS options) {
    if (!target || !brush || !format) return;
    brush->SetColor(Theme::Color(color));
    target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
                      RectF(rect), brush, options);
}

float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
                       const std::wstring& text) {
    if (!factory || !format || text.empty()) return 0.0f;

    IDWriteTextLayout* layout = nullptr;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                         format, 100000.0f, 1000.0f, &layout)) ||
        !layout) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    layout->Release();
    return std::max(metrics.width, metrics.widthIncludingTrailingWhitespace);
}

std::wstring ElideMiddle(IDWriteFactory* factory, IDWriteTextFormat* format,
                         const std::wstring& text, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return text;

    auto fits = [&](const std::wstring& s) {
        return MeasureTextWidth(factory, format, s) <= maxWidth;
    };

    if (fits(text)) return text;

    const std::wstring marker = L"...";
    if (!fits(marker)) return marker;

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
