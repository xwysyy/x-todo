#pragma once
#include "Theme.h"
#include <string>
#include <vector>

// 自定义主题加载：扫描 %APPDATA%\x-todo\themes\，严格 JSON schema 校验，收集 issue。
// 安全边界见 spec §8 / §17：主题文件是数据不是插件，不执行、不读外部路径、不加载资源。
namespace Theme {

struct LoadResult {
    std::vector<ThemeVisual> themes;
    std::vector<ThemeIssue>  issues;
};

// %APPDATA%\x-todo\themes\（以反斜杠结尾）；失败返回空串。
std::wstring ThemeDirectory();

// 扫描并校验自定义主题目录第一层。不创建目录。
LoadResult LoadCustomThemes();

// 把主题导出为可重新加载的 .xtheme 到 destDir。内置主题 id 改写为 custom.exported-*；
// 文件名经安全文件名函数生成；已存在同名文件时不覆盖（返回 false 并填 error）。
bool ExportTheme(const ThemeVisual& theme, const std::wstring& destDir, ThemeIssue* error);

} // namespace Theme
