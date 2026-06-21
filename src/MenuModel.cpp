#include "MenuModel.h"

#include <algorithm>

namespace GuiMenu {
namespace {

Item Separator() {
    Item item;
    item.separator = true;
    return item;
}

void AppendModeAndAppItems(std::vector<Item>& items, const State& state, bool includeShow) {
    const bool inCapsule = state.mountMode == MountMode::Capsule;
    if (includeShow) {
        items.push_back(Item{ kCmdShow, T(Str::Show, state.lang) });
        items.push_back(Separator());
    }
    items.push_back(Item{ kCmdModeNormal, T(Str::ModeNormal, state.lang), false,
                          state.mountMode == MountMode::Normal });
    items.push_back(Item{ kCmdModeDesktop, T(Str::ModeDesktop, state.lang), false,
                          state.mountMode == MountMode::Desktop });
    items.push_back(Item{ 0, T(Str::ModeCapsule, state.lang), false, false, false, false });
    items.push_back(Item{ kCmdStyleSlim, T(Str::StyleSlim, state.lang), false,
                          inCapsule && state.capsuleStyle == CapsuleStyle::Slim, false, true, 1 });
    items.push_back(Item{ kCmdStyleDot, T(Str::StyleDot, state.lang), false,
                          inCapsule && state.capsuleStyle == CapsuleStyle::Dot, false, true, 1 });
    items.push_back(Separator());
    items.push_back(Item{ kCmdToggleLang, T(Str::ToggleLang, state.lang) });
    items.push_back(Item{ kCmdAutostart, T(Str::Autostart, state.lang), false, state.autostart });
    items.push_back(Separator());
    items.push_back(Item{ kCmdExit, T(Str::Exit, state.lang), false, false, true });
}

struct BuiltInThemeItem {
    const char* id;
    Str name;
};

constexpr BuiltInThemeItem kBuiltInThemes[] = {
    { "paper", Str::ThemePaper },
    { "mint", Str::ThemeMint },
    { "sky", Str::ThemeSky },
    { "rose", Str::ThemeRose },
    { "sand", Str::ThemeSand },
    { "graphite", Str::ThemeGraphite },
    { "ink", Str::ThemeInk },
    { "contrast", Str::ThemeContrast },
};

} // namespace

std::vector<Item> BuildTrayMenu(const State& state) {
    std::vector<Item> items;
    AppendModeAndAppItems(items, state, true);
    return items;
}

std::vector<Item> BuildTitleMenu(const State& state) {
    std::vector<Item> items;
    AppendModeAndAppItems(items, state, false);
    return items;
}

std::vector<Item> BuildThemeMenu(const State& state) {
    std::vector<Item> items;
    items.push_back(Item{ 0, T(Str::ThemeHeader, state.lang), false, false, false, true, 0, true });
    items.push_back(Item{ kCmdThemeFollowSystem, T(Str::ThemeFollowSystem, state.lang), false,
                          state.themeMode == "follow_system", false, true, 1 });

    for (unsigned int i = 0; i < 8; ++i) {
        const bool current = state.themeMode != "follow_system" &&
                             state.currentThemeId == kBuiltInThemes[i].id;
        items.push_back(Item{ kCmdThemeBuiltinBase + i, T(kBuiltInThemes[i].name, state.lang),
                              false, current, false, true, 1 });
    }

    const std::vector<Theme::ThemeVisual>* customs = state.customThemes;
    const unsigned int shown = customs ? std::min<unsigned int>(static_cast<unsigned int>(customs->size()), 8U) : 0U;
    for (unsigned int i = 0; i < shown; ++i) {
        const Theme::ThemeVisual& theme = (*customs)[i];
        const bool current = state.themeMode == "custom" && state.currentThemeId == theme.id;
        const std::wstring name = state.lang == Lang::Zh ? theme.name.zh : theme.name.en;
        items.push_back(Item{ kCmdThemeCustomBase + i, name, false, current, false, true, 1 });
    }

    items.push_back(Item{ kCmdThemeManager, T(Str::ThemeCustom, state.lang), false, false, false, true, 1 });
    return items;
}

std::vector<Item> BuildListTabMenu(Lang lang, int listCount) {
    return {
        Item{ kCmdListRename, T(Str::ListRename, lang) },
        Separator(),
        Item{ kCmdListDelete, T(Str::ListDelete, lang), false, false, true, listCount > 1 },
    };
}

const char* BuiltInThemeIdForCommand(Command cmd) {
    if (cmd < kCmdThemeBuiltinBase || cmd >= kCmdThemeBuiltinBase + 8) return nullptr;
    return kBuiltInThemes[cmd - kCmdThemeBuiltinBase].id;
}

} // namespace GuiMenu
