#include "ReminderVisualPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void CapsulePulseRequiresVisibleWindowAndDueReminder() {
    EXPECT_TRUE(CanStartCapsuleReminderPulse(true, true));
    EXPECT_FALSE(CanStartCapsuleReminderPulse(true, false));
    EXPECT_FALSE(CanStartCapsuleReminderPulse(false, true));
    EXPECT_FALSE(CanStartCapsuleReminderPulse(false, false));
}

const TestCase kTests[] = {
    {"CapsulePulseRequiresVisibleWindowAndDueReminder", CapsulePulseRequiresVisibleWindowAndDueReminder},
};

} // namespace

int main() {
    return RunTests("reminder_visual_policy", kTests);
}
