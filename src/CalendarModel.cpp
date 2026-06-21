#include "CalendarModel.h"

#include <algorithm>
#include <utility>

namespace {

bool NormalizeRange(int& startMinute, int& endMinute) {
    startMinute = ClampCalendarMinute(startMinute);
    endMinute = ClampCalendarMinute(endMinute);
    if (endMinute <= startMinute) {
        if (startMinute < 1440) {
            endMinute = startMinute + 1;
        } else {
            startMinute = 1439;
            endMinute = 1440;
        }
    }
    return startMinute >= 0 && startMinute < endMinute && endMinute <= 1440;
}

} // namespace

int ClampCalendarMinute(int minute) {
    if (minute < 0) return 0;
    if (minute > 1440) return 1440;
    return minute;
}

bool IsValidCalendarDayKey(const std::string& day) {
    if (day.size() != 10) return false;
    for (int i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (day[(size_t)i] != '-') return false;
            continue;
        }
        if (day[(size_t)i] < '0' || day[(size_t)i] > '9') return false;
    }
    const int month = (day[5] - '0') * 10 + (day[6] - '0');
    const int date = (day[8] - '0') * 10 + (day[9] - '0');
    return month >= 1 && month <= 12 && date >= 1 && date <= 31;
}

int CalendarModel::AddBlock(const std::string& day, int startMinute, int endMinute,
                            const std::wstring& title) {
    if (!IsValidCalendarDayKey(day)) return -1;
    if (!NormalizeRange(startMinute, endMinute)) return -1;

    CalendarBlock block;
    block.id = MakeBlockId();
    block.day = day;
    block.startMinute = startMinute;
    block.endMinute = endMinute;
    block.title = title;
    const int id = block.id;
    blocks_.push_back(std::move(block));
    SortBlocks();
    return id;
}

bool CalendarModel::RemoveBlock(int id) {
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it) {
        if (it->id == id) {
            blocks_.erase(it);
            return true;
        }
    }
    return false;
}

bool CalendarModel::SetBlockTitle(int id, const std::wstring& title) {
    CalendarBlock* block = FindBlock(id);
    if (!block) return false;
    block->title = title;
    return true;
}

bool CalendarModel::SetBlockRange(int id, int startMinute, int endMinute) {
    CalendarBlock* block = FindBlock(id);
    if (!block) return false;
    if (!NormalizeRange(startMinute, endMinute)) return false;
    block->startMinute = startMinute;
    block->endMinute = endMinute;
    SortBlocks();
    return true;
}

const CalendarBlock* CalendarModel::FindBlock(int id) const {
    for (const CalendarBlock& block : blocks_) {
        if (block.id == id) return &block;
    }
    return nullptr;
}

CalendarBlock* CalendarModel::FindBlock(int id) {
    for (CalendarBlock& block : blocks_) {
        if (block.id == id) return &block;
    }
    return nullptr;
}

std::vector<const CalendarBlock*> CalendarModel::BlocksForDay(const std::string& day) const {
    std::vector<const CalendarBlock*> result;
    for (const CalendarBlock& block : blocks_) {
        if (block.day == day) result.push_back(&block);
    }
    return result;
}

void CalendarModel::ReplaceBlocks(std::vector<CalendarBlock> blocks) {
    blocks_.clear();
    nextId_ = 1;

    for (CalendarBlock block : blocks) {
        if (!IsValidCalendarDayKey(block.day)) continue;
        if (!NormalizeRange(block.startMinute, block.endMinute)) continue;
        if (block.id <= 0 || HasBlockId(block.id)) {
            block.id = MakeBlockId();
        } else if (block.id >= nextId_) {
            nextId_ = block.id + 1;
        }
        blocks_.push_back(std::move(block));
    }
    SortBlocks();
}

int CalendarModel::MakeBlockId() {
    return nextId_++;
}

bool CalendarModel::HasBlockId(int id) const {
    for (const CalendarBlock& block : blocks_) {
        if (block.id == id) return true;
    }
    return false;
}

void CalendarModel::SortBlocks() {
    std::sort(blocks_.begin(), blocks_.end(), [](const CalendarBlock& a, const CalendarBlock& b) {
        if (a.day != b.day) return a.day < b.day;
        if (a.startMinute != b.startMinute) return a.startMinute < b.startMinute;
        if (a.endMinute != b.endMinute) return a.endMinute < b.endMinute;
        return a.id < b.id;
    });
}
