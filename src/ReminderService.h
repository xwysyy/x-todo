#pragma once

#include "CalendarModel.h"
#include "ReminderTypes.h"

#include <unordered_map>
#include <vector>

class ReminderService {
public:
    void Rebuild(const CalendarModel& calendar, const ReminderSettings& settings,
                 ReminderMinute now);

    const std::vector<ReminderCandidate>& Candidates() const { return candidates_; }
    std::vector<ReminderCandidate> DueNow(ReminderMinute now) const;
    std::vector<ReminderCandidate> ScheduledCheckDue(ReminderMinute now) const;
    std::vector<ReminderCandidate> CatchUpDue(ReminderMinute now) const;
    bool NextDue(ReminderMinute now, ReminderMinute& out) const;
    void MarkFired(const ReminderCandidate& candidate, long long firedAt);
    void ImportLog(const std::vector<ReminderLogEntry>& log);
    std::vector<ReminderLogEntry> ExportLog() const;
    bool PruneLog(long long now);

private:
    ReminderSettings settings_;
    std::vector<ReminderCandidate> candidates_;
    std::unordered_map<std::string, long long> fired_;
};
