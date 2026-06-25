#include "ReminderText.h"

#include <cstdio>

namespace {

std::wstring FormatTime(int minute) {
    const int hour = minute / 60;
    const int min = minute % 60;
    wchar_t buf[8]{};
    std::swprintf(buf, 8, L"%02d:%02d", hour, min);
    return buf;
}

std::wstring FormatRange(int startMinute, int endMinute) {
    return FormatTime(startMinute) + L"-" + FormatTime(endMinute);
}

std::wstring TitleOrFallback(const ReminderCandidate& reminder, Lang lang) {
    if (!reminder.title.empty()) return reminder.title;
    return lang == Lang::Zh ? L"未命名时间块" : L"Untitled block";
}

std::wstring SingleTitle(const ReminderCandidate& reminder, Lang lang) {
    if (reminder.kind == ReminderKind::BeforeStart5)
        return lang == Lang::Zh ? L"5 分钟后开始" : L"Starts in 5 minutes";
    return lang == Lang::Zh ? L"已进行到一半" : L"Halfway through";
}

std::wstring SingleLine(const ReminderCandidate& reminder, Lang lang) {
    return FormatRange(reminder.startMinute, reminder.endMinute) + L" " +
           TitleOrFallback(reminder, lang);
}

std::wstring GroupLine(const ReminderCandidate& reminder, Lang lang) {
    const std::wstring title = TitleOrFallback(reminder, lang);
    if (reminder.kind == ReminderKind::BeforeStart5) {
        if (lang == Lang::Zh)
            return FormatTime(reminder.startMinute) + L" " + title + L" 即将开始";
        return FormatTime(reminder.startMinute) + L" " + title + L" starts soon";
    }
    if (lang == Lang::Zh)
        return FormatRange(reminder.startMinute, reminder.endMinute) + L" " + title +
               L" 已进行到一半";
    return FormatRange(reminder.startMinute, reminder.endMinute) + L" " + title +
           L" is halfway through";
}

bool AllKind(const std::vector<ReminderCandidate>& reminders, ReminderKind kind) {
    for (const ReminderCandidate& reminder : reminders) {
        if (reminder.kind != kind) return false;
    }
    return !reminders.empty();
}

std::wstring GroupTitle(const std::vector<ReminderCandidate>& reminders, Lang lang) {
    const std::wstring count = std::to_wstring(reminders.size());
    if (AllKind(reminders, ReminderKind::BeforeStart5)) {
        return lang == Lang::Zh ? count + L" 个安排将在 5 分钟后开始"
                                : count + L" blocks start in 5 minutes";
    }
    if (AllKind(reminders, ReminderKind::Halfway)) {
        return lang == Lang::Zh ? count + L" 个安排已进行到一半"
                                : count + L" blocks are halfway through";
    }
    return count + (lang == Lang::Zh ? L" 个日历提醒" : L" calendar reminders");
}

} // namespace

ReminderDisplayText FormatReminderText(const std::vector<ReminderCandidate>& reminders,
                                       Lang lang) {
    ReminderDisplayText text;
    if (reminders.empty()) return text;

    if (reminders.size() == 1) {
        text.title = SingleTitle(reminders[0], lang);
        text.lines.push_back(SingleLine(reminders[0], lang));
        return text;
    }

    text.title = GroupTitle(reminders, lang);
    for (const ReminderCandidate& reminder : reminders) {
        text.lines.push_back(GroupLine(reminder, lang));
    }
    return text;
}

std::wstring JoinReminderLines(const ReminderDisplayText& text, size_t maxChars) {
    std::wstring body;
    for (size_t i = 0; i < text.lines.size(); ++i) {
        if (i > 0) body += L"\n";
        body += text.lines[i];
        if (maxChars > 0 && body.size() > maxChars) break;
    }

    if (maxChars > 0 && body.size() > maxChars) {
        if (maxChars <= 3) {
            body.resize(maxChars);
        } else {
            body.resize(maxChars - 3);
            body += L"...";
        }
    }
    return body;
}
