#pragma once

#include "ReminderTypes.h"

#include <string>

enum class ReminderSchedulerAction {
    Noop,
    DeleteTask,
    RegisterTask,
};

struct ReminderSchedulerState {
    bool synced = false;
    bool registered = false;
};

struct ReminderSchedulerDecision {
    ReminderSchedulerAction action = ReminderSchedulerAction::Noop;
    std::wstring status;
};

inline ReminderSchedulerDecision DecideReminderScheduler(const ReminderSettings& settings,
                                                         bool hasNextDue,
                                                         ReminderSchedulerState state) {
    ReminderSchedulerDecision decision;
    auto deleteIfNeeded = [&]() {
        if (!state.synced || state.registered)
            decision.action = ReminderSchedulerAction::DeleteTask;
    };

    if (!settings.enabled || !settings.taskSchedulerFallback) {
        deleteIfNeeded();
        return decision;
    }

    if (!settings.systemNotification) {
        decision.status = L"Task Scheduler fallback needs Windows notification";
        deleteIfNeeded();
        return decision;
    }

    if (!hasNextDue) {
        deleteIfNeeded();
        return decision;
    }

    decision.action = ReminderSchedulerAction::RegisterTask;
    return decision;
}
