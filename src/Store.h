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
    std::string mountMode = "normal"; // normal | desktop | capsule
    std::string lang = "";            // "" 跟随系统；否则 zh | en
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
