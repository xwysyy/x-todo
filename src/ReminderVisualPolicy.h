#pragma once

inline bool CanStartCapsuleReminderPulse(bool hasDueReminder, bool windowVisible) {
    return hasDueReminder && windowVisible;
}
