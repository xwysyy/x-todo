#pragma once

#include "ReminderTypes.h"

enum class ReminderSettingAction {
    Enable,
    BeforeStart5,
    Halfway,
    InAppPopup,
    CapsulePulse,
    SystemNotification,
    TaskSchedulerFallback,
    CatchUp,
};

inline bool ApplyReminderSettingAction(ReminderSettings& settings,
                                       ReminderSettingAction action) {
    switch (action) {
        case ReminderSettingAction::Enable:
            settings.enabled = !settings.enabled;
            return true;
        case ReminderSettingAction::BeforeStart5:
            settings.beforeStart5 = !settings.beforeStart5;
            return true;
        case ReminderSettingAction::Halfway:
            settings.halfway = !settings.halfway;
            return true;
        case ReminderSettingAction::InAppPopup:
            settings.inAppPopup = !settings.inAppPopup;
            return true;
        case ReminderSettingAction::CapsulePulse:
            settings.capsulePulse = !settings.capsulePulse;
            return true;
        case ReminderSettingAction::SystemNotification:
            settings.systemNotification = !settings.systemNotification;
            return true;
        case ReminderSettingAction::TaskSchedulerFallback:
            settings.taskSchedulerFallback = !settings.taskSchedulerFallback;
            return true;
        case ReminderSettingAction::CatchUp:
            settings.catchUpAfterResume = !settings.catchUpAfterResume;
            return true;
    }
    return false;
}
