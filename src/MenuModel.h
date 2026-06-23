#pragma once

#include "I18n.h"
#include "Theme.h"

#include <string>
#include <vector>

namespace GuiMenu {

using Command = unsigned int;

inline constexpr Command kCmdShow = 1;
inline constexpr Command kCmdExit = 3;
inline constexpr Command kCmdSettings = 4;
inline constexpr Command kCmdModeNormal = 10;
inline constexpr Command kCmdModeDesktop = 11;
inline constexpr Command kCmdStyleSlim = 30;
inline constexpr Command kCmdStyleDot = 31;
inline constexpr Command kCmdStyleBar = 32;
inline constexpr Command kCmdStylePip = 33;

inline constexpr Command kCmdThemeFollowSystem = 1000;
inline constexpr Command kCmdThemeBuiltinBase = 1100;
inline constexpr Command kCmdThemeCustomBase = 1300;
inline constexpr Command kCmdThemeManager = 1900;
inline constexpr Command kCmdListRename = 2000;
inline constexpr Command kCmdListDelete = 2001;
inline constexpr Command kCmdCalendarBlockDelete = 2100;

enum class MountMode {
    Normal,
    Desktop,
    Capsule,
};

enum class CapsuleStyle {
    Slim,
    Dot,
    Bar,
    Pip,
};

struct Item {
    Command cmd = 0;
    std::wstring text;
    bool separator = false;
    bool checked = false;
    bool danger = false;
    bool enabled = true;
    int indent = 0;
    bool header = false;
};

struct State {
    Lang lang = Lang::Zh;
    MountMode mountMode = MountMode::Normal;
    CapsuleStyle capsuleStyle = CapsuleStyle::Slim;
    std::string themeMode = "builtin";
    std::string currentThemeId = "paper";
    const std::vector<Theme::ThemeVisual>* customThemes = nullptr;
    int listCount = 1;
};

std::vector<Item> BuildTrayMenu(const State& state);
std::vector<Item> BuildTitleMenu(const State& state);
std::vector<Item> BuildThemeMenu(const State& state);
std::vector<Item> BuildListTabMenu(Lang lang, int listCount);
std::vector<Item> BuildCalendarBlockMenu(Lang lang);

const char* BuiltInThemeIdForCommand(Command cmd);

} // namespace GuiMenu
