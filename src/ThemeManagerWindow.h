#pragma once
#include "Theme.h"
#include "I18n.h"
#include <windows.h>
#include <functional>
#include <string>
#include <vector>

// 独立自绘主题管理窗口（类名 XTodoThemeManagerWindow）。
// 不复用线性 popup 菜单：列表 + 错误 + 按钮需要专门布局。
namespace Theme {

// 与宿主（MainWindow）交互：数据快照 + 操作回调。回调内宿主自行更新状态，
// 并刷新本结构（窗口随后重建行模型并重绘，实现“打开期间实时反映主题变化”）。
struct ManagerHost {
    std::string  currentMode;          // builtin | custom | follow_system
    std::string  currentId;            // 当前解析出的主题 id
    std::string  lightThemeId;
    std::string  darkThemeId;
    ThemeVisual  current;              // 当前主题（管理窗口自身渲染用）
    std::vector<ThemeVisual> builtins; // 内置主题
    std::vector<ThemeVisual> customs;  // 自定义主题
    std::vector<ThemeIssue>  issues;   // 加载 issue
    std::vector<ThemeNotice> notices;  // 运行时 notice
    Lang lang = Lang::Zh;

    std::function<void(const std::string& id)> applyTheme;      // 点击主题：应用并保存
    std::function<void()>                      reload;          // 重新加载目录
    std::function<void()>                      openFolder;      // 打开主题目录
    std::function<void()>                      exportCurrent;   // 导出当前主题
    std::function<void(const std::string& id)> setLightFollow; // 设为浅色跟随
    std::function<void(const std::string& id)> setDarkFollow;  // 设为深色跟随
};

// 模态打开主题管理窗口；host 引用需在调用期间保持有效。
void ShowThemeManagerWindow(HWND owner, ManagerHost& host);

} // namespace Theme
