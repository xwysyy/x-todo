#include "ReminderService.h"
#include "test_framework.h"

using namespace xtodo_test;

namespace {

void DisabledRemindersProduceNoCandidates() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.enabled = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    EXPECT_TRUE(service.Candidates().empty());
}

void BuildsFiveMinuteBeforeStartCandidate() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    const auto& candidates = service.Candidates();
    EXPECT_EQ(candidates.size(), static_cast<size_t>(1));
    EXPECT_EQ(candidates[0].blockId, id);
    EXPECT_TRUE(candidates[0].kind == ReminderKind::BeforeStart5);
    EXPECT_EQ(candidates[0].day, std::string("2026-06-25"));
    EXPECT_EQ(candidates[0].startMinute, 9 * 60);
    EXPECT_EQ(candidates[0].endMinute, 10 * 60);
    EXPECT_EQ(candidates[0].due.day, std::string("2026-06-25"));
    EXPECT_EQ(candidates[0].due.minute, 8 * 60 + 55);
    EXPECT_EQ(candidates[0].title, std::wstring(L"Focus"));
    EXPECT_EQ(candidates[0].key, std::string("block:") + std::to_string(id) +
                                     "|before5|2026-06-25|540|600");
}

void BeforeStartCandidateCanFallOnPreviousDay() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 3, 30, L"Early");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-24", 23 * 60});

    const auto& candidates = service.Candidates();
    EXPECT_EQ(candidates.size(), static_cast<size_t>(1));
    EXPECT_EQ(candidates[0].blockId, id);
    EXPECT_EQ(candidates[0].due.day, std::string("2026-06-24"));
    EXPECT_EQ(candidates[0].due.minute, 23 * 60 + 58);
}

void BuildsHalfwayCandidate() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 14 * 60, 15 * 60, L"Meeting");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.beforeStart5 = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 13 * 60});

    const auto& candidates = service.Candidates();
    EXPECT_EQ(candidates.size(), static_cast<size_t>(1));
    EXPECT_EQ(candidates[0].blockId, id);
    EXPECT_TRUE(candidates[0].kind == ReminderKind::Halfway);
    EXPECT_EQ(candidates[0].due.day, std::string("2026-06-25"));
    EXPECT_EQ(candidates[0].due.minute, 14 * 60 + 30);
    EXPECT_EQ(candidates[0].key, std::string("block:") + std::to_string(id) +
                                     "|halfway|2026-06-25|840|900");
}

void OneMinuteBlockDoesNotProduceHalfwayCandidate() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 14 * 60, 14 * 60 + 1, L"Short");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.beforeStart5 = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 13 * 60});

    EXPECT_TRUE(service.Candidates().empty());
}

void InvalidGregorianDayProducesNoCandidates() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-02-31", 9 * 60, 10 * 60, L"Impossible");
    EXPECT_TRUE(id > 0);

    ReminderService service;
    ReminderSettings settings;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-02-28", 8 * 60});

    EXPECT_TRUE(service.Candidates().empty());
}

void DueNowReturnsOnlyReachedUnfiredReminder() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    EXPECT_TRUE(service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 54}).empty());

    const auto due = service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 55});
    EXPECT_EQ(due.size(), static_cast<size_t>(1));
    EXPECT_EQ(due[0].blockId, id);

    service.MarkFired(due[0], 1000);
    EXPECT_TRUE(service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 56}).empty());
}

void BeforeStartReminderExpiresWhenBlockStarts() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    EXPECT_TRUE(service.DueNow(ReminderMinute{"2026-06-25", 9 * 60}).empty());
}

void HalfwayReminderExpiresWhenBlockEnds() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.beforeStart5 = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    EXPECT_TRUE(service.DueNow(ReminderMinute{"2026-06-25", 10 * 60}).empty());
}

void MovingBlockCreatesNewReminderKey() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    const auto first = service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 55});
    EXPECT_EQ(first.size(), static_cast<size_t>(1));
    service.MarkFired(first[0], 1000);

    EXPECT_TRUE(calendar.SetBlockRange(id, 11 * 60, 12 * 60));
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 10 * 60});

    const auto moved = service.DueNow(ReminderMinute{"2026-06-25", 10 * 60 + 55});
    EXPECT_EQ(moved.size(), static_cast<size_t>(1));
    EXPECT_EQ(moved[0].blockId, id);
    EXPECT_EQ(moved[0].key, std::string("block:") + std::to_string(id) +
                              "|before5|2026-06-25|660|720");
}

void RetitlingBlockKeepsReminderKeyAndUpdatesCandidateTitle() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    const auto before = service.Candidates();
    EXPECT_EQ(before.size(), static_cast<size_t>(1));

    EXPECT_TRUE(calendar.SetBlockTitle(id, L"Deep work"));
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    const auto after = service.Candidates();

    EXPECT_EQ(after.size(), static_cast<size_t>(1));
    EXPECT_EQ(after[0].key, before[0].key);
    EXPECT_EQ(after[0].title, std::wstring(L"Deep work"));
}

void CatchUpUsesValidityAndGraceRules() {
    CalendarModel calendar;
    const int before = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    const int halfway = calendar.AddBlock("2026-06-25", 14 * 60, 15 * 60, L"Meeting");
    EXPECT_TRUE(before > 0);
    EXPECT_TRUE(halfway > 0);

    ReminderSettings settings;
    settings.catchUpGraceMinutes = 10;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    const auto beforeCatchUp = service.CatchUpDue(ReminderMinute{"2026-06-25", 8 * 60 + 58});
    EXPECT_EQ(beforeCatchUp.size(), static_cast<size_t>(1));
    EXPECT_EQ(beforeCatchUp[0].blockId, before);
    EXPECT_TRUE(beforeCatchUp[0].kind == ReminderKind::BeforeStart5);

    EXPECT_TRUE(service.CatchUpDue(ReminderMinute{"2026-06-25", 9 * 60}).empty());

    const auto halfwayCatchUp = service.CatchUpDue(ReminderMinute{"2026-06-25", 14 * 60 + 35});
    EXPECT_EQ(halfwayCatchUp.size(), static_cast<size_t>(1));
    EXPECT_EQ(halfwayCatchUp[0].blockId, halfway);
    EXPECT_TRUE(halfwayCatchUp[0].kind == ReminderKind::Halfway);

    EXPECT_TRUE(service.CatchUpDue(ReminderMinute{"2026-06-25", 14 * 60 + 41}).empty());
}

void CatchUpCanBeDisabled() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;
    settings.catchUpAfterResume = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    EXPECT_TRUE(service.CatchUpDue(ReminderMinute{"2026-06-25", 8 * 60 + 58}).empty());
}

void ScheduledCheckIgnoresCatchUpDisabled() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;
    settings.catchUpAfterResume = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    const auto due = service.ScheduledCheckDue(ReminderMinute{"2026-06-25", 8 * 60 + 55});
    EXPECT_EQ(due.size(), static_cast<size_t>(1));
    EXPECT_EQ(due[0].blockId, id);
}

void FiredLogCanBeExportedImportedAndPruned() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    const auto due = service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 55});
    EXPECT_EQ(due.size(), static_cast<size_t>(1));

    service.MarkFired(due[0], 1000);

    const auto exported = service.ExportLog();
    EXPECT_EQ(exported.size(), static_cast<size_t>(1));
    EXPECT_EQ(exported[0].key, due[0].key);
    EXPECT_EQ(exported[0].firedAt, 1000LL);

    ReminderService loaded;
    loaded.ImportLog(exported);
    loaded.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    EXPECT_TRUE(loaded.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 55}).empty());

    EXPECT_FALSE(loaded.PruneLog(1000 + 13LL * 24 * 60 * 60));
    EXPECT_TRUE(loaded.PruneLog(1000 + 15LL * 24 * 60 * 60));
    EXPECT_TRUE(loaded.ExportLog().empty());
}

void NextDueSkipsFiredAndPastCandidates() {
    CalendarModel calendar;
    const int firstId = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    const int secondId = calendar.AddBlock("2026-06-25", 11 * 60, 12 * 60, L"Read");
    EXPECT_TRUE(firstId > 0);
    EXPECT_TRUE(secondId > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});
    const auto first = service.DueNow(ReminderMinute{"2026-06-25", 8 * 60 + 55});
    EXPECT_EQ(first.size(), static_cast<size_t>(1));
    service.MarkFired(first[0], 1000);

    ReminderMinute next;
    EXPECT_TRUE(service.NextDue(ReminderMinute{"2026-06-25", 9 * 60}, next));
    EXPECT_EQ(next.day, std::string("2026-06-25"));
    EXPECT_EQ(next.minute, 10 * 60 + 55);
}

void NextDueReturnsNowForStillValidOverdueReminder() {
    CalendarModel calendar;
    const int id = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Focus");
    EXPECT_TRUE(id > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    ReminderMinute next;
    EXPECT_TRUE(service.NextDue(ReminderMinute{"2026-06-25", 8 * 60 + 56}, next));
    EXPECT_EQ(next.day, std::string("2026-06-25"));
    EXPECT_EQ(next.minute, 8 * 60 + 56);
}

void NextDueSkipsExpiredCandidateAndFindsFutureReminder() {
    CalendarModel calendar;
    const int expiredId = calendar.AddBlock("2026-06-25", 9 * 60, 10 * 60, L"Expired");
    const int futureId = calendar.AddBlock("2026-06-25", 12 * 60, 13 * 60, L"Future");
    EXPECT_TRUE(expiredId > 0);
    EXPECT_TRUE(futureId > 0);

    ReminderSettings settings;
    settings.halfway = false;

    ReminderService service;
    service.Rebuild(calendar, settings, ReminderMinute{"2026-06-25", 8 * 60});

    ReminderMinute next;
    EXPECT_TRUE(service.NextDue(ReminderMinute{"2026-06-25", 10 * 60}, next));
    EXPECT_EQ(next.day, std::string("2026-06-25"));
    EXPECT_EQ(next.minute, 11 * 60 + 55);
}

const TestCase kTests[] = {
    {"DisabledRemindersProduceNoCandidates", DisabledRemindersProduceNoCandidates},
    {"BuildsFiveMinuteBeforeStartCandidate", BuildsFiveMinuteBeforeStartCandidate},
    {"BeforeStartCandidateCanFallOnPreviousDay", BeforeStartCandidateCanFallOnPreviousDay},
    {"BuildsHalfwayCandidate", BuildsHalfwayCandidate},
    {"OneMinuteBlockDoesNotProduceHalfwayCandidate", OneMinuteBlockDoesNotProduceHalfwayCandidate},
    {"InvalidGregorianDayProducesNoCandidates", InvalidGregorianDayProducesNoCandidates},
    {"DueNowReturnsOnlyReachedUnfiredReminder", DueNowReturnsOnlyReachedUnfiredReminder},
    {"BeforeStartReminderExpiresWhenBlockStarts", BeforeStartReminderExpiresWhenBlockStarts},
    {"HalfwayReminderExpiresWhenBlockEnds", HalfwayReminderExpiresWhenBlockEnds},
    {"MovingBlockCreatesNewReminderKey", MovingBlockCreatesNewReminderKey},
    {"RetitlingBlockKeepsReminderKeyAndUpdatesCandidateTitle", RetitlingBlockKeepsReminderKeyAndUpdatesCandidateTitle},
    {"CatchUpUsesValidityAndGraceRules", CatchUpUsesValidityAndGraceRules},
    {"CatchUpCanBeDisabled", CatchUpCanBeDisabled},
    {"ScheduledCheckIgnoresCatchUpDisabled", ScheduledCheckIgnoresCatchUpDisabled},
    {"FiredLogCanBeExportedImportedAndPruned", FiredLogCanBeExportedImportedAndPruned},
    {"NextDueSkipsFiredAndPastCandidates", NextDueSkipsFiredAndPastCandidates},
    {"NextDueReturnsNowForStillValidOverdueReminder", NextDueReturnsNowForStillValidOverdueReminder},
    {"NextDueSkipsExpiredCandidateAndFindsFutureReminder", NextDueSkipsExpiredCandidateAndFindsFutureReminder},
};

} // namespace

int main() {
    return RunTests("reminder_service", kTests);
}
