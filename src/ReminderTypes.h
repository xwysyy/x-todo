#pragma once

#include <string>
#include <vector>

enum class ReminderKind {
    BeforeStart5,
    Halfway,
};

struct ReminderSettings {
    bool enabled = true;
    bool beforeStart5 = true;
    bool halfway = true;
    bool inAppPopup = true;
    bool capsulePulse = true;
    bool systemNotification = false;
    bool taskSchedulerFallback = false;
    bool catchUpAfterResume = true;
    int catchUpGraceMinutes = 10;
};

struct ReminderMinute {
    std::string day;
    int minute = 0;
};

struct ReminderCandidate {
    std::string key;
    int blockId = 0;
    ReminderKind kind = ReminderKind::BeforeStart5;
    std::string day;
    int startMinute = 0;
    int endMinute = 0;
    ReminderMinute due;
    std::wstring title;
};

struct ReminderLogEntry {
    std::string key;
    long long firedAt = 0;
};
