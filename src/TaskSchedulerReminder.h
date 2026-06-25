#pragma once

#include "ReminderTypes.h"

#include <string>

namespace TaskSchedulerReminder {

inline constexpr wchar_t kTaskName[] = L"X-TODO Reminder Check";

std::wstring ReminderCheckArguments();
std::wstring FormatStartBoundaryIsoLocal(const ReminderMinute& minute);
bool RegisterReminderCheckTask(const std::wstring& exePath,
                               const std::wstring& startBoundaryIsoLocal,
                               std::wstring* error);
bool DeleteReminderCheckTask(std::wstring* error);

} // namespace TaskSchedulerReminder
