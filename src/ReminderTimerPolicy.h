#pragma once

#include "ReminderTypes.h"

inline unsigned int ComputeReminderTimerDelayMs(ReminderMinute now, bool hasNextDue,
                                                ReminderMinute nextDue,
                                                unsigned int maxDelayMs) {
    if (maxDelayMs == 0) return 0;
    if (!hasNextDue) return maxDelayMs;

    constexpr unsigned int kImmediateDelayMs = 1000;
    const unsigned int immediate = kImmediateDelayMs < maxDelayMs ? kImmediateDelayMs : maxDelayMs;

    if (nextDue.day < now.day) return immediate;
    if (nextDue.day > now.day) return maxDelayMs;

    const int minutes = nextDue.minute - now.minute;
    if (minutes <= 0) return immediate;

    const unsigned long long delayMs = static_cast<unsigned long long>(minutes) * 60ULL * 1000ULL;
    return delayMs < maxDelayMs ? static_cast<unsigned int>(delayMs) : maxDelayMs;
}
