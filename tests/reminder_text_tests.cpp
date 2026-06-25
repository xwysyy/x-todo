#include "ReminderText.h"
#include "test_framework.h"

#include <vector>

using namespace xtodo_test;

namespace {

ReminderCandidate Candidate(int id, ReminderKind kind, const wchar_t* title,
                            int start, int end) {
    ReminderCandidate candidate;
    candidate.key = std::string("block:") + std::to_string(id);
    candidate.blockId = id;
    candidate.kind = kind;
    candidate.day = "2026-06-25";
    candidate.startMinute = start;
    candidate.endMinute = end;
    candidate.title = title;
    return candidate;
}

void SingleBeforeStartReminderTextNamesTimeAndTitle() {
    const auto text = FormatReminderText(
        {Candidate(1, ReminderKind::BeforeStart5, L"Focus", 9 * 60, 10 * 60)},
        Lang::En);

    EXPECT_EQ(text.title, std::wstring(L"Starts in 5 minutes"));
    EXPECT_EQ(text.lines.size(), static_cast<size_t>(1));
    EXPECT_EQ(text.lines[0], std::wstring(L"09:00-10:00 Focus"));
}

void GroupedReminderTextSummarizesAndListsCandidates() {
    const std::vector<ReminderCandidate> reminders = {
        Candidate(1, ReminderKind::BeforeStart5, L"Focus", 9 * 60, 10 * 60),
        Candidate(2, ReminderKind::Halfway, L"Meeting", 14 * 60, 15 * 60),
    };

    const auto text = FormatReminderText(reminders, Lang::En);

    EXPECT_EQ(text.title, std::wstring(L"2 calendar reminders"));
    EXPECT_EQ(text.lines.size(), static_cast<size_t>(2));
    EXPECT_EQ(text.lines[0], std::wstring(L"09:00 Focus starts soon"));
    EXPECT_EQ(text.lines[1], std::wstring(L"14:00-15:00 Meeting is halfway through"));
}

void GroupedSameKindReminderTextUsesSpecificChineseTitle() {
    const std::vector<ReminderCandidate> reminders = {
        Candidate(1, ReminderKind::BeforeStart5, L"会议", 9 * 60, 10 * 60),
        Candidate(2, ReminderKind::BeforeStart5, L"写作", 11 * 60, 12 * 60),
    };

    const auto text = FormatReminderText(reminders, Lang::Zh);

    EXPECT_EQ(text.title, std::wstring(L"2 个安排将在 5 分钟后开始"));
    EXPECT_EQ(text.lines.size(), static_cast<size_t>(2));
    EXPECT_EQ(text.lines[0], std::wstring(L"09:00 会议 即将开始"));
    EXPECT_EQ(text.lines[1], std::wstring(L"11:00 写作 即将开始"));
}

void GroupedHalfwayReminderTextUsesSpecificChineseTitle() {
    const std::vector<ReminderCandidate> reminders = {
        Candidate(1, ReminderKind::Halfway, L"会议", 9 * 60, 10 * 60),
        Candidate(2, ReminderKind::Halfway, L"写作", 11 * 60, 12 * 60),
    };

    const auto text = FormatReminderText(reminders, Lang::Zh);

    EXPECT_EQ(text.title, std::wstring(L"2 个安排已进行到一半"));
}

void ReminderBodyCanBeBoundedForSystemNotifications() {
    ReminderDisplayText text;
    text.lines = {
        L"09:00 First very long reminder title",
        L"10:00 Second very long reminder title",
        L"11:00 Third very long reminder title",
    };

    const std::wstring body = JoinReminderLines(text, 48);

    EXPECT_TRUE(body.size() <= 48);
    EXPECT_TRUE(body.find(L"09:00 First") != std::wstring::npos);
    EXPECT_TRUE(body.rfind(L"...") == body.size() - 3);
}

const TestCase kTests[] = {
    {"SingleBeforeStartReminderTextNamesTimeAndTitle", SingleBeforeStartReminderTextNamesTimeAndTitle},
    {"GroupedReminderTextSummarizesAndListsCandidates", GroupedReminderTextSummarizesAndListsCandidates},
    {"GroupedSameKindReminderTextUsesSpecificChineseTitle", GroupedSameKindReminderTextUsesSpecificChineseTitle},
    {"GroupedHalfwayReminderTextUsesSpecificChineseTitle", GroupedHalfwayReminderTextUsesSpecificChineseTitle},
    {"ReminderBodyCanBeBoundedForSystemNotifications", ReminderBodyCanBeBoundedForSystemNotifications},
};

} // namespace

int main() {
    return RunTests("reminder_text", kTests);
}
