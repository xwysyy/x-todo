#include "LaunchCommand.h"

#include "CalendarDate.h"

#include <cwchar>
#include <cstdlib>
#include <string>

namespace {

int ParsePositiveInt(const wchar_t* text) {
    if (!text || !*text) return -1;
    wchar_t* end = nullptr;
    const long value = std::wcstol(text, &end, 10);
    if (!end || *end != L'\0' || value <= 0 || value > 2147483647L) return -1;
    return static_cast<int>(value);
}

std::string NarrowAscii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch > 127) return std::string();
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

bool IsValidDay(const std::wstring& day) {
    const std::string narrow = NarrowAscii(day);
    CalendarDate::Date parsed{};
    return CalendarDate::Parse(narrow, parsed);
}

} // namespace

LaunchCommand ParseLaunchArgs(int argc, const wchar_t* const argv[]) {
    LaunchCommand command;
    bool openRequested = false;

    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--reminder-check") == 0) {
            command.reminderCheck = true;
        } else if (std::wcscmp(argv[i], L"--open-calendar") == 0) {
            openRequested = true;
        } else if (std::wcscmp(argv[i], L"--day") == 0 && i + 1 < argc) {
            command.day = argv[++i];
        } else if (std::wcscmp(argv[i], L"--block") == 0 && i + 1 < argc) {
            command.blockId = ParsePositiveInt(argv[++i]);
        }
    }

    command.openReminder = openRequested && command.blockId > 0 && IsValidDay(command.day);
    if (!command.openReminder) command.blockId = -1;
    return command;
}
