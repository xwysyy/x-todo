#include "ReminderTimerPolicy.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

constexpr unsigned int kMaxMs = 60 * 1000;

void NoNextDueUsesMaxInterval() {
    const unsigned int delay = ComputeReminderTimerDelayMs(
        ReminderMinute{"2026-06-25", 9 * 60}, false,
        ReminderMinute{"", 0}, kMaxMs);

    EXPECT_EQ(delay, kMaxMs);
}

void DueNowUsesImmediateOneSecondScan() {
    const ReminderMinute now{"2026-06-25", 9 * 60};
    const unsigned int delay = ComputeReminderTimerDelayMs(now, true, now, kMaxMs);

    EXPECT_EQ(delay, 1000U);
}

void PastDueUsesImmediateOneSecondScan() {
    const unsigned int delay = ComputeReminderTimerDelayMs(
        ReminderMinute{"2026-06-25", 9 * 60}, true,
        ReminderMinute{"2026-06-25", 8 * 60 + 55}, kMaxMs);

    EXPECT_EQ(delay, 1000U);
}

void FutureSameDayWithinCapUsesMinuteDelay() {
    const unsigned int delay = ComputeReminderTimerDelayMs(
        ReminderMinute{"2026-06-25", 9 * 60}, true,
        ReminderMinute{"2026-06-25", 9 * 60 + 1}, kMaxMs);

    EXPECT_EQ(delay, kMaxMs);
}

void FutureSameDayBeyondCapUsesMaxInterval() {
    const unsigned int delay = ComputeReminderTimerDelayMs(
        ReminderMinute{"2026-06-25", 9 * 60}, true,
        ReminderMinute{"2026-06-25", 9 * 60 + 30}, kMaxMs);

    EXPECT_EQ(delay, kMaxMs);
}

void FutureAnotherDayUsesMaxInterval() {
    const unsigned int delay = ComputeReminderTimerDelayMs(
        ReminderMinute{"2026-06-25", 23 * 60 + 59}, true,
        ReminderMinute{"2026-06-26", 0}, kMaxMs);

    EXPECT_EQ(delay, kMaxMs);
}

const TestCase kTests[] = {
    {"NoNextDueUsesMaxInterval", NoNextDueUsesMaxInterval},
    {"DueNowUsesImmediateOneSecondScan", DueNowUsesImmediateOneSecondScan},
    {"PastDueUsesImmediateOneSecondScan", PastDueUsesImmediateOneSecondScan},
    {"FutureSameDayWithinCapUsesMinuteDelay", FutureSameDayWithinCapUsesMinuteDelay},
    {"FutureSameDayBeyondCapUsesMaxInterval", FutureSameDayBeyondCapUsesMaxInterval},
    {"FutureAnotherDayUsesMaxInterval", FutureAnotherDayUsesMaxInterval},
};

} // namespace

int main() {
    return RunTests("reminder_timer_policy", kTests);
}
