#include "ReminderSchedulerPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void DisabledFallbackDeletesTaskWhenSchedulerStateIsUnknown() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = false;
    settings.systemNotification = true;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, true, ReminderSchedulerState{false, false});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::DeleteTask);
    EXPECT_TRUE(decision.status.empty());
}

void DisabledFallbackDoesNothingAfterSchedulerHasNoTask() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = false;
    settings.systemNotification = true;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, true, ReminderSchedulerState{true, false});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::Noop);
    EXPECT_TRUE(decision.status.empty());
}

void FallbackRequiresWindowsNotification() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = true;
    settings.systemNotification = false;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, true, ReminderSchedulerState{true, false});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::Noop);
    EXPECT_FALSE(decision.status.empty());
}

void FallbackWithoutWindowsNotificationDeletesKnownTask() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = true;
    settings.systemNotification = false;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, true, ReminderSchedulerState{true, true});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::DeleteTask);
    EXPECT_FALSE(decision.status.empty());
}

void EnabledFallbackWithNextDueRegistersTask() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = true;
    settings.systemNotification = true;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, true, ReminderSchedulerState{true, false});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::RegisterTask);
    EXPECT_TRUE(decision.status.empty());
}

void EnabledFallbackWithoutNextDueDeletesKnownTask() {
    ReminderSettings settings;
    settings.enabled = true;
    settings.taskSchedulerFallback = true;
    settings.systemNotification = true;

    const ReminderSchedulerDecision decision =
        DecideReminderScheduler(settings, false, ReminderSchedulerState{true, true});

    EXPECT_TRUE(decision.action == ReminderSchedulerAction::DeleteTask);
    EXPECT_TRUE(decision.status.empty());
}

const TestCase kTests[] = {
    {"DisabledFallbackDeletesTaskWhenSchedulerStateIsUnknown", DisabledFallbackDeletesTaskWhenSchedulerStateIsUnknown},
    {"DisabledFallbackDoesNothingAfterSchedulerHasNoTask", DisabledFallbackDoesNothingAfterSchedulerHasNoTask},
    {"FallbackRequiresWindowsNotification", FallbackRequiresWindowsNotification},
    {"FallbackWithoutWindowsNotificationDeletesKnownTask", FallbackWithoutWindowsNotificationDeletesKnownTask},
    {"EnabledFallbackWithNextDueRegistersTask", EnabledFallbackWithNextDueRegistersTask},
    {"EnabledFallbackWithoutNextDueDeletesKnownTask", EnabledFallbackWithoutNextDueDeletesKnownTask},
};

} // namespace

int main() {
    return RunTests("reminder_scheduler_policy", kTests);
}
