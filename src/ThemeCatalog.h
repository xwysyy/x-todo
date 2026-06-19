#pragma once
#include "Theme.h"
#include <string>
#include <vector>

// 内置主题表。进程内常量数据，永远可用，不被自定义主题覆盖。
namespace Theme {

// 全部内置主题（首个为 paper）。
const std::vector<ThemeVisual>& BuiltInThemes();
// 按 id 查内置主题，未命中返回 nullptr。
const ThemeVisual* FindBuiltIn(const std::string& id);
// id 是否命中内置主题。
bool IsBuiltInId(const std::string& id);
// 默认主题 paper（catalog 保证存在），作为一切 fallback 的终点。
const ThemeVisual& DefaultTheme();

} // namespace Theme
