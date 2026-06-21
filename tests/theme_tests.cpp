#include "ThemeCatalog.h"
#include "test_framework.h"

#include <set>
#include <string>
#include <vector>

using namespace xtodo_test;

namespace {

void ColorHelpersConvertClampAndMeasureContrast() {
    const D2D1_COLOR_F color = Theme::D2DColor(0x112233, 0.25f);
    EXPECT_NEAR(color.r, 17.0 / 255.0, 0.000001);
    EXPECT_NEAR(color.g, 34.0 / 255.0, 0.000001);
    EXPECT_NEAR(color.b, 51.0 / 255.0, 0.000001);
    EXPECT_NEAR(color.a, 0.25, 0.000001);

    EXPECT_EQ(Theme::GdiColor(0x112233), static_cast<COLORREF>(0x00332211));
    EXPECT_EQ(Theme::Blend(0x000000, 0xFFFFFF, -1.0f), static_cast<uint32_t>(0xFFFFFF));
    EXPECT_EQ(Theme::Blend(0x000000, 0xFFFFFF, 2.0f), static_cast<uint32_t>(0x000000));
    EXPECT_EQ(Theme::Blend(0x000000, 0xFFFFFF, 0.5f), static_cast<uint32_t>(0x808080));
    EXPECT_NEAR(Theme::ContrastRatio(0x000000, 0xFFFFFF), 21.0, 0.01);
    EXPECT_NEAR(Theme::ContrastRatio(0xFFFFFF, 0x000000), 21.0, 0.01);
}

void BuiltInCatalogIsStableUniqueAndAccessible() {
    const std::vector<std::string> expectedIds = {
        "paper", "mint", "sky", "rose", "sand", "graphite", "ink", "contrast",
    };

    const auto& themes = Theme::BuiltInThemes();
    EXPECT_EQ(themes.size(), expectedIds.size());

    std::set<std::string> seen;
    for (size_t i = 0; i < themes.size(); ++i) {
        const Theme::ThemeVisual& t = themes[i];
        EXPECT_EQ(t.id, expectedIds[i]);
        EXPECT_TRUE(seen.insert(t.id).second);
        EXPECT_EQ(t.source, Theme::Source::BuiltIn);
        EXPECT_FALSE(t.name.zh.empty());
        EXPECT_FALSE(t.name.en.empty());
        EXPECT_TRUE(t.capsule.slimAlpha >= 0.2f);
        EXPECT_TRUE(t.capsule.slimAlpha <= 1.0f);

        EXPECT_TRUE(Theme::ContrastRatio(t.colors.text, t.colors.paper) >= 7.0f);
        EXPECT_TRUE(Theme::ContrastRatio(t.colors.textWeak, t.colors.paper) >= 4.5f);
        EXPECT_TRUE(Theme::ContrastRatio(t.colors.danger, t.colors.paper) >= 4.5f);
        EXPECT_TRUE(Theme::ContrastRatio(t.colors.checkMark, t.colors.checkFill) >= 3.0f);
    }

    EXPECT_TRUE(Theme::FindBuiltIn("paper") != nullptr);
    EXPECT_TRUE(Theme::IsBuiltInId("graphite"));
    EXPECT_FALSE(Theme::IsBuiltInId("custom.graphite"));
    EXPECT_EQ(Theme::DefaultTheme().id, std::string("paper"));
}

void ResolveThemeHonorsBuiltinCustomFollowSystemAndFallback() {
    Theme::ThemeVisual custom = Theme::DefaultTheme();
    custom.id = "custom.ocean";
    custom.source = Theme::Source::Custom;
    custom.name = { L"海洋", L"Ocean" };
    custom.dark = true;
    std::vector<Theme::ThemeVisual> customThemes = { custom };

    Theme::ResolveInput input;
    input.customThemes = &customThemes;

    input.mode = "builtin";
    input.themeId = "mint";
    Theme::ResolveResult result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("mint"));
    EXPECT_FALSE(result.fellBack);

    input.mode = "custom";
    input.themeId = "custom.ocean";
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("custom.ocean"));
    EXPECT_EQ(result.theme.source, Theme::Source::Custom);
    EXPECT_FALSE(result.fellBack);

    input.mode = "follow_system";
    input.lightThemeId = "sky";
    input.darkThemeId = "graphite";
    input.systemDark = false;
    input.systemHighContrast = false;
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("sky"));

    input.systemDark = true;
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("graphite"));

    input.systemHighContrast = true;
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("contrast"));
    EXPECT_FALSE(result.fellBack);

    input.mode = "builtin";
    input.systemHighContrast = false;
    input.themeId = "missing";
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("paper"));
    EXPECT_TRUE(result.fellBack);
    EXPECT_EQ(result.message, std::wstring(L"missing"));

    input.mode = "custom";
    input.themeId = "custom.missing";
    result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("paper"));
    EXPECT_TRUE(result.fellBack);
}

void CustomThemesCannotShadowBuiltInIds() {
    Theme::ThemeVisual fakePaper = Theme::DefaultTheme();
    fakePaper.id = "paper";
    fakePaper.source = Theme::Source::Custom;
    fakePaper.name = { L"Fake", L"Fake" };
    fakePaper.colors.paper = 0x000000;
    std::vector<Theme::ThemeVisual> customThemes = { fakePaper };

    Theme::ResolveInput input;
    input.mode = "custom";
    input.themeId = "paper";
    input.customThemes = &customThemes;

    const Theme::ResolveResult result = Theme::ResolveTheme(input);
    EXPECT_EQ(result.theme.id, std::string("paper"));
    EXPECT_EQ(result.theme.source, Theme::Source::BuiltIn);
    EXPECT_FALSE(result.fellBack);
    EXPECT_TRUE(result.theme.colors.paper != 0x000000);
}

void NonWindowsSystemReadersFailClosed() {
    bool ok = true;
    const bool dark = Theme::SystemUsesDarkMode(&ok);
#ifndef _WIN32
    EXPECT_FALSE(dark);
    EXPECT_FALSE(ok);
#else
    (void)dark;
#endif

    ok = true;
    const bool highContrast = Theme::SystemHighContrastOn(&ok);
#ifndef _WIN32
    EXPECT_FALSE(highContrast);
    EXPECT_FALSE(ok);
#else
    (void)highContrast;
#endif
}

const TestCase kTests[] = {
    {"ColorHelpersConvertClampAndMeasureContrast", ColorHelpersConvertClampAndMeasureContrast},
    {"BuiltInCatalogIsStableUniqueAndAccessible", BuiltInCatalogIsStableUniqueAndAccessible},
    {"ResolveThemeHonorsBuiltinCustomFollowSystemAndFallback", ResolveThemeHonorsBuiltinCustomFollowSystemAndFallback},
    {"CustomThemesCannotShadowBuiltInIds", CustomThemesCannotShadowBuiltInIds},
    {"NonWindowsSystemReadersFailClosed", NonWindowsSystemReadersFailClosed},
};

} // namespace

int main() {
    return RunTests("theme", kTests);
}
