#include "StoreFormat.h"
#include "test_framework.h"

#include <string>
#include <vector>

using namespace xtodo_test;

namespace {

void ReminderSettingsAndLogRoundTrip() {
    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    ui.reminders.enabled = false;
    ui.reminders.beforeStart5 = false;
    ui.reminders.halfway = true;
    ui.reminders.inAppPopup = false;
    ui.reminders.capsulePulse = true;
    ui.reminders.systemNotification = true;
    ui.reminders.taskSchedulerFallback = true;
    ui.reminders.catchUpAfterResume = false;
    ui.reminders.catchUpGraceMinutes = 17;

    const std::vector<ReminderLogEntry> log = {
        ReminderLogEntry{"block:42|before5|2026-06-25|840|900", 1782396900},
    };

    const std::string text = StoreFormat::Serialize(model, calendar, geom, ui, log);
    EXPECT_TRUE(text.find("\"reminders\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"reminderLog\"") != std::string::npos);
    EXPECT_TRUE(text.find("\"taskSchedulerFallback\": true") != std::string::npos);
    EXPECT_TRUE(text.find("\"catchUpGraceMinutes\": 17") != std::string::npos);

    TodoModel loadedModel;
    CalendarModel loadedCalendar;
    WindowGeometry loadedGeom;
    UiState loadedUi;
    std::vector<ReminderLogEntry> loadedLog;
    EXPECT_TRUE(StoreFormat::Parse(text, loadedModel, loadedCalendar, loadedGeom,
                                   loadedUi, loadedLog));

    EXPECT_FALSE(loadedUi.reminders.enabled);
    EXPECT_FALSE(loadedUi.reminders.beforeStart5);
    EXPECT_TRUE(loadedUi.reminders.halfway);
    EXPECT_FALSE(loadedUi.reminders.inAppPopup);
    EXPECT_TRUE(loadedUi.reminders.capsulePulse);
    EXPECT_TRUE(loadedUi.reminders.systemNotification);
    EXPECT_TRUE(loadedUi.reminders.taskSchedulerFallback);
    EXPECT_FALSE(loadedUi.reminders.catchUpAfterResume);
    EXPECT_EQ(loadedUi.reminders.catchUpGraceMinutes, 17);

    EXPECT_EQ(loadedLog.size(), static_cast<size_t>(1));
    EXPECT_EQ(loadedLog[0].key, std::string("block:42|before5|2026-06-25|840|900"));
    EXPECT_EQ(loadedLog[0].firedAt, 1782396900LL);
}

void MissingReminderSettingsUseDefaults() {
    const std::string text = R"({
      "ui": { "currentList": "inbox" },
      "lists": [ { "id": "inbox", "title": "Inbox", "items": [] } ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    std::vector<ReminderLogEntry> log;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui, log));

    EXPECT_TRUE(ui.reminders.enabled);
    EXPECT_TRUE(ui.reminders.beforeStart5);
    EXPECT_TRUE(ui.reminders.halfway);
    EXPECT_TRUE(ui.reminders.inAppPopup);
    EXPECT_TRUE(ui.reminders.capsulePulse);
    EXPECT_FALSE(ui.reminders.systemNotification);
    EXPECT_FALSE(ui.reminders.taskSchedulerFallback);
    EXPECT_TRUE(ui.reminders.catchUpAfterResume);
    EXPECT_EQ(ui.reminders.catchUpGraceMinutes, 10);
    EXPECT_TRUE(log.empty());
}

void MalformedReminderSettingsAndLogEntriesAreIgnored() {
    const std::string text = R"({
      "ui": {
        "reminders": {
          "enabled": "yes",
          "beforeStart5": 1,
          "halfway": false,
          "inAppPopup": false,
          "capsulePulse": false,
          "systemNotification": true,
          "taskSchedulerFallback": "enabled",
          "catchUpAfterResume": true,
          "catchUpGraceMinutes": -5
        },
        "currentList": "inbox"
      },
      "reminderLog": [
        { "key": "", "firedAt": 100 },
        { "key": "block:1|before5|2026-06-25|540|600", "firedAt": -1 },
        { "key": "block:2|halfway|2026-06-25|540|600", "firedAt": 200 }
      ],
      "lists": [ { "id": "inbox", "title": "Inbox", "items": [] } ]
    })";

    TodoModel model;
    CalendarModel calendar;
    WindowGeometry geom;
    UiState ui;
    std::vector<ReminderLogEntry> log;
    EXPECT_TRUE(StoreFormat::Parse(text, model, calendar, geom, ui, log));

    EXPECT_TRUE(ui.reminders.enabled);
    EXPECT_TRUE(ui.reminders.beforeStart5);
    EXPECT_FALSE(ui.reminders.halfway);
    EXPECT_FALSE(ui.reminders.inAppPopup);
    EXPECT_FALSE(ui.reminders.capsulePulse);
    EXPECT_TRUE(ui.reminders.systemNotification);
    EXPECT_FALSE(ui.reminders.taskSchedulerFallback);
    EXPECT_TRUE(ui.reminders.catchUpAfterResume);
    EXPECT_EQ(ui.reminders.catchUpGraceMinutes, 10);

    EXPECT_EQ(log.size(), static_cast<size_t>(1));
    EXPECT_EQ(log[0].key, std::string("block:2|halfway|2026-06-25|540|600"));
    EXPECT_EQ(log[0].firedAt, 200LL);
}

const TestCase kTests[] = {
    {"ReminderSettingsAndLogRoundTrip", ReminderSettingsAndLogRoundTrip},
    {"MissingReminderSettingsUseDefaults", MissingReminderSettingsUseDefaults},
    {"MalformedReminderSettingsAndLogEntriesAreIgnored", MalformedReminderSettingsAndLogEntriesAreIgnored},
};

} // namespace

int main() {
    return RunTests("reminder_format", kTests);
}
