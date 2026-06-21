#pragma once

#include "TodoModel.h"
#include "test_framework.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace xtodo_test {

inline std::vector<std::wstring> Texts(const TodoModel& model) {
    std::vector<std::wstring> out;
    for (const TodoItem& item : model.Items()) out.push_back(item.text);
    return out;
}

inline std::vector<int> Levels(const TodoModel& model) {
    std::vector<int> out;
    for (const TodoItem& item : model.Items()) out.push_back(item.level);
    return out;
}

inline std::vector<bool> Dones(const TodoModel& model) {
    std::vector<bool> out;
    for (const TodoItem& item : model.Items()) out.push_back(item.done);
    return out;
}

inline std::vector<bool> Collapsed(const TodoModel& model) {
    std::vector<bool> out;
    for (const TodoItem& item : model.Items()) out.push_back(item.collapsed);
    return out;
}

inline int LocalSubtreeEnd(const TodoList& list, int index, int limit) {
    const int count = static_cast<int>(list.items.size());
    if (index < 0 || index >= count) return index;
    if (limit < index + 1) limit = index + 1;
    if (limit > count) limit = count;
    const int baseLevel = list.items[static_cast<size_t>(index)].level;
    int end = index + 1;
    while (end < limit && list.items[static_cast<size_t>(end)].level > baseLevel) ++end;
    return end;
}

inline void ExpectTexts(const TodoModel& model, std::initializer_list<const wchar_t*> expected) {
    std::vector<std::wstring> actual = Texts(model);
    std::vector<std::wstring> want;
    for (const wchar_t* text : expected) want.emplace_back(text);
    EXPECT_EQ(actual, want);
}

inline void ExpectLevels(const TodoModel& model, std::initializer_list<int> expected) {
    std::vector<int> actual = Levels(model);
    std::vector<int> want(expected);
    EXPECT_EQ(actual, want);
}

inline void ExpectDones(const TodoModel& model, std::initializer_list<bool> expected) {
    std::vector<bool> actual = Dones(model);
    std::vector<bool> want(expected);
    EXPECT_EQ(actual, want);
}

inline void ExpectCollapsed(const TodoModel& model, std::initializer_list<bool> expected) {
    std::vector<bool> actual = Collapsed(model);
    std::vector<bool> want(expected);
    EXPECT_EQ(actual, want);
}

inline void AssertListInvariants(const TodoList& list) {
    const int count = static_cast<int>(list.items.size());
    int active = 0;
    for (const TodoItem& item : list.items) {
        if (!item.done) ++active;
    }
    EXPECT_EQ(list.activeCount, active);
    EXPECT_TRUE(list.activeCount >= 0);
    EXPECT_TRUE(list.activeCount <= count);

    for (int i = 0; i < count; ++i) {
        const TodoItem& item = list.items[static_cast<size_t>(i)];
        EXPECT_TRUE(item.level >= 0);
        EXPECT_TRUE(item.level <= kMaxTodoLevel);
        if (i < list.activeCount) {
            EXPECT_FALSE(item.done);
        } else {
            EXPECT_TRUE(item.done);
        }
    }

    auto checkSection = [&](int first, int last) {
        if (first >= last) return;
        EXPECT_EQ(list.items[static_cast<size_t>(first)].level, 0);
        for (int i = first + 1; i < last; ++i) {
            EXPECT_TRUE(list.items[static_cast<size_t>(i)].level <=
                        list.items[static_cast<size_t>(i) - 1].level + 1);
        }
    };
    checkSection(0, list.activeCount);
    checkSection(list.activeCount, count);

    for (int i = 0; i < count; ++i) {
        const int limit = list.items[static_cast<size_t>(i)].done ? count : list.activeCount;
        if (list.items[static_cast<size_t>(i)].collapsed) {
            EXPECT_TRUE(LocalSubtreeEnd(list, i, limit) > i + 1);
        }
    }
}

inline void AssertInvariants(const TodoModel& model) {
    EXPECT_TRUE(model.ListCount() >= 1);
    EXPECT_TRUE(model.CurrentListIndex() >= 0);
    EXPECT_TRUE(model.CurrentListIndex() < model.ListCount());

    int totalActive = 0;
    for (const TodoList& list : model.Lists()) {
        AssertListInvariants(list);
        totalActive += list.activeCount;
    }
    EXPECT_EQ(model.TotalActiveCount(), totalActive);
}

} // namespace xtodo_test
