#include "ThemeCatalog.h"

// 内置主题表。每套写死全部最终消费色（spec §7：不允许运行时从 paper 继承或在渲染处混 alpha）。
// 颜色经 WCAG 对比度脚本验证：text/paper >= 7、textWeak/paper >= 4.5、danger >= 4.5、checkMark/checkFill >= 3。

namespace Theme {

static ThemeVisual Make_paper() {
    ThemeVisual t;
    t.id = "paper";
    t.source = Source::BuiltIn;
    t.name = { L"暖纸", L"Paper" };
    t.dark = false;
    t.colors = {
        0xFBF7EC, // paper
        0xFFFAF0, // paperElevated
        0xDCD8CE, // paperEdge
        0x33312C, // text
        0x767060, // textWeak
        0x938C7C, // textDone
        0xE6E2D8, // divider
        0xF0ECE1, // rowHover
        0xF0ECE1, // menuHover
        0xECE8DE, // buttonHover
        0xDEDAD0, // buttonPressed
        0xA39B86, // disabledText
        0xB8B5AC, // checkBorder
        0x6E8A5D, // checkFill
        0x627A53, // checkFillHover
        0xFFFFFF, // checkMark
        0xC4C1B7, // handle
        0xA8A59C, // handleHover
        0xA84E38, // danger
        0x8E4330, // dangerHover
        0x6E8A5D, // focusRing
    };
    t.capsule = {
        0xFBF7EC, // slimPaper
        0xDCD8CE, // slimEdge
        0x33312C, // slimText
        0x6E8A5D, // dotActive
        0xC4C1B7, // dotIdle
        0xDCD8CE, // dotEdge
        0x627A53, // dotActiveHover
        0xA8A59C, // dotIdleHover
        0xD0CCC3, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xFFFAF0, // background
        0xDCD8CE, // edge
        0x6E8A5D, // mark
        0xA84E38, // badge
    };
    return t;
}

static ThemeVisual Make_mint() {
    ThemeVisual t;
    t.id = "mint";
    t.source = Source::BuiltIn;
    t.name = { L"薄荷", L"Mint" };
    t.dark = false;
    t.colors = {
        0xF0F5EF, // paper
        0xF7FAF6, // paperElevated
        0xD1D7D1, // paperEdge
        0x2B332B, // text
        0x66735F, // textWeak
        0x83907D, // textDone
        0xDBE1DA, // divider
        0xE5EAE4, // rowHover
        0xE5EAE4, // menuHover
        0xE1E6E0, // buttonHover
        0xD3D9D3, // buttonPressed
        0x939E8B, // disabledText
        0xAEB4AD, // checkBorder
        0x4F7A4A, // checkFill
        0x476C42, // checkFillHover
        0xFFFFFF, // checkMark
        0xBAC0B9, // handle
        0x9EA59E, // handleHover
        0xB1503E, // danger
        0x964435, // dangerHover
        0x4F7A4A, // focusRing
    };
    t.capsule = {
        0xF0F5EF, // slimPaper
        0xD1D7D1, // slimEdge
        0x2B332B, // slimText
        0x4F7A4A, // dotActive
        0xBAC0B9, // dotIdle
        0xD1D7D1, // dotEdge
        0x476C42, // dotActiveHover
        0x9EA59E, // dotIdleHover
        0xC6CBC5, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xF7FAF6, // background
        0xD1D7D1, // edge
        0x4F7A4A, // mark
        0xB1503E, // badge
    };
    return t;
}

static ThemeVisual Make_sky() {
    ThemeVisual t;
    t.id = "sky";
    t.source = Source::BuiltIn;
    t.name = { L"天空", L"Sky" };
    t.dark = false;
    t.colors = {
        0xEFF3F7, // paper
        0xF6F9FC, // paperElevated
        0xD0D5D9, // paperEdge
        0x2A2F36, // text
        0x63707C, // textWeak
        0x828D9A, // textDone
        0xDADEE3, // divider
        0xE4E8EC, // rowHover
        0xE4E8EC, // menuHover
        0xE0E4E9, // buttonHover
        0xD2D7DB, // buttonPressed
        0x8E99A4, // disabledText
        0xADB1B6, // checkBorder
        0x3F6B94, // checkFill
        0x385F83, // checkFillHover
        0xFFFFFF, // checkMark
        0xB9BDC2, // handle
        0x9DA2A7, // handleHover
        0xA94E42, // danger
        0x8F4338, // dangerHover
        0x3F6B94, // focusRing
    };
    t.capsule = {
        0xEFF3F7, // slimPaper
        0xD0D5D9, // slimEdge
        0x2A2F36, // slimText
        0x3F6B94, // dotActive
        0xB9BDC2, // dotIdle
        0xD0D5D9, // dotEdge
        0x385F83, // dotActiveHover
        0x9DA2A7, // dotIdleHover
        0xC5C9CE, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xF6F9FC, // background
        0xD0D5D9, // edge
        0x3F6B94, // mark
        0xA94E42, // badge
    };
    return t;
}

static ThemeVisual Make_rose() {
    ThemeVisual t;
    t.id = "rose";
    t.source = Source::BuiltIn;
    t.name = { L"玫瑰", L"Rose" };
    t.dark = false;
    t.colors = {
        0xF9F0F2, // paper
        0xFDF7F8, // paperElevated
        0xDBD1D4, // paperEdge
        0x352B2E, // text
        0x7A6469, // textWeak
        0x9A858A, // textDone
        0xE4DBDD, // divider
        0xEEE5E7, // rowHover
        0xEEE5E7, // menuHover
        0xEAE1E3, // buttonHover
        0xDDD3D6, // buttonPressed
        0xAC969B, // disabledText
        0xB7AEB0, // checkBorder
        0x9C4A68, // checkFill
        0x8A425D, // checkFillHover
        0xFFFFFF, // checkMark
        0xC3BABC, // handle
        0xA89EA1, // handleHover
        0xB54A52, // danger
        0x993F46, // dangerHover
        0x9C4A68, // focusRing
    };
    t.capsule = {
        0xF9F0F2, // slimPaper
        0xDBD1D4, // slimEdge
        0x352B2E, // slimText
        0x9C4A68, // dotActive
        0xC3BABC, // dotIdle
        0xDBD1D4, // dotEdge
        0x8A425D, // dotActiveHover
        0xA89EA1, // dotIdleHover
        0xCFC6C8, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xFDF7F8, // background
        0xDBD1D4, // edge
        0x9C4A68, // mark
        0xB54A52, // badge
    };
    return t;
}

static ThemeVisual Make_sand() {
    ThemeVisual t;
    t.id = "sand";
    t.source = Source::BuiltIn;
    t.name = { L"沙色", L"Sand" };
    t.dark = false;
    t.colors = {
        0xF2EFEA, // paper
        0xF8F6F2, // paperElevated
        0xD4D1CC, // paperEdge
        0x302E2A, // text
        0x6E6757, // textWeak
        0x8F8878, // textDone
        0xDEDBD6, // divider
        0xE7E4DF, // rowHover
        0xE7E4DF, // menuHover
        0xE3E1DC, // buttonHover
        0xD6D3CE, // buttonPressed
        0xA39C8B, // disabledText
        0xB1AEAA, // checkBorder
        0x7C6B3F, // checkFill
        0x6E5F38, // checkFillHover
        0xFFFFFF, // checkMark
        0xBDBAB5, // handle
        0xA29F9A, // handleHover
        0x9F4E32, // danger
        0x87432B, // dangerHover
        0x7C6B3F, // focusRing
    };
    t.capsule = {
        0xF2EFEA, // slimPaper
        0xD4D1CC, // slimEdge
        0x302E2A, // slimText
        0x7C6B3F, // dotActive
        0xBDBAB5, // dotIdle
        0xD4D1CC, // dotEdge
        0x6E5F38, // dotActiveHover
        0xA29F9A, // dotIdleHover
        0xC8C6C1, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xF8F6F2, // background
        0xD4D1CC, // edge
        0x7C6B3F, // mark
        0x9F4E32, // badge
    };
    return t;
}

static ThemeVisual Make_graphite() {
    ThemeVisual t;
    t.id = "graphite";
    t.source = Source::BuiltIn;
    t.name = { L"石墨", L"Graphite" };
    t.dark = true;
    t.colors = {
        0x23262B, // paper
        0x2C3037, // paperElevated
        0x4B4D51, // paperEdge
        0xE8EAED, // text
        0xA2A8B2, // textWeak
        0x787E88, // textDone
        0x3D4044, // divider
        0x32353A, // rowHover
        0x32353A, // menuHover
        0x3B3E42, // buttonHover
        0x4D4F53, // buttonPressed
        0x6A6F78, // disabledText
        0x65676B, // checkBorder
        0x6E9E72, // checkFill
        0x7FAA83, // checkFillHover
        0x16181B, // checkMark
        0x65676B, // handle
        0x7F8184, // handleHover
        0xE08068, // danger
        0xE4927D, // dangerHover
        0x6E9E72, // focusRing
    };
    t.capsule = {
        0x23262B, // slimPaper
        0x4B4D51, // slimEdge
        0xE8EAED, // slimText
        0x6E9E72, // dotActive
        0x65676B, // dotIdle
        0x4B4D51, // dotEdge
        0x7FAA83, // dotActiveHover
        0x7F8184, // dotIdleHover
        0x585A5E, // dotEdgeHover
        0.72f, // slimAlpha
    };
    t.tray = {
        0x2C3037, // background
        0x4B4D51, // edge
        0x6E9E72, // mark
        0xE08068, // badge
    };
    return t;
}

static ThemeVisual Make_ink() {
    ThemeVisual t;
    t.id = "ink";
    t.source = Source::BuiltIn;
    t.name = { L"墨色", L"Ink" };
    t.dark = true;
    t.colors = {
        0x000000, // paper
        0x0A0A0A, // paperElevated
        0x4A4A4A, // paperEdge
        0xFFFFFF, // text
        0xC8C8C8, // textWeak
        0x8C8C8C, // textDone
        0x3A3A3A, // divider
        0x121212, // rowHover
        0x121212, // menuHover
        0x1C1C1C, // buttonHover
        0x303030, // buttonPressed
        0x767676, // disabledText
        0x8C8C8C, // checkBorder
        0x3FB950, // checkFill
        0x56C165, // checkFillHover
        0x000000, // checkMark
        0x4D4D4D, // handle
        0x6B6B6B, // handleHover
        0xFF6A4D, // danger
        0xFF7F66, // dangerHover
        0x4FC9FF, // focusRing
    };
    t.capsule = {
        0x000000, // slimPaper
        0x4A4A4A, // slimEdge
        0xFFFFFF, // slimText
        0x3FB950, // dotActive
        0x4D4D4D, // dotIdle
        0x4A4A4A, // dotEdge
        0x56C165, // dotActiveHover
        0x6B6B6B, // dotIdleHover
        0x3D3D3D, // dotEdgeHover
        0.72f, // slimAlpha
    };
    t.tray = {
        0x0A0A0A, // background
        0x4A4A4A, // edge
        0x3FB950, // mark
        0xFF6A4D, // badge
    };
    return t;
}

static ThemeVisual Make_contrast() {
    ThemeVisual t;
    t.id = "contrast";
    t.source = Source::BuiltIn;
    t.name = { L"高对比", L"High Contrast" };
    t.dark = false;
    t.colors = {
        0xFFFFFF, // paper
        0xFFFFFF, // paperElevated
        0x000000, // paperEdge
        0x000000, // text
        0x3A3A3A, // textWeak
        0x565656, // textDone
        0x000000, // divider
        0xF1F1F1, // rowHover
        0xF1F1F1, // menuHover
        0xECECEC, // buttonHover
        0xDADADA, // buttonPressed
        0x6A6A6A, // disabledText
        0x000000, // checkBorder
        0x006400, // checkFill
        0x005900, // checkFillHover
        0xFFFFFF, // checkMark
        0xB9B9B9, // handle
        0x959595, // handleHover
        0xB00000, // danger
        0x950000, // dangerHover
        0x0000EE, // focusRing
    };
    t.capsule = {
        0xFFFFFF, // slimPaper
        0x000000, // slimEdge
        0x000000, // slimText
        0x006400, // dotActive
        0xB9B9B9, // dotIdle
        0x000000, // dotEdge
        0x005900, // dotActiveHover
        0x959595, // dotIdleHover
        0xC8C8C8, // dotEdgeHover
        0.60f, // slimAlpha
    };
    t.tray = {
        0xFFFFFF, // background
        0x000000, // edge
        0x006400, // mark
        0xB00000, // badge
    };
    return t;
}

const std::vector<ThemeVisual>& BuiltInThemes() {
    static const std::vector<ThemeVisual> kThemes = {
        Make_paper(), Make_mint(), Make_sky(), Make_rose(),
        Make_sand(), Make_graphite(), Make_ink(), Make_contrast(),
    };
    return kThemes;
}

const ThemeVisual* FindBuiltIn(const std::string& id) {
    for (const auto& t : BuiltInThemes())
        if (t.id == id) return &t;
    return nullptr;
}

bool IsBuiltInId(const std::string& id) {
    return FindBuiltIn(id) != nullptr;
}

const ThemeVisual& DefaultTheme() {
    const ThemeVisual* p = FindBuiltIn("paper"); // paper 必然存在
    return p ? *p : BuiltInThemes().front();
}

} // namespace Theme
