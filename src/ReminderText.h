#pragma once

#include "I18n.h"
#include "ReminderTypes.h"

#include <cstddef>
#include <string>
#include <vector>

struct ReminderDisplayText {
    std::wstring title;
    std::vector<std::wstring> lines;
};

ReminderDisplayText FormatReminderText(const std::vector<ReminderCandidate>& reminders,
                                       Lang lang);
std::wstring JoinReminderLines(const ReminderDisplayText& text, size_t maxChars = 0);
