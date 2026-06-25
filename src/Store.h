#pragma once
#include <string>
#include <vector>
#include "CalendarModel.h"
#include "ReminderTypes.h"
#include "TodoModel.h"

struct WindowGeometry {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

enum class CalendarViewMode {
    Day,
    Week,
    Month,
};

struct UiState {
    bool alwaysOnTop = true;
    std::string mountMode = "normal"; // normal | desktop | capsule
    std::string lang = "";            // "" 跟随系统；否则 zh | en
    // 主题：模式 + 当前选择 + 跟随系统时的浅 / 深选择
    std::string themeMode    = "builtin";  // builtin | custom | follow_system
    std::string themeId      = "paper";    // builtin / custom 的当前选择
    std::string lightThemeId = "paper";    // follow_system 浅色
    std::string darkThemeId  = "paper";    // follow_system 深色（已无深色内置主题）
    // 胶囊形态：外观样式 + 吸附边 + 沿边比例 + 所在显示器
    std::string capsuleStyle    = "slim";  // slim(魔方) | dot(精灵球) | bar(细边) | pip(圆点)
    std::string capsuleDockEdge = "right"; // left | right
    double      capsuleDockT    = 0.5;     // 0..1，沿吸附边的归一化纵向位置
    std::string capsuleMonitor  = "";      // 显示器 szDevice（UTF-8），丢失时回退就近
    std::string activeView      = "list";  // list | calendar
    std::string calendarDay     = "";      // YYYY-MM-DD；空表示启动时使用本地日期
    CalendarViewMode calendarView = CalendarViewMode::Day; // 日历视图：day | week | month
    ReminderSettings reminders;             // 日历块全局提醒设置
    std::wstring backupDir;                // 自动备份目录；空表示关闭
    long long backupLastEpoch = 0;         // UTC epoch seconds；0 表示尚未成功备份
};

// 加载结果：区分"文件不存在"（可安全空启动）与"存在但读失败"（须防止覆盖丢失）。
enum class LoadResult { Missing, Loaded, Failed };

// 本地持久化：%APPDATA%\x-todo\data.json（UTF-8 JSON，原子写）。
namespace Store {
    enum class BackupResult {
        Succeeded,
        InvalidDirectory,
        SameAsDataFile,
        ReadFailed,
        WriteFailed,
    };

    std::wstring DataDirectoryPath();
    std::wstring DataFilePath();
    std::wstring BackupTargetPath(const std::wstring& backupDir);
    // Missing=无文件；Loaded=成功；Failed=文件存在但读取失败（已自动备份为 .corrupt.bak）。
    LoadResult Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui);
    LoadResult Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui,
                    std::vector<ReminderLogEntry>& reminderLog);
    // 原子写（临时文件 + 替换），成功返回 true。
    bool Save(const TodoModel& model, const CalendarModel& calendar,
              const WindowGeometry& geom, const UiState& ui);
    bool Save(const TodoModel& model, const CalendarModel& calendar,
              const WindowGeometry& geom, const UiState& ui,
              const std::vector<ReminderLogEntry>& reminderLog);
    BackupResult BackupDataFileTo(const std::wstring& backupDir);
}
