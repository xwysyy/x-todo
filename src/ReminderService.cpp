#include "ReminderService.h"

#include "CalendarDate.h"

#include <string>
#include <utility>
#include <vector>

namespace {

std::string ReminderKindKey(ReminderKind kind) {
    switch (kind) {
        case ReminderKind::BeforeStart5: return "before5";
        case ReminderKind::Halfway: return "halfway";
    }
    return "";
}

std::string MakeReminderKey(const CalendarBlock& block, ReminderKind kind) {
    return "block:" + std::to_string(block.id) + "|" + ReminderKindKey(kind) +
           "|" + block.day + "|" + std::to_string(block.startMinute) +
           "|" + std::to_string(block.endMinute);
}

ReminderMinute OffsetMinute(const std::string& day, int minute, int delta) {
    CalendarDate::Date date;
    if (!CalendarDate::Parse(day, date)) return ReminderMinute{day, minute + delta};

    int value = minute + delta;
    while (value < 0) {
        date = CalendarDate::AddDays(date, -1);
        value += 1440;
    }
    while (value >= 1440) {
        date = CalendarDate::AddDays(date, 1);
        value -= 1440;
    }
    return ReminderMinute{CalendarDate::Format(date), value};
}

ReminderCandidate MakeCandidate(const CalendarBlock& block, ReminderKind kind,
                                ReminderMinute due) {
    ReminderCandidate candidate;
    candidate.key = MakeReminderKey(block, kind);
    candidate.blockId = block.id;
    candidate.kind = kind;
    candidate.day = block.day;
    candidate.startMinute = block.startMinute;
    candidate.endMinute = block.endMinute;
    candidate.due = std::move(due);
    candidate.title = block.title;
    return candidate;
}

ReminderMinute BlockMinute(const ReminderCandidate& candidate, int minute) {
    return OffsetMinute(candidate.day, minute, 0);
}

int CompareReminderMinute(const ReminderMinute& a, const ReminderMinute& b) {
    if (a.day < b.day) return -1;
    if (a.day > b.day) return 1;
    if (a.minute < b.minute) return -1;
    if (a.minute > b.minute) return 1;
    return 0;
}

int MinuteDistance(const ReminderMinute& from, const ReminderMinute& to) {
    CalendarDate::Date fromDate;
    CalendarDate::Date toDate;
    if (!CalendarDate::Parse(from.day, fromDate) || !CalendarDate::Parse(to.day, toDate))
        return to.minute - from.minute;

    int days = 0;
    CalendarDate::Date cursor = fromDate;
    while (CalendarDate::Format(cursor) < to.day) {
        cursor = CalendarDate::AddDays(cursor, 1);
        ++days;
    }
    while (CalendarDate::Format(cursor) > to.day) {
        cursor = CalendarDate::AddDays(cursor, -1);
        --days;
    }
    return days * 1440 + (to.minute - from.minute);
}

bool IsCandidateStillValid(const ReminderCandidate& candidate, const ReminderMinute& now) {
    if (candidate.kind == ReminderKind::BeforeStart5) {
        return CompareReminderMinute(now, BlockMinute(candidate, candidate.startMinute)) < 0;
    }
    if (candidate.kind == ReminderKind::Halfway) {
        return CompareReminderMinute(now, BlockMinute(candidate, candidate.endMinute)) < 0;
    }
    return false;
}

bool IsCatchUpCandidateValid(const ReminderCandidate& candidate, const ReminderMinute& now,
                             const ReminderSettings& settings) {
    if (!IsCandidateStillValid(candidate, now)) return false;
    if (candidate.kind == ReminderKind::BeforeStart5) return true;
    if (candidate.kind == ReminderKind::Halfway) {
        return MinuteDistance(candidate.due, now) <= settings.catchUpGraceMinutes;
    }
    return false;
}

} // namespace

void ReminderService::Rebuild(const CalendarModel& calendar, const ReminderSettings& settings,
                              ReminderMinute) {
    settings_ = settings;
    candidates_.clear();
    if (!settings.enabled) return;

    for (const CalendarBlock& block : calendar.Blocks()) {
        CalendarDate::Date parsedDay;
        if (!CalendarDate::Parse(block.day, parsedDay)) continue;
        if (block.endMinute <= block.startMinute) continue;

        if (settings.beforeStart5) {
            candidates_.push_back(
                MakeCandidate(block, ReminderKind::BeforeStart5,
                              OffsetMinute(block.day, block.startMinute, -5)));
        }
        if (settings.halfway) {
            const int duration = block.endMinute - block.startMinute;
            if (duration >= 2) {
                candidates_.push_back(
                    MakeCandidate(block, ReminderKind::Halfway,
                                  OffsetMinute(block.day, block.startMinute, duration / 2)));
            }
        }
    }
}

std::vector<ReminderCandidate> ReminderService::DueNow(ReminderMinute now) const {
    std::vector<ReminderCandidate> due;
    for (const ReminderCandidate& candidate : candidates_) {
        if (fired_.find(candidate.key) != fired_.end()) continue;
        if (CompareReminderMinute(candidate.due, now) > 0) continue;
        if (!IsCandidateStillValid(candidate, now)) continue;
        due.push_back(candidate);
    }
    return due;
}

std::vector<ReminderCandidate> ReminderService::ScheduledCheckDue(ReminderMinute now) const {
    return DueNow(std::move(now));
}

std::vector<ReminderCandidate> ReminderService::CatchUpDue(ReminderMinute now) const {
    std::vector<ReminderCandidate> due;
    if (!settings_.catchUpAfterResume) return due;
    for (const ReminderCandidate& candidate : candidates_) {
        if (fired_.find(candidate.key) != fired_.end()) continue;
        if (CompareReminderMinute(candidate.due, now) > 0) continue;
        if (!IsCatchUpCandidateValid(candidate, now, settings_)) continue;
        due.push_back(candidate);
    }
    return due;
}

bool ReminderService::NextDue(ReminderMinute now, ReminderMinute& out) const {
    bool found = false;
    for (const ReminderCandidate& candidate : candidates_) {
        if (fired_.find(candidate.key) != fired_.end()) continue;
        if (CompareReminderMinute(candidate.due, now) <= 0) {
            if (!IsCandidateStillValid(candidate, now)) continue;
            out = now;
            return true;
        }
        if (!found || CompareReminderMinute(candidate.due, out) < 0) {
            out = candidate.due;
            found = true;
        }
    }
    return found;
}

void ReminderService::MarkFired(const ReminderCandidate& candidate, long long firedAt) {
    if (candidate.key.empty() || firedAt <= 0) return;
    fired_[candidate.key] = firedAt;
}

void ReminderService::ImportLog(const std::vector<ReminderLogEntry>& log) {
    fired_.clear();
    for (const ReminderLogEntry& entry : log) {
        if (entry.key.empty() || entry.firedAt <= 0) continue;
        fired_[entry.key] = entry.firedAt;
    }
}

std::vector<ReminderLogEntry> ReminderService::ExportLog() const {
    std::vector<ReminderLogEntry> log;
    log.reserve(fired_.size());
    for (const auto& kv : fired_) {
        log.push_back(ReminderLogEntry{kv.first, kv.second});
    }
    return log;
}

bool ReminderService::PruneLog(long long now) {
    bool changed = false;
    constexpr long long kKeepSeconds = 14LL * 24 * 60 * 60;
    for (auto it = fired_.begin(); it != fired_.end(); ) {
        if (now - it->second > kKeepSeconds) {
            it = fired_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    return changed;
}
