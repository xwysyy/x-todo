#pragma once

#include "Theme.h"

#include <windows.h>
#include <string>

namespace ThemedWindow {

int Px(HWND hwnd, float value);
HFONT CreateTextFont(HWND hwnd, float size, bool bold = false);

void FillColor(HDC dc, RECT rect, uint32_t color);
void FillRoundColor(HDC dc, RECT rect, int radius, uint32_t color);
void StrokeRoundColor(HDC dc, RECT rect, int radius, uint32_t color);
void DrawTextInRect(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                    uint32_t color, UINT flags);

std::wstring ElideMiddle(HDC dc, HFONT font, const std::wstring& text, int maxWidth);
void ApplyPopupRoundShape(HWND hwnd, int width, int height, int regionRadius);

} // namespace ThemedWindow
