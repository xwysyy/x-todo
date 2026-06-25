#include "ReminderDispatchPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void NoAcceptedChannelDoesNotMarkFired() {
    const ReminderDispatchResult result = DecideReminderDispatch({
        ReminderChannelResult{true, false},
        ReminderChannelResult{false, false},
        ReminderChannelResult{true, false},
    });

    EXPECT_FALSE(result.markFired);
}

void AcceptedChannelMarksFired() {
    const ReminderDispatchResult result = DecideReminderDispatch({
        ReminderChannelResult{true, false},
        ReminderChannelResult{true, true},
        ReminderChannelResult{false, false},
    });

    EXPECT_TRUE(result.markFired);
}

void UnattemptedAcceptedValueDoesNotCount() {
    const ReminderDispatchResult result = DecideReminderDispatch({
        ReminderChannelResult{false, true},
    });

    EXPECT_FALSE(result.markFired);
}

void BackgroundDeliveryRequiresDurableChannel() {
    const ReminderDispatchResult result = DecideReminderDispatch({
        ReminderChannelResult{true, true, false},
        ReminderChannelResult{true, false, true},
    }, true);

    EXPECT_FALSE(result.markFired);
}

const TestCase kTests[] = {
    {"NoAcceptedChannelDoesNotMarkFired", NoAcceptedChannelDoesNotMarkFired},
    {"AcceptedChannelMarksFired", AcceptedChannelMarksFired},
    {"UnattemptedAcceptedValueDoesNotCount", UnattemptedAcceptedValueDoesNotCount},
    {"BackgroundDeliveryRequiresDurableChannel", BackgroundDeliveryRequiresDurableChannel},
};

} // namespace

int main() {
    return RunTests("reminder_dispatch_policy", kTests);
}
