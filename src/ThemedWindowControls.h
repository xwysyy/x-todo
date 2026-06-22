#pragma once

#include "Theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <string>

namespace ThemedWindow {

int Px(HWND hwnd, float value);
float DpiScale(HWND hwnd);
D2D1_RECT_F RectF(const RECT& rect);

bool CreateDeviceResources(HWND hwnd, ID2D1Factory* factory,
                           ID2D1HwndRenderTarget** target,
                           ID2D1SolidColorBrush** brush);
bool CreateTextFormat(IDWriteFactory* factory, HWND hwnd, float size,
                      DWRITE_FONT_WEIGHT weight,
                      DWRITE_TEXT_ALIGNMENT textAlignment,
                      DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment,
                      IDWriteTextFormat** format);

void FillColor(ID2D1RenderTarget* target, uint32_t color);
void FillRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
              RECT rect, uint32_t color);
void FillRoundedRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                   RECT rect, float radius, uint32_t color);
void StrokeRoundedRect(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                     RECT rect, float radius, uint32_t color,
                     float strokeWidth = 1.0f);
void DrawLine(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
              float x1, float y1, float x2, float y2,
              uint32_t color, float strokeWidth = 1.0f);
void RenderText(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                const std::wstring& text, RECT rect,
                IDWriteTextFormat* format, uint32_t color,
                D2D1_DRAW_TEXT_OPTIONS options = D2D1_DRAW_TEXT_OPTIONS_CLIP);

float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
                       const std::wstring& text);
std::wstring ElideMiddle(IDWriteFactory* factory, IDWriteTextFormat* format,
                         const std::wstring& text, float maxWidth);
void ApplyPopupRoundShape(HWND hwnd, int width, int height, int regionRadius);

} // namespace ThemedWindow
