#include "Theme.h"
#include "ThemeCatalog.h"

#include <cmath>

namespace Theme {

D2D1_COLOR_F D2DColor(uint32_t rgb, float alpha) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        alpha);
}

COLORREF GdiColor(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

uint32_t Blend(uint32_t fg, uint32_t bg, float a) {
    if (a <= 0.0f) return bg & 0xFFFFFFu;
    if (a >= 1.0f) return fg & 0xFFFFFFu;
    int fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fb = fg & 0xFF;
    int br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int r = br + (int)((fr - br) * a + 0.5f);
    int g = bgc + (int)((fgc - bgc) * a + 0.5f);
    int b = bb + (int)((fb - bb) * a + 0.5f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// sRGB 8bit 分量 -> 线性
static float SrgbToLinear(int c8) {
    float c = c8 / 255.0f;
    return c <= 0.03928f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float RelLuminance(uint32_t rgb) {
    float r = SrgbToLinear((int)((rgb >> 16) & 0xFF));
    float g = SrgbToLinear((int)((rgb >> 8) & 0xFF));
    float b = SrgbToLinear((int)(rgb & 0xFF));
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float ContrastRatio(uint32_t a, uint32_t b) {
    float la = RelLuminance(a), lb = RelLuminance(b);
    float hi = la > lb ? la : lb;
    float lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

ResolveResult ResolveTheme(const ResolveInput& in) {
    ResolveResult out;

    auto findById = [&](const std::string& id) -> const ThemeVisual* {
        if (id.rfind("custom.", 0) == 0) { // 以 custom. 开头：只查自定义表
            if (in.customThemes)
                for (const auto& t : *in.customThemes)
                    if (t.id == id) return &t;
            return nullptr;
        }
        return FindBuiltIn(id); // 其余 id：只查内置表
    };

    std::string wantId;
    if (in.mode == "follow_system")
        wantId = in.systemDark ? in.darkThemeId : in.lightThemeId;
    else // builtin | custom（未知 mode 也按当前选择处理）
        wantId = in.themeId;

    if (const ThemeVisual* t = findById(wantId)) {
        out.theme = *t;
        return out;
    }

    // 解析失败：回退内置 paper（catalog 保证存在）
    out.theme = DefaultTheme();
    out.fellBack = true;
    out.message = std::wstring(wantId.begin(), wantId.end()); // 失败 id 仅含 ASCII，安全
    return out;
}

#ifdef _WIN32
bool SystemUsesDarkMode(bool* ok) {
    if (ok) *ok = false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false; // 读取失败：按浅色
    DWORD value = 1, size = sizeof(value), type = 0;
    LONG r = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                              reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false; // 读取失败：按浅色
    if (ok) *ok = true;
    return value == 0; // AppsUseLightTheme==0 表示深色
}
#else
bool SystemUsesDarkMode(bool* ok) {
    if (ok) *ok = false;
    return false;
}
#endif

} // namespace Theme
