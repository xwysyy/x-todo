#pragma once

#include <string>

struct LaunchCommand {
    bool openReminder = false;
    bool reminderCheck = false;
    std::wstring day;
    int blockId = -1;
};

LaunchCommand ParseLaunchArgs(int argc, const wchar_t* const argv[]);
