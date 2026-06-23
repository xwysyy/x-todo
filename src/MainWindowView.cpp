#include "MainWindow.h"
#include "CalendarDate.h"
#include "CalendarTheme.h"
#include "EditIntent.h"
#include "Theme.h"
#include "ViewLayout.h"
#include "WindowHitTest.h"

#include <algorithm>
#include <commctrl.h>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include <utility>

namespace {
bool InRect(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

Gui::Rect ToGuiRect(const D2D1_RECT_F& r) {
    return Gui::Rect{ r.left, r.top, r.right, r.bottom };
}

D2D1_RECT_F ToD2DRect(const Gui::Rect& r) {
    return D2D1::RectF(r.left, r.top, r.right, r.bottom);
}

std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

int RoundToInt(float v) { return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f); }
constexpr int kHoverEmptyActive = -2;
constexpr int kHoverAddTask = -3;

struct PixelCanvas {
    uint32_t* pixels;
    int w;
    int h;
};

double Clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

int ClampByte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

void BlendPixel(const PixelCanvas& c, int x, int y, uint32_t rgb, int alpha) {
    if (x < 0 || y < 0 || x >= c.w || y >= c.h || alpha <= 0) return;
    alpha = ClampByte(alpha);
    uint32_t& dst = c.pixels[y * c.w + x];
    const int da = (dst >> 24) & 0xff;
    const int dr = (dst >> 16) & 0xff;
    const int dg = (dst >> 8) & 0xff;
    const int db = dst & 0xff;
    const int sr = (((rgb >> 16) & 0xff) * alpha + 127) / 255;
    const int sg = (((rgb >> 8) & 0xff) * alpha + 127) / 255;
    const int sb = ((rgb & 0xff) * alpha + 127) / 255;
    const int inv = 255 - alpha;
    const int oa = alpha + (da * inv + 127) / 255;
    const int orr = sr + (dr * inv + 127) / 255;
    const int og = sg + (dg * inv + 127) / 255;
    const int ob = sb + (db * inv + 127) / 255;
    dst = ((uint32_t)oa << 24) | ((uint32_t)orr << 16) | ((uint32_t)og << 8) | (uint32_t)ob;
}

double RoundRectSignedDistance(double x, double y, double l, double t, double r, double b, double radius) {
    const double cx = (l + r) * 0.5;
    const double cy = (t + b) * 0.5;
    double hx = (r - l) * 0.5 - radius;
    double hy = (b - t) * 0.5 - radius;
    if (hx < 0.0) hx = 0.0;
    if (hy < 0.0) hy = 0.0;
    const double qx = std::fabs(x - cx) - hx;
    const double qy = std::fabs(y - cy) - hy;
    const double ox = qx > 0.0 ? qx : 0.0;
    const double oy = qy > 0.0 ? qy : 0.0;
    const double outside = std::sqrt(ox * ox + oy * oy);
    const double inside = (qx > qy ? qx : qy);
    return outside + (inside < 0.0 ? inside : 0.0) - radius;
}

void FillRoundRectPixels(const PixelCanvas& c, double l, double t, double r, double b,
                         double radius, uint32_t rgb, int alpha) {
    const int x0 = (int)std::floor(l - 1.0);
    const int y0 = (int)std::floor(t - 1.0);
    const int x1 = (int)std::ceil(r + 1.0);
    const int y1 = (int)std::ceil(b + 1.0);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double sd = RoundRectSignedDistance(x + 0.5, y + 0.5, l, t, r, b, radius);
            const double cover = Clamp01(0.5 - sd);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void StrokeRoundRectPixels(const PixelCanvas& c, double l, double t, double r, double b,
                           double radius, double stroke, uint32_t rgb, int alpha) {
    const int x0 = (int)std::floor(l - stroke - 1.0);
    const int y0 = (int)std::floor(t - stroke - 1.0);
    const int x1 = (int)std::ceil(r + stroke + 1.0);
    const int y1 = (int)std::ceil(b + stroke + 1.0);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double sd = RoundRectSignedDistance(x + 0.5, y + 0.5, l, t, r, b, radius);
            const double d = std::fabs(sd) - stroke * 0.5;
            const double cover = Clamp01(0.5 - d);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void FillEllipsePixels(const PixelCanvas& c, double cx, double cy, double rx, double ry,
                       uint32_t rgb, int alpha) {
    const int x0 = (int)std::floor(cx - rx - 1.0);
    const int y0 = (int)std::floor(cy - ry - 1.0);
    const int x1 = (int)std::ceil(cx + rx + 1.0);
    const int y1 = (int)std::ceil(cy + ry + 1.0);
    const double aa = rx < ry ? rx : ry;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = (x + 0.5 - cx) / rx;
            const double dy = (y + 0.5 - cy) / ry;
            const double sd = (std::sqrt(dx * dx + dy * dy) - 1.0) * aa;
            const double cover = Clamp01(0.5 - sd);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void StrokeEllipsePixels(const PixelCanvas& c, double cx, double cy, double rx, double ry,
                         double stroke, uint32_t rgb, int alpha) {
    const int x0 = (int)std::floor(cx - rx - stroke - 1.0);
    const int y0 = (int)std::floor(cy - ry - stroke - 1.0);
    const int x1 = (int)std::ceil(cx + rx + stroke + 1.0);
    const int y1 = (int)std::ceil(cy + ry + stroke + 1.0);
    const double aa = rx < ry ? rx : ry;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = (x + 0.5 - cx) / rx;
            const double dy = (y + 0.5 - cy) / ry;
            const double sd = (std::sqrt(dx * dx + dy * dy) - 1.0) * aa;
            const double d = std::fabs(sd) - stroke * 0.5;
            const double cover = Clamp01(0.5 - d);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void FillLinePixels(const PixelCanvas& c, double x1, double y1, double x2, double y2,
                    double width, uint32_t rgb, int alpha) {
    const double minX = x1 < x2 ? x1 : x2;
    const double maxX = x1 > x2 ? x1 : x2;
    const double minY = y1 < y2 ? y1 : y2;
    const double maxY = y1 > y2 ? y1 : y2;
    const int ix0 = (int)std::floor(minX - width - 1.0);
    const int iy0 = (int)std::floor(minY - width - 1.0);
    const int ix1 = (int)std::ceil(maxX + width + 1.0);
    const int iy1 = (int)std::ceil(maxY + width + 1.0);
    const double vx = x2 - x1;
    const double vy = y2 - y1;
    const double len2 = vx * vx + vy * vy;
    if (len2 <= 0.0) return;
    for (int y = iy0; y <= iy1; ++y) {
        for (int x = ix0; x <= ix1; ++x) {
            const double px = x + 0.5 - x1;
            const double py = y + 0.5 - y1;
            double u = (px * vx + py * vy) / len2;
            if (u < 0.0) u = 0.0;
            if (u > 1.0) u = 1.0;
            const double qx = x1 + u * vx;
            const double qy = y1 + u * vy;
            const double dx = x + 0.5 - qx;
            const double dy = y + 0.5 - qy;
            const double d = std::sqrt(dx * dx + dy * dy) - width * 0.5;
            const double cover = Clamp01(0.5 - d);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void FillRotatedRoundRectPixels(const PixelCanvas& c, double cx, double cy, double w, double h,
                                double radius, double radians, uint32_t rgb, int alpha) {
    const double co = std::cos(radians);
    const double si = std::sin(radians);
    const double boundX = std::fabs(co) * w * 0.5 + std::fabs(si) * h * 0.5 + radius + 1.0;
    const double boundY = std::fabs(si) * w * 0.5 + std::fabs(co) * h * 0.5 + radius + 1.0;
    const int x0 = (int)std::floor(cx - boundX);
    const int y0 = (int)std::floor(cy - boundY);
    const int x1 = (int)std::ceil(cx + boundX);
    const int y1 = (int)std::ceil(cy + boundY);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const double lx = co * dx + si * dy;
            const double ly = -si * dx + co * dy;
            const double sd = RoundRectSignedDistance(lx, ly, -w * 0.5, -h * 0.5, w * 0.5, h * 0.5, radius);
            const double cover = Clamp01(0.5 - sd);
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

double EntryX(bool rightDock, double designW, double x) {
    return rightDock ? x : designW - x;
}

void FillRoundRectDesign(const PixelCanvas& c, bool rightDock, double designW, double scale,
                         double l, double t, double r, double b, double radius,
                         uint32_t rgb, int alpha) {
    double x1 = EntryX(rightDock, designW, l) * scale;
    double x2 = EntryX(rightDock, designW, r) * scale;
    if (x1 > x2) {
        const double tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    FillRoundRectPixels(c, x1, t * scale, x2, b * scale, radius * scale, rgb, alpha);
}

void StrokeRoundRectDesign(const PixelCanvas& c, bool rightDock, double designW, double scale,
                           double l, double t, double r, double b, double radius, double stroke,
                           uint32_t rgb, int alpha) {
    double x1 = EntryX(rightDock, designW, l) * scale;
    double x2 = EntryX(rightDock, designW, r) * scale;
    if (x1 > x2) {
        const double tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    StrokeRoundRectPixels(c, x1, t * scale, x2, b * scale, radius * scale, stroke * scale, rgb, alpha);
}

void FillLineDesign(const PixelCanvas& c, bool rightDock, double designW, double scale,
                    double x1, double y1, double x2, double y2, double width,
                    uint32_t rgb, int alpha) {
    FillLinePixels(c, EntryX(rightDock, designW, x1) * scale, y1 * scale,
                   EntryX(rightDock, designW, x2) * scale, y2 * scale,
                   width * scale, rgb, alpha);
}

void FillRotatedRoundRectDesign(const PixelCanvas& c, bool rightDock, double designW, double scale,
                                double cx, double cy, double w, double h, double radius,
                                double degrees, uint32_t rgb, int alpha) {
    const double pi = 3.14159265358979323846;
    const double mirroredDegrees = rightDock ? degrees : -degrees;
    FillRotatedRoundRectPixels(c, EntryX(rightDock, designW, cx) * scale, cy * scale,
                               w * scale, h * scale, radius * scale,
                               mirroredDegrees * pi / 180.0, rgb, alpha);
}

void RotateDesignPoint(double cx, double cy, double localX, double localY, double degrees,
                       double& outX, double& outY) {
    const double pi = 3.14159265358979323846;
    const double rad = degrees * pi / 180.0;
    const double co = std::cos(rad);
    const double si = std::sin(rad);
    outX = cx + co * localX - si * localY;
    outY = cy + si * localX + co * localY;
}

void DrawPetCubeSticker(const PixelCanvas& c, bool rightDock, double designW, double scale,
                        double bodyCx, double bodyCy, double angleDeg,
                        double localX, double localY, double size, uint32_t rgb) {
    double globalCx = bodyCx;
    double globalCy = bodyCy;
    RotateDesignPoint(bodyCx, bodyCy, localX, localY, angleDeg, globalCx, globalCy);
    FillRotatedRoundRectDesign(c, rightDock, designW, scale, globalCx, globalCy,
                               size, size, 3.0, angleDeg, rgb, 245);
}

void DrawCapsulePetEntry(const PixelCanvas& c, bool rightDock, bool hover) {
    constexpr double designW = 76.0;
    constexpr double designH = 128.0;
    const double scale = c.h / designH;
    const double cubeLeft = hover ? 16.0 : 26.0;
    const double cubeAngle = hover ? 5.0 : -8.0;
    const double cubeCx = cubeLeft + 20.0;
    const double cubeCy = 62.0;

    FillRoundRectDesign(c, rightDock, designW, scale, 44.0, 88.0, 76.0, 102.0,
                        12.0, 0x000000, 28);
    FillRoundRectDesign(c, rightDock, designW, scale, 44.0, 88.0, 76.0, 102.0,
                        12.0, 0xFFFFFF, 118);
    StrokeRoundRectDesign(c, rightDock, designW, scale, 44.0, 88.0, 76.0, 102.0,
                          12.0, 1.0, 0x302B25, 34);

    FillRotatedRoundRectDesign(c, rightDock, designW, scale, cubeCx + 1.5, cubeCy + 5.0,
                               40.0, 40.0, 11.0, cubeAngle, 0x000000, 40);
    FillRotatedRoundRectDesign(c, rightDock, designW, scale, cubeCx, cubeCy,
                               40.0, 40.0, 11.0, cubeAngle, 0x26272B, 255);

    constexpr uint32_t colors[9] = {
        0x1197D5, 0xF6C12A, 0x40BD54,
        0xEB513F, 0xFFF7E8, 0x1197D5,
        0xF27C28, 0x40BD54, 0xEB513F,
    };
    const double sticker = 9.5;
    const double gap = 2.0;
    const double start = -20.0 + 3.0 + sticker * 0.5;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            DrawPetCubeSticker(c, rightDock, designW, scale, cubeCx, cubeCy, cubeAngle,
                               start + col * (sticker + gap),
                               start + row * (sticker + gap),
                               sticker, colors[row * 3 + col]);
        }
    }

    double eyeX = cubeCx;
    double eyeY = cubeCy;
    RotateDesignPoint(cubeCx, cubeCy, -8.5, -1.5, cubeAngle, eyeX, eyeY);
    FillRotatedRoundRectDesign(c, rightDock, designW, scale, eyeX, eyeY,
                               5.0, 5.0, 2.5, cubeAngle, 0x1B1D21, 250);
    RotateDesignPoint(cubeCx, cubeCy, 6.5, -1.5, cubeAngle, eyeX, eyeY);
    FillRotatedRoundRectDesign(c, rightDock, designW, scale, eyeX, eyeY,
                               5.0, 5.0, 2.5, cubeAngle, 0x1B1D21, 250);

    const int zAlpha = hover ? 120 : 168;
    FillLineDesign(c, rightDock, designW, scale, 54.0, 25.0, 62.0, 25.0, 2.0, 0x2F77D4, zAlpha);
    FillLineDesign(c, rightDock, designW, scale, 62.0, 25.0, 54.0, 34.0, 2.0, 0x2F77D4, zAlpha);
    FillLineDesign(c, rightDock, designW, scale, 54.0, 34.0, 62.0, 34.0, 2.0, 0x2F77D4, zAlpha);
}

void FillOrbBody(const PixelCanvas& c, double cx, double cy, double r) {
    const int x0 = (int)std::floor(cx - r - 1.0);
    const int y0 = (int)std::floor(cy - r - 1.0);
    const int x1 = (int)std::ceil(cx + r + 1.0);
    const int y1 = (int)std::ceil(cy + r + 1.0);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = y + 0.5 - cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double cover = Clamp01(0.5 - (dist - r));
            if (cover <= 0.0) continue;
            const bool top = (y + 0.5) < cy;
            const uint32_t rgb = top ? 0xFFFFFF : 0x2F77D4;
            const int alpha = top ? 184 : 92;
            BlendPixel(c, x, y, rgb, (int)(alpha * cover + 0.5));
        }
    }
}

void DrawCapsuleOrbEntry(const PixelCanvas& c, bool rightDock, bool hover) {
    constexpr double designW = 70.0;
    constexpr double designH = 120.0;
    const double scale = c.h / designH;
    const double orbLeft = hover ? 4.0 : 30.0;
    const double orbTop = 31.0;
    const double r = 29.0 * scale;
    const double cx = EntryX(rightDock, designW, orbLeft + 29.0) * scale;
    const double cy = (orbTop + 29.0) * scale;

    FillEllipsePixels(c, cx + (rightDock ? 2.0 : -2.0) * scale, cy + 6.0 * scale,
                      r * 0.92, r * 0.80, 0x000000, 38);
    FillOrbBody(c, cx, cy, r);
    StrokeEllipsePixels(c, cx, cy, r - 0.5 * scale, r - 0.5 * scale, 1.0 * scale, 0x302B25, 34);
    FillLinePixels(c, cx - 23.0 * scale, cy - 2.0 * scale, cx + 23.0 * scale, cy - 2.0 * scale,
                   4.0 * scale, 0x272623, 38);
    FillEllipsePixels(c, cx, cy - 7.0 * scale, 8.0 * scale, 8.0 * scale, 0xFFFAF0, 230);
    StrokeEllipsePixels(c, cx, cy - 7.0 * scale, 8.0 * scale, 8.0 * scale,
                        3.0 * scale, 0x272623, 36);

    const double stickerSize = 9.0;
    const double gap = 2.0;
    const double gridLeft = orbLeft + 8.0;
    const double gridTop = orbTop + 8.0;
    const double angle = rightDock ? -16.0 : 16.0;
    constexpr uint32_t colors[9] = {
        0xF6C12A, 0x1197D5, 0x40BD54,
        0xEB513F, 0xFFF7E8, 0x40BD54,
        0xF27C28, 0x1197D5, 0xEB513F,
    };
    const double gridCx = gridLeft + (stickerSize * 3.0 + gap * 2.0) * 0.5;
    const double gridCy = gridTop + (stickerSize * 3.0 + gap * 2.0) * 0.5;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double localX = -(stickerSize + gap) + col * (stickerSize + gap);
            const double localY = -(stickerSize + gap) + row * (stickerSize + gap);
            double sx = gridCx;
            double sy = gridCy;
            RotateDesignPoint(gridCx, gridCy, localX, localY, angle, sx, sy);
            FillRotatedRoundRectDesign(c, rightDock, designW, scale, sx, sy,
                                       stickerSize, stickerSize, 2.0, angle,
                                       colors[row * 3 + col], 232);
        }
    }
}

std::wstring ReadWindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring text((size_t)len + 1, L'\0');
    int got = GetWindowTextW(hwnd, text.data(), len + 1);
    if (got < 0) got = 0;
    text.resize((size_t)got);
    return text;
}

std::wstring NormalizeTodoText(std::wstring text) {
    for (wchar_t& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return Trim(text);
}

std::string TodayDayKey() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  static_cast<int>(st.wYear), static_cast<int>(st.wMonth), static_cast<int>(st.wDay));
    return std::string(buf);
}

int CurrentMinuteOfDay() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return static_cast<int>(st.wHour) * 60 + static_cast<int>(st.wMinute);
}

bool ParseDayKey(const std::string& day, int& year, int& month, int& date) {
    if (!IsValidCalendarDayKey(day)) return false;
    year = (day[0] - '0') * 1000 + (day[1] - '0') * 100 + (day[2] - '0') * 10 + (day[3] - '0');
    month = (day[5] - '0') * 10 + (day[6] - '0');
    date = (day[8] - '0') * 10 + (day[9] - '0');
    return true;
}

std::string OffsetDayKey(const std::string& day, int deltaDays) {
    int year = 0, month = 0, date = 0;
    if (!ParseDayKey(day, year, month, date)) return TodayDayKey();
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = date + deltaDays;
    tm.tm_hour = 12;
    if (std::mktime(&tm) == static_cast<std::time_t>(-1)) return day;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

std::wstring WidenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring CalendarDayLabel(const std::string& day, Lang lang) {
    int year = 0, month = 0, date = 0;
    if (!ParseDayKey(day, year, month, date)) return WidenAscii(day);
    wchar_t buf[32];
    if (lang == Lang::Zh) {
        swprintf_s(buf, L"%04d年%02d月%02d日", year, month, date);
    } else {
        swprintf_s(buf, L"%04d-%02d-%02d", year, month, date);
    }
    return std::wstring(buf);
}

bool IsWrapDelimiter(wchar_t ch) {
    switch (ch) {
    case L'/': case L'\\': case L'.': case L'-': case L'_':
    case L'?': case L'&': case L'=': case L'#': case L':':
        return true;
    default:
        return false;
    }
}

std::wstring MakeBreakableText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 16);
    int run = 0;
    for (wchar_t ch : text) {
        out.push_back(ch);
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            run = 0;
            continue;
        }
        run++;
        if (IsWrapDelimiter(ch) || run >= 24) {
            out.push_back(L'\x200B');
            run = 0;
        }
    }
    return out;
}

std::wstring MakeCalendarBreakableText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size() + text.size() / 8);
    int run = 0;
    for (wchar_t ch : text) {
        out.push_back(ch);
        if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
            run = 0;
            continue;
        }
        run++;
        if (IsWrapDelimiter(ch) || run >= 8) {
            out.push_back(L'\x200B');
            run = 0;
        }
    }
    return out;
}

// 当天时间重叠的块 id（O(n²)，单日块数很小）。
std::vector<int> ConflictingBlockIds(const std::vector<const CalendarBlock*>& blocks) {
    const size_t n = blocks.size();
    std::vector<char> flag(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const CalendarBlock* a = blocks[i];
            const CalendarBlock* b = blocks[j];
            if (a && b && a->startMinute < b->endMinute && b->startMinute < a->endMinute) {
                flag[i] = 1;
                flag[j] = 1;
            }
        }
    }
    std::vector<int> ids;
    for (size_t i = 0; i < n; ++i)
        if (flag[i] && blocks[i]) ids.push_back(blocks[i]->id);
    return ids;
}
} // namespace

// ——————————————————————————— 布局 ———————————————————————————

float MainWindow::ContentTop() const { return S(Theme::kTitleH + Theme::kTabsH); }
float MainWindow::ContentHeight() const { return contentH_; }

float MainWindow::ViewportHeight() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    float h = (float)(rc.bottom - rc.top) - ContentTop() - S(Theme::kFooterH);
    return h < 0 ? 0 : h;
}

void MainWindow::ClampScroll() {
    float maxScroll = contentH_ - ViewportHeight();
    if (maxScroll < 0) maxScroll = 0;
    if (scroll_ < 0) scroll_ = 0;
    if (scroll_ > maxScroll) scroll_ = maxScroll;
}

void MainWindow::ScrollItemIntoView(int itemIndex) {
    for (const RowLayout& r : rows_) {
        if (r.itemIndex != itemIndex) continue;
        float viewH = ViewportHeight();
        if (r.row.top < scroll_) scroll_ = r.row.top;
        else if (r.row.bottom > scroll_ + viewH) scroll_ = r.row.bottom - viewH;
        ClampScroll();
        return;
    }
    ClampScroll();
}

int MainWindow::PreviousVisibleActiveItem(int itemIndex) const {
    int previous = -1;
    for (const RowLayout& r : rows_) {
        if (r.completed) break;
        if (r.itemIndex == itemIndex) return previous;
        previous = r.itemIndex;
    }
    return -1;
}

void MainWindow::RebuildLayout() {
    for (auto& r : rows_) // 释放上一轮缓存的删除线布局，避免泄漏
        if (r.strikeLayout) { r.strikeLayout->Release(); r.strikeLayout = nullptr; }
    rows_.clear();
    listTabs_.clear();
    calendarBlockRects_.clear();
    activeEndY_ = 0.0f;
    emptyActiveRect_ = D2D1::RectF(0, 0, 0, 0);
    addTaskRect_ = D2D1::RectF(0, 0, 0, 0);
    addListRect_ = D2D1::RectF(0, 0, 0, 0);

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left);

    const float pad     = S(Theme::kPadX);
    const float baseRowH = S(Theme::kRowH);

    float docY = 0;
    const int active = model_.ActiveCount();
    const int total  = model_.Count();

    auto makeRow = [&](int itemIndex, bool completed) {
        RowLayout r{};
        r.itemIndex = itemIndex;
        r.completed = completed;
        const TodoItem& item = model_.Items()[itemIndex];
        std::wstring measureText;
        const std::wstring& savedText = item.text;
        const std::wstring* text = &savedText;
        if (editing() && editIndex_ == itemIndex && edit_) {
            measureText = ReadWindowText(edit_);
            text = &measureText;
        }
        const GuiLayout::RowControls baseControls =
            GuiLayout::ComputeRowControls(W, docY, baseRowH, item.level, dpiScale());
        const float rowH = MeasureRowHeight(*text, baseControls.text.Width());
        const GuiLayout::RowControls finalControls =
            GuiLayout::ComputeRowControls(W, docY, rowH, item.level, dpiScale());
        r.row        = ToD2DRect(finalControls.row);
        r.disclosure = ToD2DRect(finalControls.disclosure);
        r.check      = ToD2DRect(finalControls.check);
        r.text       = ToD2DRect(finalControls.text);
        r.del        = ToD2DRect(finalControls.del);
        r.handle     = ToD2DRect(finalControls.handle);
        r.hasChildren = model_.HasChildren(itemIndex);
        r.collapsed = item.collapsed;
        rows_.push_back(r);
        docY += rowH;
    };

    for (int i = 0; i < active; ) {
        makeRow(i, false);
        i = model_.Items()[(size_t)i].collapsed ? model_.SubtreeEnd(i) : i + 1;
    }

    activeEndY_ = docY;
    if (total == 0) {
        // 空列表：占满视口的居中提示（图标 + 文案 + 新建按钮）。
        emptyActiveRect_ = ToD2DRect(GuiLayout::ComputeEmptyActivePrompt(
            W, ViewportHeight(), dpiScale()));
    } else {
        emptyActiveRect_ = D2D1::RectF(0, 0, 0, 0);
    }

    if (total - active > 0) {
        sectionRect_ = D2D1::RectF(pad, docY, W - pad, docY + S(Theme::kSectionH));
        clearRect_   = D2D1::RectF(W - pad - S(44), docY, W - pad, docY + S(Theme::kSectionH));
        docY += S(Theme::kSectionH);
        if (model_.CurrentList().completedExpanded) {
            for (int i = active; i < total; ) {
                makeRow(i, true);
                i = model_.Items()[(size_t)i].collapsed ? model_.SubtreeEnd(i) : i + 1;
            }
        }
    } else {
        sectionRect_ = D2D1::RectF(0, 0, 0, 0);
        clearRect_   = D2D1::RectF(0, 0, 0, 0);
    }

    if (total > 0) {
        // 非空列表底部常驻"新建待办"入口。
        docY += S(6);
        addTaskRect_ = D2D1::RectF(pad, docY, W - pad, docY + S(36));
        docY = addTaskRect_.bottom + S(8);
    } else {
        addTaskRect_ = D2D1::RectF(0, 0, 0, 0);
    }
    contentH_ = docY;

    const GuiLayout::TitleButtons title = GuiLayout::ComputeTitleButtons(W, dpiScale());
    closeRect_    = ToD2DRect(title.close);
    pinRect_      = ToD2DRect(title.pin);
    calendarBtnRect_ = ToD2DRect(title.calendar);
    themeRect_    = ToD2DRect(title.theme);
    menuRect_     = ToD2DRect(title.menu);

    std::vector<GuiLayout::TabMetric> tabMetrics;
    tabMetrics.reserve((size_t)model_.ListCount());
    for (int i = 0; i < model_.ListCount(); ++i) {
        const TodoList* list = model_.ListAt(i);
        if (!list) continue;
        tabMetrics.push_back(GuiLayout::TabMetric{ i, list->title.size(), list->activeCount });
    }
    const GuiLayout::TabStrip tabStrip = GuiLayout::ComputeTabStrip(W, dpiScale(), tabMetrics);
    addListRect_ = ToD2DRect(tabStrip.addList);
    for (const GuiLayout::TabRect& tab : tabStrip.tabs)
        listTabs_.push_back(ListTabLayout{ tab.listIndex, ToD2DRect(tab.rect) });

    const bool showToday = !calendarDay_.empty() && calendarDay_ != TodayDayKey();
    calendarFrame_ = GuiCalendar::ComputeFrame(W, ViewportHeight(), dpiScale(), showToday);
    calendarWeekFrame_ = GuiCalendar::ComputeWeekFrame(W, ViewportHeight(), dpiScale(), showToday);
    calendarMonthFrame_ = GuiCalendar::ComputeMonthFrame(W, ViewportHeight(), dpiScale(), showToday);
    BuildCalendarBlockRects();
    BuildCalendarWeekBlockRects();
    if (calendarActive() && !calendarScrollInitialized_) AlignCalendarScrollToNow(false);
    ClampCalendarScroll();
    if (calendarEditing()) LayoutCalendarEditControls();
}

float MainWindow::MeasureRowHeight(const std::wstring& text, float textWidth) {
    const float base = S(Theme::kRowH);
    if (!dwrite_ || !textFormat_ || text.empty() || textWidth <= S(8)) return base;

    IDWriteTextLayout* layout = nullptr;
    std::wstring breakable = MakeBreakableText(text);
    HRESULT hr = dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(), textFormat_,
                                           textWidth, S(2000), &layout);
    if (FAILED(hr) || !layout) return base;
    DWRITE_TEXT_METRICS tm{};
    float h = base;
    if (SUCCEEDED(layout->GetMetrics(&tm))) {
        float want = (float)std::ceil(tm.height + S(14));
        if (want > h) h = want;
    }
    layout->Release();
    return h;
}

// ——————————————————————————— 命中测试 ———————————————————————————

MainWindow::Hit MainWindow::HitTest(float x, float y) {
    Hit h;

    if (y < ContentTop()) {
        GuiLayout::TitleButtons title;
        title.close = ToGuiRect(closeRect_);
        title.pin = ToGuiRect(pinRect_);
        title.calendar = ToGuiRect(calendarBtnRect_);
        title.theme = ToGuiRect(themeRect_);
        title.menu = ToGuiRect(menuRect_);
        std::vector<GuiLayout::TabRect> tabs;
        tabs.reserve(listTabs_.size());
        for (const ListTabLayout& tab : listTabs_)
            tabs.push_back(GuiLayout::TabRect{ tab.listIndex, GuiLayout::TabKind::List, ToGuiRect(tab.rect) });

        const GuiLayout::ChromeHitResult chrome =
            GuiLayout::HitTestChrome(x, y, dpiScale(), title, ToGuiRect(addListRect_), tabs);
        switch (chrome.kind) {
        case GuiLayout::ChromeHit::Menu:    h.kind = HitKind::Menu;    return h;
        case GuiLayout::ChromeHit::Theme:   h.kind = HitKind::Theme;   return h;
        case GuiLayout::ChromeHit::Pin:     h.kind = HitKind::Pin;     return h;
        case GuiLayout::ChromeHit::Close:   h.kind = HitKind::Close;   return h;
        case GuiLayout::ChromeHit::AddList: h.kind = HitKind::AddList; return h;
        case GuiLayout::ChromeHit::Calendar:
            h.kind = HitKind::Calendar;
            return h;
        case GuiLayout::ChromeHit::ListTab:
            h.kind = HitKind::ListTab;
            h.itemIndex = chrome.listIndex;
            return h;
        case GuiLayout::ChromeHit::None:
            return h; // 标题栏空白交给 NCHITTEST 拖动；标签栏空白无动作
        }
    }

    if (calendarActive()) {
        const float localY = y - ContentTop();

        // The shared header (nav, segmented mode control, today) is laid out the
        // same way in every mode, so hit-test it first off the active frame.
        const GuiCalendar::HeaderLayout& hdr =
            (calendarMode() == CalendarViewMode::Week)  ? calendarWeekFrame_.header
            : (calendarMode() == CalendarViewMode::Month) ? calendarMonthFrame_.header
                                                          : calendarFrame_.header;
        switch (GuiCalendar::HitTestHeader(x, localY, hdr)) {
        case GuiCalendar::HeaderHit::Prev:      h.kind = HitKind::CalendarPrevDay;   return h;
        case GuiCalendar::HeaderHit::Next:      h.kind = HitKind::CalendarNextDay;   return h;
        case GuiCalendar::HeaderHit::Today:     h.kind = HitKind::CalendarToday;     return h;
        case GuiCalendar::HeaderHit::ModeDay:   h.kind = HitKind::CalendarModeDay;   return h;
        case GuiCalendar::HeaderHit::ModeWeek:  h.kind = HitKind::CalendarModeWeek;  return h;
        case GuiCalendar::HeaderHit::ModeMonth: h.kind = HitKind::CalendarModeMonth; return h;
        case GuiCalendar::HeaderHit::None:      break;
        }

        if (calendarMode() == CalendarViewMode::Week) {
            const GuiCalendar::WeekHitResult wh = GuiCalendar::HitTestWeek(
                x, localY, calendarScroll_, dpiScale(), calendarWeekFrame_, calendarWeekBlockRects_);
            switch (wh.kind) {
            case GuiCalendar::WeekHitKind::DayHeader:
                h.kind = HitKind::CalendarWeekDayHeader;
                h.rowIndex = wh.dayIndex;
                return h;
            case GuiCalendar::WeekHitKind::Block:
                h.kind = HitKind::CalendarWeekBlock;
                h.itemIndex = wh.blockId;
                h.rowIndex = wh.dayIndex;
                return h;
            case GuiCalendar::WeekHitKind::EmptyColumn:
            case GuiCalendar::WeekHitKind::None:
                return h;
            }
            return h;
        }

        if (calendarMode() == CalendarViewMode::Month) {
            const GuiCalendar::MonthHitResult mh =
                GuiCalendar::HitTestMonth(x, localY, dpiScale(), calendarMonthFrame_);
            switch (mh.kind) {
            case GuiCalendar::MonthHitKind::Cell:
                h.kind = HitKind::CalendarMonthCell;
                h.itemIndex = mh.cellIndex;
                return h;
            case GuiCalendar::MonthHitKind::None:
                return h;
            }
            return h;
        }

        const GuiCalendar::HitResult calendarHit =
            GuiCalendar::HitTest(x, localY, calendarScroll_, dpiScale(), calendarFrame_, calendarBlockRects_);
        switch (calendarHit.kind) {
        case GuiCalendar::HitKind::EmptyTimeline:
            h.kind = HitKind::CalendarEmptyTimeline;
            return h;
        case GuiCalendar::HitKind::BlockBody:
            h.kind = HitKind::CalendarBlock;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::ResizeStart:
            h.kind = HitKind::CalendarResizeStart;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::ResizeEnd:
            h.kind = HitKind::CalendarResizeEnd;
            h.itemIndex = calendarHit.blockId;
            return h;
        case GuiCalendar::HitKind::None:
            return h;
        }
    }

    float docY = y - ContentTop() + scroll_;
    D2D1_POINT_2F dp{ x, docY };

    if (emptyActiveRect_.bottom > emptyActiveRect_.top && InRect(emptyActiveRect_, dp)) {
        h.kind = HitKind::EmptyActive;
        return h;
    }

    if (addTaskRect_.bottom > addTaskRect_.top && InRect(addTaskRect_, dp)) {
        h.kind = HitKind::AddTask;
        return h;
    }

    if (sectionRect_.bottom > sectionRect_.top &&
        docY >= sectionRect_.top && docY < sectionRect_.bottom) {
        if (InRect(clearRect_, dp)) { h.kind = HitKind::Clear; return h; }
        h.kind = HitKind::Section;
        return h;
    }

    for (size_t i = 0; i < rows_.size(); i++) {
        const RowLayout& r = rows_[i];
        if (docY >= r.row.top && docY < r.row.bottom) {
            h.rowIndex = (int)i;
            h.itemIndex = r.itemIndex;
            GuiLayout::RowControls controls;
            controls.row = ToGuiRect(r.row);
            controls.disclosure = ToGuiRect(r.disclosure);
            controls.check = ToGuiRect(r.check);
            controls.text = ToGuiRect(r.text);
            controls.del = ToGuiRect(r.del);
            controls.handle = ToGuiRect(r.handle);
            switch (GuiLayout::HitTestRowControls(controls, x, docY, r.hasChildren, r.completed)) {
            case GuiLayout::RowHit::TreeToggle: h.kind = HitKind::TreeToggle; return h;
            case GuiLayout::RowHit::Check:      h.kind = HitKind::Check;      return h;
            case GuiLayout::RowHit::Text:       h.kind = HitKind::Text;       return h;
            case GuiLayout::RowHit::Delete:     h.kind = HitKind::Delete;     return h;
            case GuiLayout::RowHit::Handle:     h.kind = HitKind::Handle;     return h;
            case GuiLayout::RowHit::None:       break;
            }
            return h;
        }
    }
    return h;
}

// ——————————————————————————— 渲染 ———————————————————————————

void MainWindow::FillRect(const D2D1_RECT_F& r, uint32_t rgb, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->FillRectangle(r, brush_);
}

void MainWindow::StrokeRect(const D2D1_RECT_F& r, uint32_t rgb, float w, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->DrawRectangle(r, brush_, w);
}

void MainWindow::FillRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->FillRoundedRectangle(rr, brush_);
}

void MainWindow::StrokeRoundRect(const D2D1_ROUNDED_RECT& rr, uint32_t rgb, float w, float a) {
    brush_->SetColor(Theme::D2DColor(rgb, a));
    rt_->DrawRoundedRectangle(rr, brush_, w);
}

void MainWindow::DrawSurfaceFrame(const D2D1_RECT_F& r, float radius, uint32_t fill,
                                  uint32_t edge, float stroke) {
    D2D1_ROUNDED_RECT rr{ r, radius, radius };
    FillRoundRect(rr, fill);
    StrokeRoundRect(rr, edge, stroke);
}

void MainWindow::Text(const std::wstring& s, const D2D1_RECT_F& r, uint32_t rgb,
                      IDWriteTextFormat* fmt) {
    if (s.empty()) return;
    brush_->SetColor(Theme::D2DColor(rgb));
    rt_->DrawTextW(s.c_str(), (UINT32)s.size(), fmt, r, brush_);
}

float MainWindow::MeasureCalendarText(const std::wstring& text, const D2D1_RECT_F& rect) {
    if (!dwrite_ || !calendarTextFormat_ || text.empty()) return 0.0f;
    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= S(6) || h <= S(6)) return 0.0f;

    const std::wstring breakable = MakeCalendarBreakableText(text);
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(),
                                           calendarTextFormat_, w, h, &layout);
    if (FAILED(hr) || !layout) return 0.0f;

    DWRITE_TEXT_METRICS tm{};
    float measured = 0.0f;
    if (SUCCEEDED(layout->GetMetrics(&tm))) {
        measured = std::ceil(tm.height);
        if (measured > h) measured = h;
    }
    layout->Release();
    return measured;
}

float MainWindow::DrawCalendarWrappedText(const std::wstring& text, const D2D1_RECT_F& rect,
                                          uint32_t color) {
    if (!dwrite_ || !calendarTextFormat_ || !rt_ || text.empty()) return 0.0f;
    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    if (w <= S(6) || h <= S(6)) return 0.0f;

    const std::wstring breakable = MakeCalendarBreakableText(text);
    IDWriteTextLayout* layout = nullptr;
    HRESULT hr = dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(),
                                           calendarTextFormat_, w, h, &layout);
    if (FAILED(hr) || !layout) return 0.0f;

    DWRITE_TEXT_METRICS tm{};
    float measured = h;
    if (SUCCEEDED(layout->GetMetrics(&tm))) {
        measured = std::ceil(tm.height);
        if (measured > h) measured = h;
    }

    brush_->SetColor(Theme::D2DColor(color));
    rt_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, brush_);
    rt_->PopAxisAlignedClip();
    layout->Release();
    return measured;
}

void MainWindow::DrawTimelineBlockText(const CalendarBlock& block, const D2D1_RECT_F& blockRect,
                                       bool includeTime) {
    const GuiCalendar::TimelineTextLayout policy =
        GuiCalendar::ComputeTimelineTextLayout(ToGuiRect(blockRect), dpiScale(), includeTime);
    if (!policy.showTitle) return;

    const D2D1_RECT_F titleMax = ToD2DRect(policy.title);
    const float titleMaxH = titleMax.bottom - titleMax.top;
    float titleH = MeasureCalendarText(block.title, titleMax);
    if (titleH <= 0.0f) titleH = std::min(policy.lineHeight, titleMaxH);
    if (titleH > titleMaxH) titleH = titleMaxH;

    const bool showTime = policy.showTime && titleH + policy.gap + policy.time.Height() <= policy.content.Height();
    const float groupH = titleH + (showTime ? policy.gap + policy.time.Height() : 0.0f);
    float y = policy.content.top;
    if (policy.content.Height() > groupH) {
        y += (policy.content.Height() - groupH) * 0.45f;
    }

    D2D1_RECT_F titleR = titleMax;
    titleR.top = y;
    titleR.bottom = std::min(titleR.top + titleH, static_cast<float>(policy.content.bottom));
    DrawCalendarWrappedText(block.title, titleR, CalendarTheme::kBlockTitle);

    if (!showTime) return;
    D2D1_RECT_F timeR = ToD2DRect(policy.time);
    timeR.top = titleR.bottom + policy.gap;
    timeR.bottom = timeR.top + policy.time.Height();
    if (timeR.bottom > policy.content.bottom) return;

    const std::wstring timeText = GuiCalendar::FormatTimeText(block.startMinute) + L" - " +
                                  GuiCalendar::FormatTimeText(block.endMinute);
    Text(timeText, timeR, CalendarTheme::kBlockTime, smallFormat_);
}

void MainWindow::DrawCheckbox(const D2D1_RECT_F& box, bool checked) {
    D2D1_ROUNDED_RECT rr{ box, S(4), S(4) };
    if (checked) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
        rt_->FillRoundedRectangle(rr, brush_);
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkMark));
        float l = box.left, t = box.top, w = box.right - box.left, hh = box.bottom - box.top;
        rt_->DrawLine(D2D1::Point2F(l + w * 0.24f, t + hh * 0.52f),
                      D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f), brush_, S(2));
        rt_->DrawLine(D2D1::Point2F(l + w * 0.42f, t + hh * 0.70f),
                      D2D1::Point2F(l + w * 0.76f, t + hh * 0.30f), brush_, S(2));
    } else {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkBorder));
        rt_->DrawRoundedRectangle(rr, brush_, S(1.5f));
    }
}

void MainWindow::DrawRow(const RowLayout& r, bool hovered) {
    if (hovered) FillRect(r.row, theme_.colors.rowHover); // 最终消费色，不再混 alpha

    int level = ClampTodoLevel(model_.Items()[r.itemIndex].level);
    if (level > 0) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak, 0.45f));
        float x = r.check.left - S(9);
        rt_->DrawLine(D2D1::Point2F(x, r.row.top + S(7)),
                      D2D1::Point2F(x, r.row.bottom - S(7)), brush_, S(1));
    }

    if (r.hasChildren)
        Text(r.collapsed ? L"▸" : L"▾", r.disclosure, theme_.colors.textWeak, smallFormat_);

    DrawCheckbox(r.check, r.completed);

    if (!(editing() && editIndex_ == r.itemIndex)) {
        const std::wstring& s = model_.Items()[r.itemIndex].text;
        std::wstring breakable = MakeBreakableText(s);
        if (r.completed) {
            if (!r.strikeLayout && !s.empty()) { // 仅首帧创建带删除线的布局，之后复用
                IDWriteTextLayout* layout = nullptr;
                if (SUCCEEDED(dwrite_->CreateTextLayout(breakable.c_str(), (UINT32)breakable.size(), textFormat_,
                                                        r.text.right - r.text.left,
                                                        r.text.bottom - r.text.top, &layout))) {
                    DWRITE_TEXT_RANGE rg{ 0, (UINT32)breakable.size() };
                    layout->SetStrikethrough(TRUE, rg);
                    r.strikeLayout = layout;
                }
            }
            if (r.strikeLayout) {
                brush_->SetColor(Theme::D2DColor(theme_.colors.textDone));
                rt_->DrawTextLayout(D2D1::Point2F(r.text.left, r.text.top), r.strikeLayout, brush_);
            }
        } else {
            Text(breakable, r.text, theme_.colors.text, textFormat_);
        }
    }

    if (hovered) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.danger));
        D2D1_RECT_F d = r.del;
        float p = S(5);
        rt_->DrawLine(D2D1::Point2F(d.left + p, d.top + p), D2D1::Point2F(d.right - p, d.bottom - p), brush_, S(1.6f));
        rt_->DrawLine(D2D1::Point2F(d.right - p, d.top + p), D2D1::Point2F(d.left + p, d.bottom - p), brush_, S(1.6f));

        if (!r.completed) {
            brush_->SetColor(Theme::D2DColor(theme_.colors.handle));
            float cx = (r.handle.left + r.handle.right) / 2;
            float hh = r.handle.bottom - r.handle.top;
            for (int i = 0; i < 3; i++) {
                float yy = r.handle.top + hh * (0.34f + 0.16f * i);
                rt_->DrawLine(D2D1::Point2F(cx - S(5), yy), D2D1::Point2F(cx + S(5), yy), brush_, S(1.5f));
            }
        }
    }
}

void MainWindow::DrawSection() {
    if (model_.CompletedCount() == 0) return;

    brush_->SetColor(Theme::D2DColor(theme_.colors.divider));
    rt_->DrawLine(D2D1::Point2F(sectionRect_.left, sectionRect_.top),
                  D2D1::Point2F(sectionRect_.right, sectionRect_.top), brush_, S(1));

    wchar_t buf[96];
    swprintf_s(buf, L"%s (%d) %s", T(Str::Completed, lang_), model_.CompletedCount(),
               model_.CurrentList().completedExpanded ? L"▾" : L"▸");
    D2D1_RECT_F lr = sectionRect_;
    lr.left += S(2);
    Text(buf, lr, theme_.colors.textWeak, smallFormat_);

    Text(T(Str::Clear, lang_), clearRect_, theme_.colors.danger, smallFormat_);
}

void MainWindow::DrawTitleBar() {
    D2D1_RECT_F tr = D2D1::RectF(S(Theme::kPadX), 0, menuRect_.left - S(8), S(Theme::kTitleH));
    Text(L"X-TODO", tr, theme_.colors.textWeak, smallFormat_);

    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    {
        float mcx = (menuRect_.left + menuRect_.right) / 2;
        float mcy = (menuRect_.top + menuRect_.bottom) / 2;
        for (int i = -1; i <= 1; i++) {
            float yy = mcy + i * S(4);
            rt_->DrawLine(D2D1::Point2F(mcx - S(6), yy), D2D1::Point2F(mcx + S(6), yy), brush_, S(1.5f));
        }
    }

    D2D1_POINT_2F tc = D2D1::Point2F((themeRect_.left + themeRect_.right) / 2,
                                     (themeRect_.top + themeRect_.bottom) / 2);
    D2D1_ELLIPSE palette = D2D1::Ellipse(D2D1::Point2F(tc.x - S(0.4f), tc.y), S(6.4f), S(5.4f));
    brush_->SetColor(Theme::D2DColor(theme_.colors.paperElevated));
    rt_->FillEllipse(palette, brush_);
    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    rt_->DrawEllipse(palette, brush_, S(1.2f));

    auto swatch = [&](float dx, float dy, float radius, uint32_t color) {
        brush_->SetColor(Theme::D2DColor(color));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tc.x + S(dx), tc.y + S(dy)),
                                       S(radius), S(radius)), brush_);
    };
    swatch(-3.1f, -1.7f, 1.15f, theme_.colors.checkFill);
    swatch(-0.6f, -2.5f, 1.05f, theme_.colors.danger);
    swatch(-2.2f,  1.3f, 1.05f, theme_.colors.handleHover);

    D2D1_ELLIPSE hole = D2D1::Ellipse(D2D1::Point2F(tc.x + S(3.0f), tc.y + S(1.4f)), S(1.55f), S(1.35f));
    brush_->SetColor(Theme::D2DColor(theme_.colors.paper));
    rt_->FillEllipse(hole, brush_);
    brush_->SetColor(Theme::D2DColor(theme_.colors.paperEdge));
    rt_->DrawEllipse(hole, brush_, S(0.9f));

    // 日历视图开关图标（激活态高亮）
    {
        const bool on = calendarActive();
        if (on) {
            DrawSurfaceFrame(calendarBtnRect_, S(7),
                             Theme::Blend(theme_.colors.checkFill, theme_.colors.paper, 0.14f),
                             Theme::Blend(theme_.colors.checkFill, theme_.colors.paperEdge, 0.5f), S(1));
        }
        brush_->SetColor(Theme::D2DColor(on ? theme_.colors.checkFill : theme_.colors.textWeak));
        const float ccx = (calendarBtnRect_.left + calendarBtnRect_.right) / 2.0f;
        const float ccy = (calendarBtnRect_.top + calendarBtnRect_.bottom) / 2.0f;
        D2D1_RECT_F body = D2D1::RectF(ccx - S(6.5f), ccy - S(4.5f), ccx + S(6.5f), ccy + S(6.0f));
        rt_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ body, S(2), S(2) }, brush_, S(1.4f));
        rt_->DrawLine(D2D1::Point2F(body.left, body.top + S(3.5f)),
                      D2D1::Point2F(body.right, body.top + S(3.5f)), brush_, S(1.2f));
        rt_->DrawLine(D2D1::Point2F(ccx - S(3.0f), body.top - S(2.0f)),
                      D2D1::Point2F(ccx - S(3.0f), body.top + S(1.0f)), brush_, S(1.4f));
        rt_->DrawLine(D2D1::Point2F(ccx + S(3.0f), body.top - S(2.0f)),
                      D2D1::Point2F(ccx + S(3.0f), body.top + S(1.0f)), brush_, S(1.4f));
    }

    D2D1_POINT_2F pc = D2D1::Point2F((pinRect_.left + pinRect_.right) / 2,
                                     (pinRect_.top + pinRect_.bottom) / 2);
    D2D1_ELLIPSE pe = D2D1::Ellipse(pc, S(5), S(5));
    if (ui_.alwaysOnTop) {
        brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
        rt_->FillEllipse(pe, brush_);
    } else {
        brush_->SetColor(Theme::D2DColor(theme_.colors.handle));
        rt_->DrawEllipse(pe, brush_, S(1.5f));
    }

    brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
    D2D1_RECT_F c = closeRect_;
    float p = S(7);
    rt_->DrawLine(D2D1::Point2F(c.left + p, c.top + p), D2D1::Point2F(c.right - p, c.bottom - p), brush_, S(1.6f));
    rt_->DrawLine(D2D1::Point2F(c.right - p, c.top + p), D2D1::Point2F(c.left + p, c.bottom - p), brush_, S(1.6f));
}

void MainWindow::DrawListTabs() {
    const float tabTop = S(Theme::kTitleH);
    const float tabBottom = ContentTop();
    FillRect(D2D1::RectF(0, tabTop, addListRect_.right + S(Theme::kPadX), tabBottom),
             theme_.colors.paper);

    const int current = model_.CurrentListIndex();
    for (const ListTabLayout& tab : listTabs_) {
        const TodoList* list = model_.ListAt(tab.listIndex);
        if (!list) continue;

        const bool selected = !calendarActive() && tab.listIndex == current;
        if (selected) {
            DrawSurfaceFrame(tab.rect, S(8), theme_.colors.paperElevated,
                             theme_.colors.paperEdge, S(1));
        }

        D2D1_RECT_F textR = tab.rect;
        textR.left += S(10);
        textR.right -= S(9);
        if (list->activeCount > 0) textR.right -= S(25);
        Text(list->title, textR, selected ? theme_.colors.text : theme_.colors.textWeak, smallFormat_);

        if (list->activeCount > 0) {
            wchar_t countBuf[16];
            swprintf_s(countBuf, L"%d", list->activeCount);
            const float pillW = S(15.0f + 6.0f * (float)wcslen(countBuf));
            const float pillH = S(16);
            D2D1_RECT_F pill = D2D1::RectF(tab.rect.right - S(8) - pillW,
                                           tab.rect.top + (tab.rect.bottom - tab.rect.top - pillH) / 2.0f,
                                           tab.rect.right - S(8),
                                           tab.rect.top + (tab.rect.bottom - tab.rect.top + pillH) / 2.0f);
            D2D1_ROUNDED_RECT prr{ pill, pillH / 2.0f, pillH / 2.0f };
            const uint32_t pillFill = selected
                ? Theme::Blend(theme_.colors.checkFill, theme_.colors.paperElevated, 0.16f)
                : Theme::Blend(theme_.colors.checkFill, theme_.colors.paper, 0.10f);
            brush_->SetColor(Theme::D2DColor(pillFill));
            rt_->FillRoundedRectangle(prr, brush_);
            brush_->SetColor(Theme::D2DColor(theme_.colors.checkFill));
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            rt_->DrawTextW(countBuf, (UINT32)wcslen(countBuf), smallFormat_, pill, brush_,
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    if (addListRect_.right > addListRect_.left) {
        DrawSurfaceFrame(addListRect_, S(8), theme_.colors.paperElevated,
                         theme_.colors.paperEdge, S(1));

        const float cx = (addListRect_.left + addListRect_.right) / 2.0f;
        const float cy = (addListRect_.top + addListRect_.bottom) / 2.0f;
        const float half = S(5);
        brush_->SetColor(Theme::D2DColor(theme_.colors.textWeak));
        rt_->DrawLine(D2D1::Point2F(cx - half, cy), D2D1::Point2F(cx + half, cy), brush_, S(1.5f));
        rt_->DrawLine(D2D1::Point2F(cx, cy - half), D2D1::Point2F(cx, cy + half), brush_, S(1.5f));
    }
}

void MainWindow::DrawEmptyActivePrompt(bool hovered) {
    if (emptyActiveRect_.bottom <= emptyActiveRect_.top) return;

    const float cx = (emptyActiveRect_.left + emptyActiveRect_.right) / 2.0f;
    const float cy = (emptyActiveRect_.top + emptyActiveRect_.bottom) / 2.0f;

    // 图标卡片（清单方块）
    D2D1_RECT_F badge = D2D1::RectF(cx - S(26), cy - S(86), cx + S(26), cy - S(34));
    DrawSurfaceFrame(badge, S(14), theme_.colors.paper, theme_.colors.paperEdge, S(1));
    brush_->SetColor(Theme::D2DColor(theme_.colors.disabledText));
    const float ix = (badge.left + badge.right) / 2.0f;
    const float iy = (badge.top + badge.bottom) / 2.0f;
    D2D1_RECT_F sheet = D2D1::RectF(ix - S(9), iy - S(11), ix + S(9), iy + S(11));
    rt_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{ sheet, S(2.5f), S(2.5f) }, brush_, S(1.5f));
    for (int i = 0; i < 2; ++i) {
        const float ly = iy - S(2) + i * S(6);
        rt_->DrawLine(D2D1::Point2F(sheet.left + S(4), ly), D2D1::Point2F(sheet.right - S(3), ly), brush_, S(1.4f));
    }

    // 标题
    D2D1_RECT_F titleR = D2D1::RectF(emptyActiveRect_.left, cy - S(28), emptyActiveRect_.right, cy - S(6));
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(T(Str::EmptyListTitle, lang_), titleR, theme_.colors.text, textFormat_);
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    // 副说明
    D2D1_RECT_F subR = D2D1::RectF(emptyActiveRect_.left, cy - S(4), emptyActiveRect_.right, cy + S(16));
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(T(Str::EmptyActivePrompt, lang_), subR, theme_.colors.textWeak, smallFormat_);

    // 新建按钮
    const float bw = S(132), bh = S(34);
    D2D1_RECT_F cta = D2D1::RectF(cx - bw / 2.0f, cy + S(28), cx + bw / 2.0f, cy + S(28) + bh);
    DrawSurfaceFrame(cta, S(8), hovered ? theme_.colors.checkFillHover : theme_.colors.checkFill,
                     theme_.colors.checkFill, S(1));
    Text(T(Str::NewTask, lang_), cta, theme_.colors.checkMark, smallFormat_);
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

void MainWindow::DrawAddTaskRow(bool hovered) {
    if (addTaskRect_.bottom <= addTaskRect_.top) return;

    const uint32_t edge = hovered ? theme_.colors.checkFill : theme_.colors.paperEdge;
    const uint32_t fg   = hovered ? theme_.colors.checkFill : theme_.colors.textWeak;
    D2D1_ROUNDED_RECT rr{ addTaskRect_, S(8), S(8) };
    if (hovered)
        FillRoundRect(rr, Theme::Blend(theme_.colors.checkFill, theme_.colors.paper, 0.10f));
    StrokeRoundRect(rr, edge, S(1.2f));

    // 居中的 "+ 新建待办"
    const float cy = (addTaskRect_.top + addTaskRect_.bottom) / 2.0f;
    const float plusX = (addTaskRect_.left + addTaskRect_.right) / 2.0f - S(36);
    brush_->SetColor(Theme::D2DColor(fg));
    rt_->DrawLine(D2D1::Point2F(plusX - S(5), cy), D2D1::Point2F(plusX + S(5), cy), brush_, S(1.6f));
    rt_->DrawLine(D2D1::Point2F(plusX, cy - S(5)), D2D1::Point2F(plusX, cy + S(5)), brush_, S(1.6f));
    D2D1_RECT_F textR = addTaskRect_;
    textR.left = plusX + S(11);
    Text(T(Str::NewTask, lang_), textR, fg, smallFormat_);
}

void MainWindow::DrawCalendarDay(float W, float H) {
    const float top = ContentTop();
    const GuiCalendar::Frame& frame = calendarFrame_;

    // 皮肤色跟随主题；事件块色固定（CalendarTheme）。
    const uint32_t soft = Theme::Blend(theme_.colors.paperEdge, theme_.colors.paper, 0.45f);
    const uint32_t hourLine = theme_.colors.divider;
    const uint32_t halfLine = Theme::Blend(theme_.colors.divider, theme_.colors.paper, 0.5f);

    FillRect(D2D1::RectF(0, top, W, H), theme_.colors.paper);

    // Header chrome (nav, mode control, today) is drawn by DrawCalendarHeader.
    const auto dayBlocks = calendar_.BlocksForDay(calendarDay_);
    const std::vector<int> conflictIds = ConflictingBlockIds(dayBlocks);

    // —— 时间轴 ——
    const float tlTop = top + frame.timelineViewport.top;
    const float tlBottom = top + frame.timelineViewport.bottom;
    FillRect(D2D1::RectF(0, tlTop, frame.gutter.right, tlBottom), soft);

    const D2D1_RECT_F clip = D2D1::RectF(0, tlTop, W, tlBottom);
    rt_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, tlTop - calendarScroll_));

    for (int half = 0; half <= 48; ++half) {
        const float y = (static_cast<float>(half) / 48.0f) * frame.contentHeight;
        const bool onHour = (half % 2 == 0);
        brush_->SetColor(Theme::D2DColor(onHour ? hourLine : halfLine));
        rt_->DrawLine(D2D1::Point2F(onHour ? 0.0f : frame.lane.left, y),
                      D2D1::Point2F(frame.lane.right, y), brush_, S(onHour ? 1.0f : 0.75f));
        if (onHour && half / 2 < 24) {
            wchar_t buf[8];
            swprintf_s(buf, L"%02d:00", half / 2);
            // 0 点标签放到刻度线下方，避免被时间轴顶边裁掉；其余整点仍跨线居中。
            const float labTop = (half == 0) ? y + S(2) : y - S(8);
            D2D1_RECT_F label = D2D1::RectF(S(7), labTop, frame.gutter.right - S(7), labTop + S(20));
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(buf, label, theme_.colors.textWeak, smallFormat_);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    int idx = 0;
    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        const CalendarBlock* block = calendar_.FindBlock(blockRect.blockId);
        if (!block) { ++idx; continue; }
        const bool selected = block->id == calendarEditId_;
        bool conflict = false;
        for (int c : conflictIds) if (c == block->id) { conflict = true; break; }
        const CalendarTheme::BlockColor& bc =
            conflict ? CalendarTheme::kConflict : CalendarTheme::BlockColorAt(idx);

        const GuiCalendar::EditLayout editLayout =
            GuiCalendar::ComputeEditLayout(blockRect.rect, dpiScale());
        D2D1_RECT_F r = ToD2DRect(selected ? editLayout.block : blockRect.rect);
        const float radius = S(7);
        FillRoundRect(D2D1_ROUNDED_RECT{ r, radius, radius }, bc.fill);
        StrokeRoundRect(D2D1_ROUNDED_RECT{ r, radius, radius },
                        bc.edge, S(selected ? 1.6f : 1.0f));

        if (selected) {
            auto focusField = [&]() {
                switch (calendarEditFocus_) {
                case CalendarEditFocus::StartTime: return GuiCalendar::EditField::StartTime;
                case CalendarEditFocus::EndTime:   return GuiCalendar::EditField::EndTime;
                case CalendarEditFocus::Title:
                    break;
                }
                return GuiCalendar::EditField::Title;
            };
            auto drawEditFrame = [&](GuiCalendar::EditField field, const Gui::Rect& rect) {
                const bool active = field == focusField();
                DrawSurfaceFrame(ToD2DRect(rect), S(5), bc.fill, bc.edge,
                                 S(active ? 1.5f : 1.0f));
            };
            drawEditFrame(GuiCalendar::EditField::Title, editLayout.titleFrame);
            drawEditFrame(GuiCalendar::EditField::StartTime, editLayout.startFrame);
            drawEditFrame(GuiCalendar::EditField::EndTime, editLayout.endFrame);
        } else {
            DrawTimelineBlockText(*block, r, true);
        }
        ++idx;
    }

    // 当前时间线浮于事件块之上但半透明，跨在块上时不喧宾夺主；端点圆点保持实心作锚点。
    if (calendarDay_ == TodayDayKey()) {
        const float y = (static_cast<float>(CurrentMinuteOfDay()) / 1440.0f) * frame.contentHeight;
        brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing, 0.5f));
        rt_->DrawLine(D2D1::Point2F(frame.lane.left, y),
                      D2D1::Point2F(frame.lane.right, y), brush_, S(1.2f));
        brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(frame.lane.left, y), S(3), S(3)), brush_);
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();
}

void MainWindow::DrawCalendarView(float W, float H) {
    switch (calendarMode()) {
    case CalendarViewMode::Week:  DrawCalendarWeek(W, H);  break;
    case CalendarViewMode::Month: DrawCalendarMonth(W, H); break;
    case CalendarViewMode::Day:
    default:                      DrawCalendarDay(W, H);   break;
    }
    const GuiCalendar::HeaderLayout& hdr =
        (calendarMode() == CalendarViewMode::Week)  ? calendarWeekFrame_.header
        : (calendarMode() == CalendarViewMode::Month) ? calendarMonthFrame_.header
                                                      : calendarFrame_.header;
    DrawCalendarHeader(hdr, CalendarHeaderTitle(hdr.compactTitle));
}

std::wstring MainWindow::CalendarHeaderTitle(bool compact) const {
    CalendarDate::Date d;
    if (!CalendarDate::Parse(calendarDay_, d)) return CalendarDayLabel(calendarDay_, lang_);
    wchar_t buf[48];
    switch (calendarMode()) {
    case CalendarViewMode::Week: {
        const CalendarDate::Date ws = CalendarDate::StartOfWeek(d);
        if (compact) { swprintf_s(buf, L"%d/%d", ws.month, ws.day); return buf; }
        const CalendarDate::Date we = CalendarDate::AddDays(ws, 6);
        if (lang_ == Lang::Zh) swprintf_s(buf, L"%d月%d日 - %d月%d日", ws.month, ws.day, we.month, we.day);
        else                   swprintf_s(buf, L"%d/%d - %d/%d", ws.month, ws.day, we.month, we.day);
        return buf;
    }
    case CalendarViewMode::Month:
        if (lang_ == Lang::Zh) swprintf_s(buf, L"%04d年%02d月", d.year, d.month);
        else                   swprintf_s(buf, L"%04d-%02d", d.year, d.month);
        return buf;
    case CalendarViewMode::Day:
    default:
        if (compact) { swprintf_s(buf, L"%d/%d", d.month, d.day); return buf; }
        return CalendarDayLabel(calendarDay_, lang_);
    }
}

void MainWindow::DrawCalendarHeader(const GuiCalendar::HeaderLayout& header, const std::wstring& title) {
    const float top = ContentTop();
    auto off = [&](const Gui::Rect& r) {
        D2D1_RECT_F d = ToD2DRect(r);
        d.top += top;
        d.bottom += top;
        return d;
    };

    // Grouped nav chevrons, drawn as vector strokes so they read clearly at any DPI.
    auto chevron = [&](const Gui::Rect& r, bool pointLeft) {
        const D2D1_RECT_F d = off(r);
        const float cx = (d.left + d.right) * 0.5f;
        const float cy = (d.top + d.bottom) * 0.5f;
        const float a = S(5.5f);
        const float tipX = pointLeft ? cx - a * 0.55f : cx + a * 0.55f;
        const float armX = pointLeft ? cx + a * 0.55f : cx - a * 0.55f;
        brush_->SetColor(Theme::D2DColor(theme_.colors.text));
        rt_->DrawLine(D2D1::Point2F(armX, cy - a), D2D1::Point2F(tipX, cy), brush_, S(2.0f));
        rt_->DrawLine(D2D1::Point2F(tipX, cy), D2D1::Point2F(armX, cy + a), brush_, S(2.0f));
    };
    chevron(header.prev, true);
    chevron(header.next, false);

    if (header.titleVisible && !title.empty())
        Text(title, off(header.title), theme_.colors.text, textFormat_);

    // Connected segmented control: one rounded container, selected segment filled.
    DrawSurfaceFrame(off(header.segment), S(7), theme_.colors.paperElevated,
                     theme_.colors.paperEdge, S(1));
    auto drawSeg = [&](const Gui::Rect& r, Str label, bool active) {
        const D2D1_RECT_F d = off(r);
        if (active) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(d.left + S(2), d.top + S(2), d.right - S(2), d.bottom - S(2)),
                                  S(5), S(5) };
            FillRoundRect(rr, theme_.colors.focusRing);
        }
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(T(label, lang_), d, active ? theme_.colors.paper : theme_.colors.text, smallFormat_);
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    };
    drawSeg(header.modeDay,   Str::CalendarModeDay,   calendarMode() == CalendarViewMode::Day);
    drawSeg(header.modeWeek,  Str::CalendarModeWeek,  calendarMode() == CalendarViewMode::Week);
    drawSeg(header.modeMonth, Str::CalendarModeMonth, calendarMode() == CalendarViewMode::Month);

    // Back-to-today target glyph, shown only when the anchor is not today.
    if (header.todayVisible) {
        const D2D1_RECT_F td = off(header.today);
        DrawSurfaceFrame(td, S(7), theme_.colors.paperElevated, theme_.colors.paperEdge, S(1));
        const float cx = (td.left + td.right) * 0.5f;
        const float cy = (td.top + td.bottom) * 0.5f;
        brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
        rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), S(5), S(5)), brush_, S(1.4f));
        rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), S(1.6f), S(1.6f)), brush_);
    }
}

void MainWindow::DrawCalendarWeek(float W, float H) {
    const float top = ContentTop();
    const GuiCalendar::WeekFrame& frame = calendarWeekFrame_;

    const uint32_t soft = Theme::Blend(theme_.colors.paperEdge, theme_.colors.paper, 0.45f);
    const uint32_t hourLine = theme_.colors.divider;
    const uint32_t halfLine = Theme::Blend(theme_.colors.divider, theme_.colors.paper, 0.5f);
    const uint32_t colLine = Theme::Blend(theme_.colors.divider, theme_.colors.paper, 0.35f);

    FillRect(D2D1::RectF(0, top, W, H), theme_.colors.paper);

    auto offRect = [&](const Gui::Rect& r) {
        D2D1_RECT_F d = ToD2DRect(r);
        d.top += top;
        d.bottom += top;
        return d;
    };

    CalendarDate::Date anchor;
    CalendarDate::Date weekStart{};
    const bool haveWeek = CalendarDate::Parse(calendarDay_, anchor);
    if (haveWeek) weekStart = CalendarDate::StartOfWeek(anchor);
    const std::string todayKey = TodayDayKey();

    // Header chrome (nav, mode control, today) is drawn by DrawCalendarHeader.
    static const wchar_t* kWeekdayZh[7] = { L"一", L"二", L"三", L"四", L"五", L"六", L"日" };
    static const wchar_t* kWeekdayEn[7] = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    for (int i = 0; i < 7; ++i) {
        D2D1_RECT_F hd = offRect(frame.dayHeaders[(size_t)i]);
        std::string key;
        int dom = 0;
        if (haveWeek) {
            const CalendarDate::Date cell = CalendarDate::AddDays(weekStart, i);
            key = CalendarDate::Format(cell);
            dom = cell.day;
        }
        const bool isToday = haveWeek && key == todayKey;
        const bool isSel = haveWeek && key == calendarDay_;
        if (isSel) {
            FillRoundRect(D2D1_ROUNDED_RECT{ hd, S(6), S(6) },
                          Theme::Blend(theme_.colors.focusRing, theme_.colors.paper, 0.18f));
        }
        wchar_t buf[24];
        swprintf_s(buf, L"%s %d", (lang_ == Lang::Zh ? kWeekdayZh[i] : kWeekdayEn[i]), dom);
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(buf, hd, isToday ? theme_.colors.focusRing : theme_.colors.text, smallFormat_);
        smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    const float tlTop = top + frame.timelineViewport.top;
    const float tlBottom = top + frame.timelineViewport.bottom;
    FillRect(D2D1::RectF(0, tlTop, frame.gutter.right, tlBottom), soft);

    const D2D1_RECT_F clip = D2D1::RectF(0, tlTop, W, tlBottom);
    rt_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, tlTop - calendarScroll_));

    const float laneLeft = frame.columns[0].left;
    const float laneRight = frame.columns[6].right;
    for (int half = 0; half <= 48; ++half) {
        const float y = (static_cast<float>(half) / 48.0f) * frame.contentHeight;
        const bool onHour = (half % 2 == 0);
        brush_->SetColor(Theme::D2DColor(onHour ? hourLine : halfLine));
        rt_->DrawLine(D2D1::Point2F(onHour ? 0.0f : laneLeft, y),
                      D2D1::Point2F(laneRight, y), brush_, S(onHour ? 1.0f : 0.75f));
        if (onHour && half / 2 < 24) {
            wchar_t buf[8];
            swprintf_s(buf, L"%02d:00", half / 2);
            const float labTop = (half == 0) ? y + S(2) : y - S(8);
            D2D1_RECT_F label = D2D1::RectF(S(7), labTop, frame.gutter.right - S(7), labTop + S(20));
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(buf, label, theme_.colors.textWeak, smallFormat_);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }

    brush_->SetColor(Theme::D2DColor(colLine));
    for (int i = 0; i <= 7; ++i) {
        const float x = (i < 7) ? frame.columns[(size_t)i].left : laneRight;
        rt_->DrawLine(D2D1::Point2F(x, 0.0f), D2D1::Point2F(x, frame.contentHeight), brush_, S(0.75f));
    }

    int idx = 0;
    for (const GuiCalendar::WeekBlockRect& wb : calendarWeekBlockRects_) {
        const CalendarBlock* block = calendar_.FindBlock(wb.blockId);
        if (!block) { ++idx; continue; }
        const CalendarTheme::BlockColor& bc = CalendarTheme::BlockColorAt(idx);
        D2D1_RECT_F r = ToD2DRect(wb.rect);
        const float radius = S(5);
        FillRoundRect(D2D1_ROUNDED_RECT{ r, radius, radius }, bc.fill);
        StrokeRoundRect(D2D1_ROUNDED_RECT{ r, radius, radius }, bc.edge, S(1.0f));
        DrawTimelineBlockText(*block, r, true);
        ++idx;
    }

    // 当前时间线浮于事件块之上但半透明，跨在块上时不喧宾夺主。
    if (haveWeek) {
        for (int i = 0; i < 7; ++i) {
            if (CalendarDate::Format(CalendarDate::AddDays(weekStart, i)) != todayKey) continue;
            const float y = (static_cast<float>(CurrentMinuteOfDay()) / 1440.0f) * frame.contentHeight;
            brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing, 0.5f));
            rt_->DrawLine(D2D1::Point2F(frame.columns[(size_t)i].left, y),
                          D2D1::Point2F(frame.columns[(size_t)i].right, y), brush_, S(1.2f));
            break;
        }
    }

    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->PopAxisAlignedClip();
}

void MainWindow::DrawCalendarMonth(float W, float H) {
    const float top = ContentTop();
    const GuiCalendar::MonthFrame& frame = calendarMonthFrame_;
    const uint32_t cellLine = Theme::Blend(theme_.colors.divider, theme_.colors.paper, 0.5f);

    FillRect(D2D1::RectF(0, top, W, H), theme_.colors.paper);

    auto offRect = [&](const Gui::Rect& r) {
        D2D1_RECT_F d = ToD2DRect(r);
        d.top += top;
        d.bottom += top;
        return d;
    };

    CalendarDate::Date anchor;
    CalendarDate::Date gridStart{};
    const bool haveMonth = CalendarDate::Parse(calendarDay_, anchor);
    if (haveMonth) gridStart = CalendarDate::MonthGridStart(anchor.year, anchor.month);
    const std::string todayKey = TodayDayKey();

    // Header chrome (nav, mode control, today) is drawn by DrawCalendarHeader.
    static const wchar_t* kWeekdayZh[7] = { L"一", L"二", L"三", L"四", L"五", L"六", L"日" };
    static const wchar_t* kWeekdayEn[7] = { L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun" };
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    for (int i = 0; i < 7; ++i) {
        Text(lang_ == Lang::Zh ? kWeekdayZh[i] : kWeekdayEn[i],
             offRect(frame.weekdayHeaders[(size_t)i]), theme_.colors.textWeak, smallFormat_);
    }
    smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    for (int i = 0; i < 42; ++i) {
        D2D1_RECT_F cell = offRect(frame.cells[(size_t)i]);
        std::string key;
        int dom = 0;
        bool inMonth = false;
        if (haveMonth) {
            const CalendarDate::Date cd = CalendarDate::AddDays(gridStart, i);
            key = CalendarDate::Format(cd);
            dom = cd.day;
            inMonth = (cd.month == anchor.month);
        }
        const bool isToday = haveMonth && key == todayKey;
        const bool isSel = haveMonth && key == calendarDay_;

        if (isSel) {
            FillRoundRect(D2D1_ROUNDED_RECT{ cell, S(5), S(5) },
                          Theme::Blend(theme_.colors.focusRing, theme_.colors.paper, 0.16f));
        }
        StrokeRect(cell, cellLine, S(0.75f));

        wchar_t num[8];
        swprintf_s(num, L"%d", dom);
        D2D1_RECT_F numR = cell;
        numR.left += S(5);
        numR.top += S(3);
        numR.right -= S(4);
        numR.bottom = numR.top + S(16);
        uint32_t numColor = inMonth ? theme_.colors.text : theme_.colors.textWeak;
        if (isToday) numColor = theme_.colors.focusRing;
        Text(num, numR, numColor, smallFormat_);

        if (!haveMonth) continue;
        const std::vector<const CalendarBlock*> blocks = calendar_.BlocksForDay(key);
        if (blocks.empty()) continue;

        const float lineH = S(14);
        const float firstTop = cell.top + S(20);
        const float avail = cell.bottom - firstTop - S(2);
        int maxLines = (avail > 0.0f) ? static_cast<int>(avail / lineH) : 0;
        const uint32_t titleColor = inMonth ? theme_.colors.text : theme_.colors.textWeak;
        if (maxLines >= 1) {
            int usedLines = 0;
            int shown = 0;
            for (size_t bi = 0; bi < blocks.size(); ++bi) {
                const int remainingAfter = static_cast<int>(blocks.size() - bi - 1);
                const bool reserveMore = remainingAfter > 0;
                const int availableForTitle = maxLines - usedLines - (reserveMore ? 1 : 0);
                if (availableForTitle <= 0) break;

                const int maxTitleLines = (blocks.size() == 1)
                    ? std::min(availableForTitle, 4)
                    : std::min(availableForTitle, 2);
                D2D1_RECT_F tr = cell;
                tr.left += S(4);
                tr.right -= S(4);
                tr.top = firstTop + static_cast<float>(usedLines) * lineH;
                tr.bottom = tr.top + static_cast<float>(maxTitleLines) * lineH;

                float measured = MeasureCalendarText(blocks[bi]->title, tr);
                int used = static_cast<int>(std::ceil(measured / lineH));
                if (used < 1) used = 1;
                if (used > maxTitleLines) used = maxTitleLines;
                tr.bottom = tr.top + static_cast<float>(used) * lineH;
                DrawCalendarWrappedText(blocks[bi]->title, tr, titleColor);
                usedLines += used;
                shown++;
            }
            const int remaining = static_cast<int>(blocks.size()) - shown;
            if (remaining > 0) {
                wchar_t more[16];
                swprintf_s(more, L"+%d", remaining);
                D2D1_RECT_F tr = cell;
                tr.left += S(4);
                tr.right -= S(4);
                tr.top = firstTop + static_cast<float>(usedLines) * lineH;
                tr.bottom = tr.top + lineH;
                Text(more, tr, theme_.colors.textWeak, smallFormat_);
            }
        } else {
            wchar_t cnt[16];
            swprintf_s(cnt, L"%d", static_cast<int>(blocks.size()));
            D2D1_RECT_F cr = cell;
            cr.left += S(4);
            cr.right -= S(4);
            cr.bottom -= S(2);
            cr.top = cr.bottom - lineH;
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            Text(cnt, cr, theme_.colors.textWeak, smallFormat_);
            smallFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }
}

bool MainWindow::Render() {
    if (capsuleShrunk())
        return RenderCapsuleEntryLayered();

    if (!CreateDeviceResources()) return false;

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float W = (float)(rc.right - rc.left), H = (float)(rc.bottom - rc.top);

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(Theme::D2DColor(theme_.colors.paper));

    if (calendarActive()) {
        DrawCalendarView(W, H);
    } else {
        D2D1_RECT_F vp = D2D1::RectF(0, ContentTop(), W, H - S(Theme::kFooterH));
        rt_->PushAxisAlignedClip(vp, D2D1_ANTIALIAS_MODE_ALIASED);
        rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, ContentTop() - scroll_));

        DrawEmptyActivePrompt(hoverRow_ == kHoverEmptyActive);
        for (size_t i = 0; i < rows_.size(); i++)
            DrawRow(rows_[i], (int)i == hoverRow_);
        DrawSection();
        DrawAddTaskRow(hoverRow_ == kHoverAddTask);

        if (dragging_ && dragInsert_ >= 0) {
            float yy = activeEndY_;
            for (const RowLayout& r : rows_) {
                if (!r.completed && r.itemIndex == dragInsert_) { yy = r.row.top; break; }
            }
            brush_->SetColor(Theme::D2DColor(theme_.colors.focusRing));
            rt_->DrawLine(D2D1::Point2F(S(Theme::kPadX), yy),
                          D2D1::Point2F(W - S(Theme::kPadX), yy), brush_, S(2));
        }

        rt_->SetTransform(D2D1::Matrix3x2F::Identity());
        rt_->PopAxisAlignedClip();
    }

    // 固定层
    DrawTitleBar();
    DrawListTabs();
    StrokeRect(D2D1::RectF(0.5f, 0.5f, W - 0.5f, H - 0.5f), theme_.colors.paperEdge, 1.0f);

    if (rt_->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return false;
    }
    return true;
}

bool MainWindow::RenderCapsuleEntryLayered() {
    if (layeredMode_ != 2) UpdateLayeredState();

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    if (w <= 0 || h <= 0) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(screen);
    if (!bmp || !mem || !bits) {
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        ReleaseDC(nullptr, screen);
        return false;
    }

    std::memset(bits, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    PixelCanvas canvas{ static_cast<uint32_t*>(bits), w, h };
    const bool rightDock = CapsuleDockEdge() == DockEdge::Right;
    if (capsuleStyle_ == CapsuleStyle::Slim)
        DrawCapsulePetEntry(canvas, rightDock, capsuleHover_);
    else
        DrawCapsuleOrbEntry(canvas, rightDock, capsuleHover_);

    HGDIOBJ old = SelectObject(mem, bmp);
    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    POINT dst{ wr.left, wr.top };
    POINT src{ 0, 0 };
    SIZE size{ w, h };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL ok = UpdateLayeredWindow(hwnd_, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
    return ok != FALSE;
}

// ——————————————————————————— 鼠标 / 键盘 ———————————————————————————

void MainWindow::OnLButtonDown(float x, float y) {
    if (animActive_) return;                                     // 动画中忽略点击
    if (capsuleShrunk()) { BeginCapsulePress((int)x, (int)y); return; } // 区分点击展开 / 拖动吸附
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (calendarActive()) {
        if (calendarMode() != CalendarViewMode::Day) {
            // Week and month are read-only browse surfaces; act on button up.
            pressHit_ = h;
            return;
        }
        if (calendarEditing()) {
            if (CalendarEditSurfaceContainsPoint(calendarEditId_, x, y)) {
                pressHit_ = Hit{};
                FocusCalendarEditor(CalendarEditFocusFromPoint(calendarEditId_, x, y), false);
                return;
            }
            EndCalendarEdit(true);
        }
        pressHit_ = h;
        if (h.kind == HitKind::CalendarEmptyTimeline) {
            calendarDrag_.mode = CalendarDragMode::PendingCreate;
            calendarDrag_.startX = x;
            calendarDrag_.startY = y;
            calendarDrag_.anchorMinute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
            return;
        }
        if (h.kind == HitKind::CalendarBlock || h.kind == HitKind::CalendarResizeStart ||
            h.kind == HitKind::CalendarResizeEnd) {
            const CalendarBlock* block = calendar_.FindBlock(h.itemIndex);
            if (block) {
                calendarDrag_.mode = CalendarDragMode::PendingBlock;
                calendarDrag_.blockId = block->id;
                calendarDrag_.startX = x;
                calendarDrag_.startY = y;
                calendarDrag_.anchorMinute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
                calendarDrag_.originalStart = block->startMinute;
                calendarDrag_.originalEnd = block->endMinute;
            }
        }
        return;
    }
    pressHit_ = h;
    if (h.kind == HitKind::Handle) {
        dragging_   = true;
        dragFrom_   = h.itemIndex;
        dragY_      = y;
        dragInsert_ = h.itemIndex;
    }
}

void MainWindow::OnLButtonUp(float x, float y) {
    if (capsulePressing_) { FinishCapsulePress(); return; } // 胶囊按压：松手收尾（展开或吸附）
    if (calendarActive() && calendarDrag_.mode != CalendarDragMode::None) {
        const CalendarDragMode mode = calendarDrag_.mode;
        const int blockId = calendarDrag_.blockId;
        ResetCalendarDrag();
        if (mode == CalendarDragMode::Creating && calendar_.FindBlock(blockId)) {
            BuildCalendarBlockRects();
            BeginCalendarEdit(blockId, CalendarEditFocus::Title);
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if ((mode == CalendarDragMode::Moving || mode == CalendarDragMode::ResizingStart ||
             mode == CalendarDragMode::ResizingEnd) && calendar_.FindBlock(blockId)) {
            if (calendarEditId_ == blockId) SyncCalendarEditors();
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (mode == CalendarDragMode::PendingBlock && calendar_.FindBlock(blockId)) {
            CalendarEditFocus focus = CalendarEditFocusFromPoint(blockId, x, y);
            if (pressHit_.kind == HitKind::CalendarResizeStart) focus = CalendarEditFocus::StartTime;
            else if (pressHit_.kind == HitKind::CalendarResizeEnd) focus = CalendarEditFocus::EndTime;
            BeginCalendarEdit(blockId, focus);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dragging_) {
        dragging_ = false;
        if (dragInsert_ >= 0) {
            bool moved = model_.MoveActive(dragFrom_, dragInsert_);
            if (moved) {
                RebuildLayout();
                ScheduleSave();
            }
        }
        dragFrom_ = dragInsert_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    Hit h = HitTest(x, y);
    if (h.kind != pressHit_.kind || h.itemIndex != pressHit_.itemIndex ||
        h.rowIndex != pressHit_.rowIndex) {
        pressHit_ = Hit{};
        return;
    }

    switch (h.kind) {
    case HitKind::TreeToggle:
        if (model_.ToggleCollapsed(h.itemIndex)) {
            RebuildLayout();
            ClampScroll();
            ScheduleSave();
        }
        break;
    case HitKind::Check:
        model_.SetDone(h.itemIndex, !model_.Items()[h.itemIndex].done);
        RebuildLayout();
        ClampScroll();
        RefreshTrayIcon();
        ScheduleSave();
        break;
    case HitKind::Text:
        BeginEdit(h.itemIndex);
        break;
    case HitKind::Delete: {
        int subtreeSize = model_.SubtreeEnd(h.itemIndex) - h.itemIndex;
        bool confirmed = false;
        if (subtreeSize > 1) {
            std::wstring msg = (lang_ == Lang::Zh)
                ? (L"删除这一项及其 " + std::to_wstring(subtreeSize - 1) + L" 个子项？")
                : (L"Delete this item and its " + std::to_wstring(subtreeSize - 1) + L" children?");
            confirmed = ConfirmText(msg, true);
        } else {
            confirmed = Confirm(Str::DeleteItemMsg, MB_ICONQUESTION);
        }
        if (confirmed)
            DeleteItem(h.itemIndex);
        break;
    }
    case HitKind::Section: ToggleCompletedExpanded(); break;
    case HitKind::Clear:   ClearCompletedConfirm();   break;
    case HitKind::ListTab: SwitchList(h.itemIndex);   break;
    case HitKind::AddList: CreateList();              break;
    case HitKind::Calendar: SetActiveView(calendarActive() ? MainView::Lists : MainView::Calendar); break;
    case HitKind::CalendarPrevDay: SwitchCalendarPeriod(-1); break;
    case HitKind::CalendarNextDay: SwitchCalendarPeriod(1);  break;
    case HitKind::CalendarToday:   GoToCalendarToday();   break;
    case HitKind::CalendarModeDay:   SetCalendarMode(CalendarViewMode::Day);   break;
    case HitKind::CalendarModeWeek:  SetCalendarMode(CalendarViewMode::Week);  break;
    case HitKind::CalendarModeMonth: SetCalendarMode(CalendarViewMode::Month); break;
    case HitKind::CalendarWeekDayHeader:
        DrillToCalendarDay(CalendarWeekDayKey(h.rowIndex));
        break;
    case HitKind::CalendarWeekBlock: {
        const CalendarBlock* block = calendar_.FindBlock(h.itemIndex);
        if (block) DrillToCalendarDay(block->day);
        break;
    }
    case HitKind::CalendarMonthCell:
        DrillToCalendarDay(CalendarMonthCellDayKey(h.itemIndex));
        break;
    case HitKind::CalendarBlock:
        BeginCalendarEdit(h.itemIndex, CalendarEditFocusFromPoint(h.itemIndex, x, y));
        break;
    case HitKind::CalendarResizeStart:
        BeginCalendarEdit(h.itemIndex, CalendarEditFocus::StartTime);
        break;
    case HitKind::CalendarResizeEnd:
        BeginCalendarEdit(h.itemIndex, CalendarEditFocus::EndTime);
        break;
    case HitKind::EmptyActive:
        CreateEmptyActiveItem();
        break;
    case HitKind::AddTask:
        BeginNewTask();
        break;
    case HitKind::Menu:    ShowTitleMenu();           break;
    case HitKind::Theme:   ShowThemeMenu();           break;
    case HitKind::Pin:     TogglePin();               break;
    case HitKind::Close:   HideToTray();              break;
    default: break;
    }
    pressHit_ = Hit{};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::OnLButtonDoubleClick(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::Calendar) { SetActiveView(calendarActive() ? MainView::Lists : MainView::Calendar); return; }
    if (h.kind == HitKind::ListTab) RenameList(h.itemIndex);
}

void MainWindow::OnRButtonUp(float x, float y) {
    if (animActive_ || capsuleShrunk()) return;
    if (editing()) CommitEdit(false);
    Hit h = HitTest(x, y);
    if (h.kind == HitKind::Calendar) { SetActiveView(calendarActive() ? MainView::Lists : MainView::Calendar); return; }
    if (h.kind == HitKind::ListTab) { ShowListTabMenu(h.itemIndex, x, y); return; }
    if (h.kind == HitKind::CalendarBlock || h.kind == HitKind::CalendarResizeStart ||
        h.kind == HitKind::CalendarResizeEnd) {
        ShowCalendarBlockMenu(h.itemIndex, x, y);
    }
}

void MainWindow::OnMouseMove(float x, float y, bool lButton) {
    if (capsulePressing_) { UpdateCapsulePress(lButton); return; } // 胶囊按压：判定拖动并跟随
    if (calendarActive() && calendarDrag_.mode != CalendarDragMode::None) {
        if (!lButton) { ResetCalendarDrag(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        const int minute = GuiCalendar::MinuteFromPoint(y - ContentTop(), calendarScroll_, calendarFrame_);
        if (calendarDrag_.mode == CalendarDragMode::PendingCreate) {
            if (!GuiCalendar::DragExceeded(calendarDrag_.startX, calendarDrag_.startY, x, y, dpiScale()))
                return;
            const GuiCalendar::TimeRange range = GuiCalendar::RangeFromDrag(calendarDrag_.anchorMinute, minute);
            const int id = calendar_.AddBlock(calendarDay_, range.startMinute, range.endMinute, L"");
            if (id > 0) {
                calendarDrag_.mode = CalendarDragMode::Creating;
                calendarDrag_.blockId = id;
                BuildCalendarBlockRects();
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return;
        }

        if (calendarDrag_.mode == CalendarDragMode::PendingBlock) {
            if (!GuiCalendar::DragExceeded(calendarDrag_.startX, calendarDrag_.startY, x, y, dpiScale()))
                return;
            if (pressHit_.kind == HitKind::CalendarResizeStart)
                calendarDrag_.mode = CalendarDragMode::ResizingStart;
            else if (pressHit_.kind == HitKind::CalendarResizeEnd)
                calendarDrag_.mode = CalendarDragMode::ResizingEnd;
            else
                calendarDrag_.mode = CalendarDragMode::Moving;
        }

        bool changed = false;
        if (calendarDrag_.mode == CalendarDragMode::Creating) {
            const GuiCalendar::TimeRange range = GuiCalendar::RangeFromDrag(calendarDrag_.anchorMinute, minute);
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, range.startMinute, range.endMinute);
        } else if (calendarDrag_.mode == CalendarDragMode::Moving) {
            // 按实际位移自由移动，再把结果整体吸附到 15 分，而不是以 15 分为步长。
            const int duration = calendarDrag_.originalEnd - calendarDrag_.originalStart;
            int start = GuiCalendar::SnapMinute(
                calendarDrag_.originalStart + (minute - calendarDrag_.anchorMinute));
            if (start < 0) start = 0;
            if (start + duration > 1440) start = 1440 - duration;
            if (start < 0) start = 0;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, start, start + duration);
        } else if (calendarDrag_.mode == CalendarDragMode::ResizingStart) {
            int start = GuiCalendar::SnapMinute(minute);
            if (start > calendarDrag_.originalEnd - 15) start = calendarDrag_.originalEnd - 15;
            if (start < 0) start = 0;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, start, calendarDrag_.originalEnd);
        } else if (calendarDrag_.mode == CalendarDragMode::ResizingEnd) {
            int end = GuiCalendar::SnapMinute(minute);
            if (end < calendarDrag_.originalStart + 15) end = calendarDrag_.originalStart + 15;
            if (end > 1440) end = 1440;
            changed = calendar_.SetBlockRange(calendarDrag_.blockId, calendarDrag_.originalStart, end);
        }
        if (changed) {
            BuildCalendarBlockRects();
            if (calendarEditId_ == calendarDrag_.blockId) LayoutCalendarEditControls();
            ScheduleSave();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (dragging_) {
        dragY_ = y;
        float docY = y - ContentTop() + scroll_;
        int active = model_.ActiveCount();
        int insert = active;
        for (const RowLayout& r : rows_) {
            if (r.completed) break;
            if (docY < (r.row.top + r.row.bottom) / 2) { insert = r.itemIndex; break; }
        }
        int dragEnd = model_.SubtreeEnd(dragFrom_);
        if (insert > dragFrom_ && insert < dragEnd) insert = dragEnd;
        dragInsert_ = insert;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    Hit h = HitTest(x, y);
    int hover = (h.kind == HitKind::EmptyActive) ? kHoverEmptyActive
              : (h.kind == HitKind::AddTask)     ? kHoverAddTask
              : h.rowIndex;
    if (hover != hoverRow_) {
        hoverRow_ = hover;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseLeave() {
    if (hoverRow_ != -1) {
        hoverRow_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::OnMouseWheel(int delta) {
    if (calendarActive()) {
        if (calendarMode() == CalendarViewMode::Month) return; // month grid does not scroll
        calendarScroll_ -= (delta / 120.0f) * S(96);
        ClampCalendarScroll();
        if (calendarEditing()) LayoutCalendarEditControls();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    scroll_ -= (delta / 120.0f) * S(48);
    ClampScroll();
    if (editing()) LayoutEditBox();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT MainWindow::OnNcHitTest(int sx, int sy) {
    POINT p{ sx, sy };
    ScreenToClient(hwnd_, &p);
    RECT rc;
    GetClientRect(hwnd_, &rc);

    GuiHit::Input input;
    input.x = (float)p.x;
    input.y = (float)p.y;
    input.width = (float)(rc.right - rc.left);
    input.height = (float)(rc.bottom - rc.top);
    input.dpiScale = dpiScale();
    input.titleHeight = Theme::kTitleH;
    input.resizeEdge = Theme::kResizeEdge;
    input.forceClient = mountMode_ == MountMode::Capsule && (capsuleShrunk() || animActive_);
    input.capsuleMode = mountMode_ == MountMode::Capsule;
    input.menu = ToGuiRect(menuRect_);
    input.theme = ToGuiRect(themeRect_);
    input.pin = ToGuiRect(pinRect_);
    input.close = ToGuiRect(closeRect_);

    switch (GuiHit::HitTestNonClient(input)) {
    case GuiHit::NonClientHit::Client:      return HTCLIENT;
    case GuiHit::NonClientHit::Caption:     return HTCAPTION;
    case GuiHit::NonClientHit::Left:        return HTLEFT;
    case GuiHit::NonClientHit::Right:       return HTRIGHT;
    case GuiHit::NonClientHit::Top:         return HTTOP;
    case GuiHit::NonClientHit::Bottom:      return HTBOTTOM;
    case GuiHit::NonClientHit::TopLeft:     return HTTOPLEFT;
    case GuiHit::NonClientHit::TopRight:    return HTTOPRIGHT;
    case GuiHit::NonClientHit::BottomLeft:  return HTBOTTOMLEFT;
    case GuiHit::NonClientHit::BottomRight: return HTBOTTOMRIGHT;
    }
    return HTCLIENT;
}

// ——————————————————————————— 日历视图 / 编辑 ———————————————————————————

void MainWindow::EnsureCalendarDay() {
    if (!calendarDay_.empty() && IsValidCalendarDayKey(calendarDay_)) return;
    if (IsValidCalendarDayKey(ui_.calendarDay)) calendarDay_ = ui_.calendarDay;
    else calendarDay_ = TodayDayKey();
    ui_.calendarDay = calendarDay_;
}

void MainWindow::SetActiveView(MainView view) {
    if (view == activeView_) return;
    if (editing()) CommitEdit(false);
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    activeView_ = view;
    ui_.activeView = view == MainView::Calendar ? "calendar" : "list";
    hoverRow_ = -1;
    dragging_ = false;
    dragFrom_ = dragInsert_ = -1;
    if (calendarActive()) {
        EnsureCalendarDay();
        calendarScrollInitialized_ = false;
    }
    RebuildLayout();
    if (calendarActive()) AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SwitchCalendarDay(int deltaDays) {
    if (deltaDays == 0) return;
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    EnsureCalendarDay();
    calendarDay_ = OffsetCalendarDayKey(deltaDays);
    ui_.calendarDay = calendarDay_;
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::GoToCalendarToday() {
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    const std::string today = TodayDayKey();
    if (calendarDay_ != today) {
        calendarDay_ = today;
        ui_.calendarDay = calendarDay_;
        ScheduleSave();
    }
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::string MainWindow::OffsetCalendarDayKey(int deltaDays) const {
    return OffsetDayKey(calendarDay_.empty() ? TodayDayKey() : calendarDay_, deltaDays);
}

void MainWindow::ClampCalendarScroll() {
    if (calendarMode() == CalendarViewMode::Month) { calendarScroll_ = 0.0f; return; }
    const bool week = calendarMode() == CalendarViewMode::Week;
    const float timelineH = week ? calendarWeekFrame_.timelineViewport.Height()
                                 : calendarFrame_.timelineViewport.Height();
    const float contentH = week ? calendarWeekFrame_.contentHeight : calendarFrame_.contentHeight;
    float maxScroll = contentH - timelineH;
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (calendarScroll_ < 0.0f) calendarScroll_ = 0.0f;
    if (calendarScroll_ > maxScroll) calendarScroll_ = maxScroll;
}

void MainWindow::AlignCalendarScrollToNow(bool force) {
    if (!force && calendarScrollInitialized_) return;
    EnsureCalendarDay();
    if (calendarMode() == CalendarViewMode::Month) {
        calendarScroll_ = 0.0f;
        calendarScrollInitialized_ = true;
        return;
    }
    const int minute = (calendarDay_ == TodayDayKey()) ? CurrentMinuteOfDay() : 8 * 60;
    // Day and week share the same hour scale and content height, so the day frame
    // drives the scroll target for both.
    calendarScroll_ = GuiCalendar::ScrollForMinute(minute, ViewportHeight(), calendarFrame_);
    calendarScrollInitialized_ = true;
    ClampCalendarScroll();
}

void MainWindow::BuildCalendarBlockRects() {
    calendarBlockRects_.clear();
    EnsureCalendarDay();
    for (const CalendarBlock* block : calendar_.BlocksForDay(calendarDay_)) {
        if (!block) continue;
        calendarBlockRects_.push_back(GuiCalendar::BlockRect{
            block->id,
            GuiCalendar::ComputeBlockRect(calendarFrame_, block->id, block->startMinute, block->endMinute)
        });
    }
}

void MainWindow::BuildCalendarWeekBlockRects() {
    calendarWeekBlockRects_.clear();
    EnsureCalendarDay();
    CalendarDate::Date anchor;
    if (!CalendarDate::Parse(calendarDay_, anchor)) return;
    const CalendarDate::Date weekStart = CalendarDate::StartOfWeek(anchor);
    for (int day = 0; day < 7; ++day) {
        const std::string key = CalendarDate::Format(CalendarDate::AddDays(weekStart, day));
        // BlocksForDay preserves the model's sort by start, then end, then id,
        // which is exactly what PackDayLanes expects.
        const std::vector<const CalendarBlock*> blocks = calendar_.BlocksForDay(key);
        std::vector<GuiCalendar::TimeRange> spans;
        spans.reserve(blocks.size());
        for (const CalendarBlock* block : blocks)
            spans.push_back(GuiCalendar::TimeRange{ block->startMinute, block->endMinute });
        const std::vector<GuiCalendar::LaneSpan> lanes = GuiCalendar::PackDayLanes(spans);
        for (size_t i = 0; i < blocks.size(); ++i) {
            calendarWeekBlockRects_.push_back(GuiCalendar::WeekBlockRect{
                blocks[i]->id, day,
                GuiCalendar::ComputeWeekBlockRect(calendarWeekFrame_, day, lanes[i],
                                                  blocks[i]->startMinute, blocks[i]->endMinute) });
        }
    }
}

void MainWindow::SetCalendarMode(CalendarViewMode mode) {
    if (mode == calendarMode()) return;
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    ui_.calendarView = mode;
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SwitchCalendarPeriod(int dir) {
    if (dir == 0) return;
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    EnsureCalendarDay();

    CalendarDate::Date d;
    std::string next;
    if (CalendarDate::Parse(calendarDay_, d)) {
        switch (calendarMode()) {
        case CalendarViewMode::Day:   next = CalendarDate::Format(CalendarDate::AddDays(d, dir));      break;
        case CalendarViewMode::Week:  next = CalendarDate::Format(CalendarDate::AddDays(d, dir * 7));  break;
        case CalendarViewMode::Month: next = CalendarDate::Format(CalendarDate::AddMonths(d, dir));    break;
        }
    } else {
        next = TodayDayKey();
    }
    calendarDay_ = next;
    ui_.calendarDay = next;
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::DrillToCalendarDay(const std::string& dayKey) {
    if (!IsValidCalendarDayKey(dayKey)) return;
    if (calendarEditing()) EndCalendarEdit(true);
    ResetCalendarDrag();
    calendarDay_ = dayKey;
    ui_.calendarDay = dayKey;
    ui_.calendarView = CalendarViewMode::Day;
    calendarScrollInitialized_ = false;
    RebuildLayout();
    AlignCalendarScrollToNow(false);
    ScheduleSave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::string MainWindow::CalendarWeekDayKey(int dayIndex) const {
    CalendarDate::Date d;
    if (!CalendarDate::Parse(calendarDay_, d)) return calendarDay_;
    return CalendarDate::Format(CalendarDate::AddDays(CalendarDate::StartOfWeek(d), dayIndex));
}

std::string MainWindow::CalendarMonthCellDayKey(int cellIndex) const {
    CalendarDate::Date d;
    if (!CalendarDate::Parse(calendarDay_, d)) return calendarDay_;
    return CalendarDate::Format(
        CalendarDate::AddDays(CalendarDate::MonthGridStart(d.year, d.month), cellIndex));
}

void MainWindow::EnsureCalendarEditors() {
    if (!editFont_) {
        LOGFONTW lf{};
        lf.lfHeight  = -(LONG)S(Theme::kFontSize);
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
        editFont_ = CreateFontIndirectW(&lf);
    }
    auto createEdit = [&](HWND& edit, DWORD style) {
        if (edit) return;
        edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style,
                               0, 0, 10, 10, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(edit, CalendarEditProcStatic, 2, (DWORD_PTR)this);
        SendMessageW(edit, WM_SETFONT, (WPARAM)editFont_, TRUE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(RoundToInt(S(3)), RoundToInt(S(3))));
    };
    createEdit(calendarTitleEdit_, ES_AUTOHSCROLL);
    createEdit(calendarStartEdit_, ES_AUTOHSCROLL | ES_CENTER);
    createEdit(calendarEndEdit_, ES_AUTOHSCROLL | ES_CENTER);
    // 空标题显示占位提示，避免只剩一根光标
    SendMessageW(calendarTitleEdit_, EM_SETCUEBANNER, TRUE,
                 (LPARAM)(lang_ == Lang::Zh ? L"内容" : L"Title"));
}

void MainWindow::BeginCalendarEdit(int blockId, CalendarEditFocus focus) {
    const CalendarBlock* block = calendar_.FindBlock(blockId);
    if (!block) return;
    if (editing()) CommitEdit(false);
    EnsureCalendarEditors();
    calendarEditId_ = blockId;

    // 记录编辑块底色：输入框以同色融入，文字用块内固定深色。
    {
        const auto dayBlocks = calendar_.BlocksForDay(calendarDay_);
        const std::vector<int> conflictIds = ConflictingBlockIds(dayBlocks);
        int idx = 0;
        for (size_t i = 0; i < dayBlocks.size(); ++i)
            if (dayBlocks[i] && dayBlocks[i]->id == blockId) { idx = static_cast<int>(i); break; }
        bool conflict = false;
        for (int c : conflictIds) if (c == blockId) { conflict = true; break; }
        calendarEditFill_ = conflict ? CalendarTheme::kConflict.fill
                                     : CalendarTheme::BlockColorAt(idx).fill;
        if (calendarEditBg_) { DeleteObject(calendarEditBg_); calendarEditBg_ = nullptr; }
    }

    SyncCalendarEditors();
    LayoutCalendarEditControls();
    ShowWindow(calendarTitleEdit_, SW_SHOW);
    ShowWindow(calendarStartEdit_, SW_SHOW);
    ShowWindow(calendarEndEdit_, SW_SHOW);
    FocusCalendarEditor(focus, true);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::EndCalendarEdit(bool removeEmpty) {
    if (!calendarEditing()) return;
    const int blockId = calendarEditId_;
    if (calendarTitleEdit_) calendar_.SetBlockTitle(blockId, ReadWindowText(calendarTitleEdit_));
    CommitCalendarTimeEdits(true);
    calendarEditId_ = -1;
    calendarEditFocus_ = CalendarEditFocus::Title;
    HideCalendarEditors();
    if (removeEmpty && CalendarBlockTitleEmpty(blockId)) {
        calendar_.RemoveBlock(blockId);
    }
    BuildCalendarBlockRects();
    ScheduleSave();
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::HideCalendarEditors() {
    if (calendarTitleEdit_) ShowWindow(calendarTitleEdit_, SW_HIDE);
    if (calendarStartEdit_) ShowWindow(calendarStartEdit_, SW_HIDE);
    if (calendarEndEdit_) ShowWindow(calendarEndEdit_, SW_HIDE);
}

void MainWindow::SyncCalendarEditors() {
    if (!calendarEditing()) return;
    const CalendarBlock* block = calendar_.FindBlock(calendarEditId_);
    if (!block) return;
    EnsureCalendarEditors();
    calendarSyncing_ = true;
    SetWindowTextW(calendarTitleEdit_, block->title.c_str());
    SetWindowTextW(calendarStartEdit_, GuiCalendar::FormatTimeText(block->startMinute).c_str());
    SetWindowTextW(calendarEndEdit_, GuiCalendar::FormatTimeText(block->endMinute).c_str());
    calendarSyncing_ = false;
}

void MainWindow::LayoutCalendarEditControls() {
    if (!calendarEditing() || !calendarTitleEdit_) return;
    const CalendarBlock* block = calendar_.FindBlock(calendarEditId_);
    if (!block) { EndCalendarEdit(false); return; }

    Gui::Rect rect{};
    bool found = false;
    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        if (blockRect.blockId == calendarEditId_) {
            rect = blockRect.rect;
            found = true;
            break;
        }
    }
    if (!found) { HideCalendarEditors(); return; }

    // 几何与 DrawCalendarView 的编辑字段 frame 对齐，子 HWND 内缩避免盖住边框。
    const GuiCalendar::EditLayout layout = GuiCalendar::ComputeEditLayout(rect, dpiScale());
    const float clientTop = ContentTop() + calendarFrame_.timelineViewport.top - calendarScroll_;
    const int viewportTop = RoundToInt(ContentTop() + calendarFrame_.timelineViewport.top);
    const int viewportBottom = RoundToInt(ContentTop() + calendarFrame_.timelineViewport.bottom);
    const int blockTop = RoundToInt(clientTop + layout.block.top);
    const int blockBottom = RoundToInt(clientTop + layout.block.bottom);
    if (blockTop >= viewportBottom || blockBottom <= viewportTop || layout.block.Width() <= S(40)) {
        HideCalendarEditors();
        return;
    }

    auto moveEdit = [&](HWND edit, const Gui::Rect& editRect) {
        const int left = RoundToInt(editRect.left);
        const int top = RoundToInt(clientTop + editRect.top);
        const int width = RoundToInt(editRect.Width());
        const int height = RoundToInt(editRect.Height());
        MoveWindow(edit, left, top, width, height, TRUE);
    };
    moveEdit(calendarTitleEdit_, layout.titleEdit);
    moveEdit(calendarStartEdit_, layout.startEdit);
    moveEdit(calendarEndEdit_, layout.endEdit);
    ShowWindow(calendarTitleEdit_, SW_SHOW);
    ShowWindow(calendarStartEdit_, SW_SHOW);
    ShowWindow(calendarEndEdit_, SW_SHOW);
}

void MainWindow::OnCalendarEditChanged(HWND edit) {
    if (calendarSyncing_ || !calendarEditing()) return;
    if (edit == calendarTitleEdit_) {
        calendar_.SetBlockTitle(calendarEditId_, ReadWindowText(calendarTitleEdit_));
        ScheduleSave();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    // 时间字段不在每次输入时提交：回车或失焦时才提交（见 CalendarEditProcStatic），
    // 否则刚敲到合法值块就跳走、和输入打架。
}

bool MainWindow::CommitCalendarTimeEdits(bool syncText) {
    if (!calendarEditing() || !calendarStartEdit_ || !calendarEndEdit_) return false;
    if (!calendar_.FindBlock(calendarEditId_)) return false;
    GuiCalendar::TimeRange range;
    if (!GuiCalendar::ParseTimeRangeText(ReadWindowText(calendarStartEdit_),
                                         ReadWindowText(calendarEndEdit_),
                                         range))
        return false;

    if (calendar_.SetBlockRange(calendarEditId_, range.startMinute, range.endMinute)) {
        BuildCalendarBlockRects();
        LayoutCalendarEditControls();
        if (syncText) SyncCalendarEditors();
        ScheduleSave();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    return false;
}

bool MainWindow::CalendarEditSurfaceContainsPoint(int blockId, float x, float y) const {
    Gui::Rect rect{};
    bool found = false;
    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        if (blockRect.blockId == blockId) {
            rect = blockRect.rect;
            found = true;
            break;
        }
    }
    if (!found) return false;

    const float docY = y - ContentTop() - calendarFrame_.timelineViewport.top + calendarScroll_;
    const GuiCalendar::EditLayout layout = GuiCalendar::ComputeEditLayout(rect, dpiScale());
    return layout.block.Contains(x, docY);
}

MainWindow::CalendarEditFocus MainWindow::CalendarEditFocusFromPoint(int blockId,
                                                                     float x,
                                                                     float y) const {
    Gui::Rect rect{};
    bool found = false;
    for (const GuiCalendar::BlockRect& blockRect : calendarBlockRects_) {
        if (blockRect.blockId == blockId) {
            rect = blockRect.rect;
            found = true;
            break;
        }
    }
    if (!found) return CalendarEditFocus::Title;

    const float docY = y - ContentTop() - calendarFrame_.timelineViewport.top + calendarScroll_;
    const GuiCalendar::EditLayout layout = GuiCalendar::ComputeEditLayout(rect, dpiScale());
    switch (GuiCalendar::HitTestEditField(x, docY, layout)) {
    case GuiCalendar::EditField::StartTime: return CalendarEditFocus::StartTime;
    case GuiCalendar::EditField::EndTime:   return CalendarEditFocus::EndTime;
    case GuiCalendar::EditField::Title:
        return CalendarEditFocus::Title;
    case GuiCalendar::EditField::None:
        return CalendarEditFocus::Title;
    }
    return CalendarEditFocus::Title;
}

MainWindow::CalendarEditFocus MainWindow::CalendarEditFocusFromHwnd(HWND edit) const {
    if (edit == calendarStartEdit_) return CalendarEditFocus::StartTime;
    if (edit == calendarEndEdit_) return CalendarEditFocus::EndTime;
    return CalendarEditFocus::Title;
}

MainWindow::CalendarEditFocus MainWindow::NextCalendarEditFocus(HWND edit, bool reverse) const {
    const CalendarEditFocus current = CalendarEditFocusFromHwnd(edit);
    if (!reverse) {
        if (current == CalendarEditFocus::Title) return CalendarEditFocus::StartTime;
        if (current == CalendarEditFocus::StartTime) return CalendarEditFocus::EndTime;
        return CalendarEditFocus::Title;
    }
    if (current == CalendarEditFocus::Title) return CalendarEditFocus::EndTime;
    if (current == CalendarEditFocus::EndTime) return CalendarEditFocus::StartTime;
    return CalendarEditFocus::Title;
}

void MainWindow::FocusCalendarEditor(CalendarEditFocus focus, bool selectAll) {
    HWND target = calendarTitleEdit_;
    if (focus == CalendarEditFocus::StartTime) target = calendarStartEdit_;
    else if (focus == CalendarEditFocus::EndTime) target = calendarEndEdit_;
    if (!target) return;
    calendarEditFocus_ = focus;
    SetFocus(target);
    const int len = GetWindowTextLengthW(target);
    if (selectAll) SendMessageW(target, EM_SETSEL, 0, len);
    else SendMessageW(target, EM_SETSEL, len, len);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool MainWindow::IsCalendarEditorHwnd(HWND edit) const {
    return edit == calendarTitleEdit_ || edit == calendarStartEdit_ || edit == calendarEndEdit_;
}

bool MainWindow::IsCalendarEditInternalFocusTarget(HWND target) const {
    return target == hwnd_ || IsCalendarEditorHwnd(target);
}

bool MainWindow::CalendarBlockTitleEmpty(int blockId) const {
    const CalendarBlock* block = calendar_.FindBlock(blockId);
    if (!block) return true;
    return Trim(block->title).empty();
}

void MainWindow::ResetCalendarDrag() {
    calendarDrag_ = CalendarDragState{};
}

void MainWindow::CancelCalendarCapture() {
    if (calendarDrag_.mode == CalendarDragMode::Creating && CalendarBlockTitleEmpty(calendarDrag_.blockId)) {
        calendar_.RemoveBlock(calendarDrag_.blockId);
        BuildCalendarBlockRects();
    }
    ResetCalendarDrag();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK MainWindow::CalendarEditProcStatic(HWND h, UINT m, WPARAM w, LPARAM l,
                                                    UINT_PTR id, DWORD_PTR ref) {
    (void)id;
    MainWindow* self = reinterpret_cast<MainWindow*>(ref);
    switch (m) {
    case WM_GETDLGCODE:
        return DefSubclassProc(h, m, w, l) | DLGC_WANTALLKEYS | DLGC_WANTTAB;
    case WM_SETFOCUS:
        self->calendarEditFocus_ = self->CalendarEditFocusFromHwnd(h);
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    case WM_LBUTTONDOWN:
        self->calendarEditFocus_ = self->CalendarEditFocusFromHwnd(h);
        InvalidateRect(self->hwnd_, nullptr, FALSE);
        break;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            self->EndCalendarEdit(true);
            return 0;
        }
        if (w == VK_TAB) {
            const bool reverse = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            self->FocusCalendarEditor(self->NextCalendarEditFocus(h, reverse), true);
            return 0;
        }
        if (w == VK_RETURN) {
            self->EndCalendarEdit(true);
            return 0;
        }
        break;
    case WM_CHAR:
        if (w == L'\r' || w == L'\t' || w == 0x1B) return 0;
        break;
    case WM_KILLFOCUS: {
        HWND next = reinterpret_cast<HWND>(w);
        if (self->IsCalendarEditInternalFocusTarget(next))
            break;
        self->EndCalendarEdit(true);
        return 0;
    }
    }
    return DefSubclassProc(h, m, w, l);
}

// ——————————————————————————— 行内编辑 ———————————————————————————

void MainWindow::BeginEdit(int itemIndex) {
    if (itemIndex < 0 || itemIndex >= model_.Count()) return;
    if (model_.Items()[itemIndex].done) return; // 已完成不可编辑
    if (editing()) CommitEdit(false);

    editIndex_ = itemIndex;

    if (!edit_) {
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL,
                                0, 0, 10, 10, hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(edit_, EditProcStatic, 1, (DWORD_PTR)this);
    }

    if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
    LOGFONTW lf{};
    lf.lfHeight  = -(LONG)S(Theme::kFontSize);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, Theme::kFontFamily);
    editFont_ = CreateFontIndirectW(&lf);
    SendMessageW(edit_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

    SetWindowTextW(edit_, model_.Items()[itemIndex].text.c_str());
    LayoutEditBox();
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    int n = GetWindowTextLengthW(edit_);
    SendMessageW(edit_, EM_SETSEL, n, n); // 光标置末尾
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CommitEdit(bool addNext) {
    if (!editing()) return;
    int idx = editIndex_;
    editIndex_ = -1;

    std::wstring text = ReadWindowText(edit_);
    ShowWindow(edit_, SW_HIDE);

    text = NormalizeTodoText(std::move(text));
    if (text.empty()) model_.Remove(idx);
    else              model_.SetText(idx, text);

    RebuildLayout();
    RefreshTrayIcon();
    ScheduleSave();

    if (addNext && !text.empty()) {
        int nextLevel = (idx >= 0 && idx < model_.Count()) ? model_.Items()[idx].level : 0;
        int insertAt = (idx >= 0 && idx < model_.ActiveCount()) ? model_.SubtreeEnd(idx) : model_.ActiveCount();
        int n = model_.InsertActive(insertAt, L"", nextLevel);
        RebuildLayout();
        ScrollItemIntoView(n);
        RefreshTrayIcon();
        BeginEdit(n);
    } else {
        ClampScroll();
        SetFocus(hwnd_);
        MaybeCollapseCapsule(); // 编辑结束，鼠标已在外则收回胶囊
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CancelEdit() {
    if (!editing()) return;
    int idx = editIndex_;
    editIndex_ = -1;
    ShowWindow(edit_, SW_HIDE);

    // 新建后未输入即取消的空项：移除
    if (idx >= 0 && idx < model_.Count() && model_.Items()[idx].text.empty())
        model_.Remove(idx);

    RebuildLayout();
    RefreshTrayIcon();
    ClampScroll();
    SetFocus(hwnd_);
    MaybeCollapseCapsule(); // 取消编辑后，鼠标已在外则收回胶囊
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SyncActiveEditorsForSave() {
    if (editing() && edit_ && editIndex_ >= 0 && editIndex_ < model_.Count() &&
        !model_.Items()[editIndex_].done) {
        model_.SetText(editIndex_, NormalizeTodoText(ReadWindowText(edit_)));
    }
    if (calendarEditing()) {
        if (calendarTitleEdit_)
            calendar_.SetBlockTitle(calendarEditId_, ReadWindowText(calendarTitleEdit_));
        CommitCalendarTimeEdits(false);
    }
}

void MainWindow::MaybeCollapseCapsule() {
    if (mountMode_ != MountMode::Capsule || !capsuleExpanded_ || animActive_ ||
        editing() || calendarEditing()) return;
    POINT cur;
    GetCursorPos(&cur);
    RECT wr;
    if (!GetWindowRect(hwnd_, &wr)) return;
    if (!PtInRect(&wr, cur)) StartCapsuleAnim(false);
}

void MainWindow::LayoutEditBox() {
    if (!editing() || !edit_) return;
    float off = ContentTop() - scroll_;
    for (const RowLayout& r : rows_) {
        if (r.itemIndex != editIndex_ || r.completed) continue;

        const int rowTop = RoundToInt(r.row.top + off);
        const int rowH   = RoundToInt(r.row.bottom - r.row.top);
        const int left   = RoundToInt(r.text.left);
        const int width  = RoundToInt(r.text.right - r.text.left);
        if (rowH <= 0 || width <= 0) return;

        MoveWindow(edit_, left, rowTop, width, rowH, FALSE);
        RECT fmt{ 0, RoundToInt(S(5)), width, rowH };
        SendMessageW(edit_, EM_SETRECTNP, 0, (LPARAM)&fmt);
        MoveWindow(edit_, left, rowTop, width, rowH, TRUE);
        return;
    }
}

LRESULT CALLBACK MainWindow::EditProcStatic(HWND h, UINT m, WPARAM w, LPARAM l,
                                            UINT_PTR id, DWORD_PTR ref) {
    MainWindow* self = reinterpret_cast<MainWindow*>(ref);
    auto refresh = [&]() {
        self->RebuildLayout();
        self->ClampScroll();
        self->LayoutEditBox();
        InvalidateRect(self->hwnd_, nullptr, FALSE);
    };
    switch (m) {
    case WM_GETDLGCODE:
        return DefSubclassProc(h, m, w, l) | DLGC_WANTTAB;
    case WM_KEYDOWN: {
        GuiEdit::Key key = GuiEdit::Key::Other;
        if (w == VK_RETURN) key = GuiEdit::Key::Enter;
        else if (w == VK_ESCAPE) key = GuiEdit::Key::Escape;
        else if (w == VK_TAB) key = GuiEdit::Key::Tab;
        else if (w == VK_DELETE) key = GuiEdit::Key::DeleteKey;

        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const GuiEdit::Intent intent = GuiEdit::KeyDownIntent(key, shiftDown);
        switch (intent) {
        case GuiEdit::Intent::CommitAndAddNext:
            self->CommitEdit(true);
            return 0;
        case GuiEdit::Intent::Cancel:
            self->CancelEdit();
            return 0;
        case GuiEdit::Intent::Indent:
        case GuiEdit::Intent::Outdent: {
            const bool outdent = intent == GuiEdit::Intent::Outdent;
            bool changed = outdent
                ? self->model_.OutdentItem(self->editIndex_)
                : self->model_.IndentItemUnder(self->editIndex_,
                                               self->PreviousVisibleActiveItem(self->editIndex_));
            if (changed) {
                refresh();
                self->ScheduleSave();
            }
            return 0;
        }
        case GuiEdit::Intent::RefreshAfterDefault: {
            LRESULT r = DefSubclassProc(h, m, w, l);
            refresh();
            return r;
        }
        case GuiEdit::Intent::None:
            break;
        }
        break;
    }
    case WM_CHAR:
        if (GuiEdit::SuppressChar((wchar_t)w)) return 0; // 吞掉 Tab/回车/Esc 的字符，避免提示音
        {
            LRESULT r = DefSubclassProc(h, m, w, l);
            refresh();
            return r;
        }
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
    case WM_UNDO: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        refresh();
        return r;
    }
    case WM_KILLFOCUS:
        self->CommitEdit(false);
        break;
    }
    return DefSubclassProc(h, m, w, l); // 注意：仅 4 参，不传 id/ref
}
