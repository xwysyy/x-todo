#include "TaskSchedulerReminder.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void FormatsReminderMinuteAsLocalIsoStartBoundary() {
    const std::wstring boundary =
        TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-06-25", 8 * 60 + 55});

    EXPECT_EQ(boundary, std::wstring(L"2026-06-25T08:55:00"));
}

void RejectsInvalidReminderMinuteBoundary() {
    EXPECT_TRUE(TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-6-25", 8 * 60}).empty());
    EXPECT_TRUE(TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-02-31", 8 * 60}).empty());
    EXPECT_TRUE(TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-13-01", 8 * 60}).empty());
    EXPECT_TRUE(TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-06-25", -1}).empty());
    EXPECT_TRUE(TaskSchedulerReminder::FormatStartBoundaryIsoLocal({"2026-06-25", 1440}).empty());
}

void ReminderCheckTaskUsesDocumentedCommandArgument() {
    EXPECT_EQ(TaskSchedulerReminder::ReminderCheckArguments(),
              std::wstring(L"--reminder-check"));
}

const TestCase kTests[] = {
    {"FormatsReminderMinuteAsLocalIsoStartBoundary", FormatsReminderMinuteAsLocalIsoStartBoundary},
    {"RejectsInvalidReminderMinuteBoundary", RejectsInvalidReminderMinuteBoundary},
    {"ReminderCheckTaskUsesDocumentedCommandArgument", ReminderCheckTaskUsesDocumentedCommandArgument},
};

} // namespace

int main() {
    return RunTests("reminder_scheduler", kTests);
}
