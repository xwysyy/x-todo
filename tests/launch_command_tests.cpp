#include "LaunchCommand.h"
#include "TaskSchedulerReminder.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void ToastActivationArgsOpenCalendarTarget() {
    const wchar_t* argv[] = {
        L"x-todo.exe",
        L"--open-calendar",
        L"--day",
        L"2026-06-25",
        L"--block",
        L"42",
    };

    const LaunchCommand command = ParseLaunchArgs(6, argv);

    EXPECT_TRUE(command.openReminder);
    EXPECT_EQ(command.day, std::wstring(L"2026-06-25"));
    EXPECT_EQ(command.blockId, 42);
}

void OpenCalendarRequiresPositiveBlockId() {
    const wchar_t* argv[] = {
        L"x-todo.exe",
        L"--open-calendar",
        L"--day",
        L"2026-06-25",
        L"--block",
        L"0",
    };

    const LaunchCommand command = ParseLaunchArgs(6, argv);

    EXPECT_FALSE(command.openReminder);
    EXPECT_EQ(command.blockId, -1);
}

void OpenCalendarRequiresValidDay() {
    const wchar_t* argv[] = {
        L"x-todo.exe",
        L"--open-calendar",
        L"--day",
        L"2026-02-30",
        L"--block",
        L"42",
    };

    const LaunchCommand command = ParseLaunchArgs(6, argv);

    EXPECT_FALSE(command.openReminder);
    EXPECT_EQ(command.blockId, -1);
}

void ReminderCheckCommandIsIndependent() {
    const std::wstring reminderCheck = TaskSchedulerReminder::ReminderCheckArguments();
    const wchar_t* argv[] = {
        L"x-todo.exe",
        reminderCheck.c_str(),
    };

    const LaunchCommand command = ParseLaunchArgs(2, argv);

    EXPECT_TRUE(command.reminderCheck);
    EXPECT_FALSE(command.openReminder);
}

const TestCase kTests[] = {
    {"ToastActivationArgsOpenCalendarTarget", ToastActivationArgsOpenCalendarTarget},
    {"OpenCalendarRequiresPositiveBlockId", OpenCalendarRequiresPositiveBlockId},
    {"OpenCalendarRequiresValidDay", OpenCalendarRequiresValidDay},
    {"ReminderCheckCommandIsIndependent", ReminderCheckCommandIsIndependent},
};

} // namespace

int main() {
    return RunTests("launch_command", kTests);
}
