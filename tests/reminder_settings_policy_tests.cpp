#include "ReminderSettingsPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void EveryReminderActionTogglesOnlyItsSetting() {
    ReminderSettings settings;
    ReminderSettings original = settings;

    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::Enable));
    EXPECT_FALSE(settings.enabled);
    EXPECT_TRUE(settings.beforeStart5 == original.beforeStart5);
    EXPECT_TRUE(settings.halfway == original.halfway);
    EXPECT_TRUE(settings.inAppPopup == original.inAppPopup);
    EXPECT_TRUE(settings.capsulePulse == original.capsulePulse);
    EXPECT_TRUE(settings.systemNotification == original.systemNotification);
    EXPECT_TRUE(settings.taskSchedulerFallback == original.taskSchedulerFallback);
    EXPECT_TRUE(settings.catchUpAfterResume == original.catchUpAfterResume);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::BeforeStart5));
    EXPECT_TRUE(settings.beforeStart5 != original.beforeStart5);
    EXPECT_TRUE(settings.enabled == original.enabled);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::Halfway));
    EXPECT_TRUE(settings.halfway != original.halfway);
    EXPECT_TRUE(settings.beforeStart5 == original.beforeStart5);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::InAppPopup));
    EXPECT_TRUE(settings.inAppPopup != original.inAppPopup);
    EXPECT_TRUE(settings.halfway == original.halfway);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::CapsulePulse));
    EXPECT_TRUE(settings.capsulePulse != original.capsulePulse);
    EXPECT_TRUE(settings.inAppPopup == original.inAppPopup);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::SystemNotification));
    EXPECT_TRUE(settings.systemNotification != original.systemNotification);
    EXPECT_TRUE(settings.capsulePulse == original.capsulePulse);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::TaskSchedulerFallback));
    EXPECT_TRUE(settings.taskSchedulerFallback != original.taskSchedulerFallback);
    EXPECT_TRUE(settings.systemNotification == original.systemNotification);

    original = settings;
    EXPECT_TRUE(ApplyReminderSettingAction(settings, ReminderSettingAction::CatchUp));
    EXPECT_TRUE(settings.catchUpAfterResume != original.catchUpAfterResume);
    EXPECT_TRUE(settings.taskSchedulerFallback == original.taskSchedulerFallback);
}

void UnknownReminderActionDoesNotMutateSettings() {
    ReminderSettings settings;
    const ReminderSettings original = settings;

    EXPECT_FALSE(ApplyReminderSettingAction(
        settings, static_cast<ReminderSettingAction>(999)));

    EXPECT_TRUE(settings.enabled == original.enabled);
    EXPECT_TRUE(settings.beforeStart5 == original.beforeStart5);
    EXPECT_TRUE(settings.halfway == original.halfway);
    EXPECT_TRUE(settings.inAppPopup == original.inAppPopup);
    EXPECT_TRUE(settings.capsulePulse == original.capsulePulse);
    EXPECT_TRUE(settings.systemNotification == original.systemNotification);
    EXPECT_TRUE(settings.taskSchedulerFallback == original.taskSchedulerFallback);
    EXPECT_TRUE(settings.catchUpAfterResume == original.catchUpAfterResume);
    EXPECT_TRUE(settings.catchUpGraceMinutes == original.catchUpGraceMinutes);
}

const TestCase kTests[] = {
    {"EveryReminderActionTogglesOnlyItsSetting", EveryReminderActionTogglesOnlyItsSetting},
    {"UnknownReminderActionDoesNotMutateSettings", UnknownReminderActionDoesNotMutateSettings},
};

} // namespace

int main() {
    return RunTests("reminder_settings_policy", kTests);
}
