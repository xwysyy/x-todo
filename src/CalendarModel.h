#pragma once

#include <string>
#include <vector>

struct CalendarBlock {
    int id = 0;
    std::string day;
    int startMinute = 0;
    int endMinute = 30;
    std::wstring title;
};

class CalendarModel {
public:
    const std::vector<CalendarBlock>& Blocks() const { return blocks_; }

    int AddBlock(const std::string& day, int startMinute, int endMinute,
                 const std::wstring& title);
    bool RemoveBlock(int id);
    bool SetBlockTitle(int id, const std::wstring& title);
    bool SetBlockRange(int id, int startMinute, int endMinute);

    const CalendarBlock* FindBlock(int id) const;
    CalendarBlock* FindBlock(int id);

    std::vector<const CalendarBlock*> BlocksForDay(const std::string& day) const;
    void ReplaceBlocks(std::vector<CalendarBlock> blocks);

private:
    int MakeBlockId();
    bool HasBlockId(int id) const;
    void SortBlocks();

    std::vector<CalendarBlock> blocks_;
    int nextId_ = 1;
};

int ClampCalendarMinute(int minute);
bool IsValidCalendarDayKey(const std::string& day);
