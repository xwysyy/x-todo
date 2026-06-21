#pragma once
#include <string>
#include "CalendarModel.h"
#include "TodoModel.h"

struct WindowGeometry {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

struct UiState {
    bool completedExpanded = false;
    bool alwaysOnTop = true;
    std::string mountMode = "normal"; // normal | desktop | capsule
    std::string lang = "";            // "" 跟随系统；否则 zh | en
    // 主题：模式 + 当前选择 + 跟随系统时的浅 / 深选择
    std::string themeMode    = "builtin";  // builtin | custom | follow_system
    std::string themeId      = "paper";    // builtin / custom 的当前选择
    std::string lightThemeId = "paper";    // follow_system 浅色
    std::string darkThemeId  = "graphite"; // follow_system 深色
    // 胶囊形态：外观样式 + 吸附边 + 沿边比例 + 所在显示器
    std::string capsuleStyle    = "slim";  // slim | dot
    std::string capsuleDockEdge = "right"; // left | right
    double      capsuleDockT    = 0.5;     // 0..1，沿吸附边的归一化纵向位置
    std::string capsuleMonitor  = "";      // 显示器 szDevice（UTF-8），丢失时回退就近
    std::string activeView      = "list";  // list | calendar
    std::string calendarDay     = "";      // YYYY-MM-DD；空表示启动时使用本地日期
};

// 加载结果：区分"文件不存在"（可安全空启动）与"存在但读失败"（须防止覆盖丢失）。
enum class LoadResult { Missing, Loaded, Failed };

// 本地持久化：%APPDATA%\x-todo\data.txt（UTF-8，原子写）。
namespace Store {
    std::wstring DataFilePath();
    // Missing=无文件；Loaded=成功；Failed=文件存在但读取失败（已自动备份为 .corrupt.bak）。
    LoadResult Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui);
    // 原子写（临时文件 + 替换），成功返回 true。
    bool Save(const TodoModel& model, const CalendarModel& calendar,
              const WindowGeometry& geom, const UiState& ui);
}
