#pragma once
#include <string>
#include "TodoModel.h"

struct WindowGeometry {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

struct UiState {
    bool completedExpanded = false;
    bool alwaysOnTop = true;
    std::string mountMode = "normal"; // normal | desktop | capsule | taskbar
    std::string lang = "";            // "" 跟随系统；否则 zh | en
    // 胶囊形态：外观样式 + 吸附边 + 沿边比例 + 所在显示器
    std::string capsuleStyle    = "slim";  // slim | dot
    std::string capsuleDockEdge = "right"; // left | right
    double      capsuleDockT    = 0.5;     // 0..1，沿吸附边的归一化纵向位置
    std::string capsuleMonitor  = "";      // 显示器 szDevice（UTF-8），丢失时回退就近
    // 任务栏嵌入状态条
    std::string taskbarMonitor  = "";      // 任务栏所在显示器 szDevice（UTF-8）
    double      taskbarDockT    = 0.18;    // 沿任务栏长边的归一化位置 0..1
    int         taskbarWidth    = 320;     // 水平状态条宽度，逻辑像素
    std::string taskbarStrategy = "popup_shell_noowner";
};

// 加载结果：区分"文件不存在"（可安全空启动）与"存在但读失败"（须防止覆盖丢失）。
enum class LoadResult { Missing, Loaded, Failed };

// 本地持久化：%APPDATA%\x-todo\data.txt（UTF-8，原子写）。
namespace Store {
    std::wstring DataFilePath();
    // Missing=无文件；Loaded=成功；Failed=文件存在但读取失败（已自动备份为 .corrupt.bak）。
    LoadResult Load(TodoModel& model, WindowGeometry& geom, UiState& ui);
    // 原子写（临时文件 + 替换），成功返回 true。
    bool Save(const TodoModel& model, const WindowGeometry& geom, const UiState& ui);
}
