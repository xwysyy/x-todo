#include "TodoModel.h"
#include <algorithm>
#include <cstdio>

namespace {
constexpr const char* kDefaultListId = "inbox";
constexpr const wchar_t* kDefaultListTitle = L"默认";

int SubtreeEndInList(const TodoList& list, int index, int limit) {
    const int count = static_cast<int>(list.items.size());
    if (index < 0 || index >= count) return index;
    if (limit < index + 1) limit = index + 1;
    if (limit > count) limit = count;

    const int baseLevel = list.items[(size_t)index].level;
    int end = index + 1;
    while (end < limit && list.items[(size_t)end].level > baseLevel)
        ++end;
    return end;
}

void NormalizeLevels(std::vector<TodoItem>& items, int first, int last) {
    if (first < 0) first = 0;
    if (last > static_cast<int>(items.size())) last = static_cast<int>(items.size());
    for (int i = first; i < last; ++i) {
        int maxHere = (i == first) ? 0 : items[(size_t)i - 1].level + 1;
        int level = ClampTodoLevel(items[(size_t)i].level);
        if (level > maxHere) level = maxHere;
        items[(size_t)i].level = ClampTodoLevel(level);
    }
}

void NormalizeCollapsed(TodoList& list) {
    const int count = static_cast<int>(list.items.size());
    for (int i = 0; i < count; ++i) {
        int limit = list.items[(size_t)i].done ? count : list.activeCount;
        if (SubtreeEndInList(list, i, limit) <= i + 1)
            list.items[(size_t)i].collapsed = false;
    }
}

void RebaseBlockToRoot(std::vector<TodoItem>& block) {
    if (block.empty()) return;
    int baseLevel = block.front().level;
    for (TodoItem& item : block)
        item.level = ClampTodoLevel(item.level - baseLevel);
}
}

TodoModel::TodoModel() {
    EnsureList();
}

const TodoList& TodoModel::CurrentList() const {
    return lists_[currentList_];
}

TodoList& TodoModel::CurrentListMutable() {
    EnsureList();
    return lists_[currentList_];
}

const TodoList* TodoModel::ListAt(int index) const {
    if (index < 0 || index >= static_cast<int>(lists_.size())) return nullptr;
    return &lists_[(size_t)index];
}

int TodoModel::ActiveCount() const {
    return CurrentList().activeCount;
}

int TodoModel::CompletedCount() const {
    const TodoList& list = CurrentList();
    return static_cast<int>(list.items.size()) - list.activeCount;
}

int TodoModel::TotalActiveCount() const {
    int total = 0;
    for (const TodoList& list : lists_) total += list.activeCount;
    return total;
}

int TodoModel::AddActive(const std::wstring& text, int level) {
    TodoList& list = CurrentListMutable();
    return InsertActive(list.activeCount, text, level);
}

int TodoModel::InsertActive(int index, const std::wstring& text, int level) {
    TodoList& list = CurrentListMutable();
    int at = index;
    if (at < 0) at = 0;
    if (at > list.activeCount) at = list.activeCount;
    if (level < 0)
        level = (at > 0) ? list.items[(size_t)at - 1].level : 0;
    list.items.insert(list.items.begin() + at, TodoItem{ text, false, ClampTodoLevel(level), false });
    ++list.activeCount;
    NormalizeLevels(list.items, 0, list.activeCount);
    NormalizeCollapsed(list);
    return at;
}

void TodoModel::SetText(int index, const std::wstring& text) {
    TodoList& list = CurrentListMutable();
    if (index < 0 || index >= static_cast<int>(list.items.size())) return;
    list.items[(size_t)index].text = text;
}

void TodoModel::Remove(int index) {
    TodoList& list = CurrentListMutable();
    if (index < 0 || index >= static_cast<int>(list.items.size())) return;
    int limit = list.items[(size_t)index].done ? static_cast<int>(list.items.size()) : list.activeCount;
    int end = SubtreeEndInList(list, index, limit);
    int activeRemoved = 0;
    for (int i = index; i < end; ++i)
        if (!list.items[(size_t)i].done) ++activeRemoved;
    list.items.erase(list.items.begin() + index, list.items.begin() + end);
    list.activeCount -= activeRemoved;
    if (list.activeCount < 0) list.activeCount = 0;
    NormalizeCollapsed(list);
}

void TodoModel::SetDone(int index, bool done) {
    TodoList& list = CurrentListMutable();
    if (index < 0 || index >= static_cast<int>(list.items.size())) return;
    int limit = list.items[(size_t)index].done ? static_cast<int>(list.items.size()) : list.activeCount;
    int end = SubtreeEndInList(list, index, limit);
    bool changed = false;
    int activeBefore = 0;
    for (int i = index; i < end; ++i) {
        if (list.items[(size_t)i].done != done) changed = true;
        if (!list.items[(size_t)i].done) ++activeBefore;
    }
    if (!changed) return;

    std::vector<TodoItem> block(list.items.begin() + index, list.items.begin() + end);
    for (TodoItem& item : block)
        item.done = done;
    RebaseBlockToRoot(block);
    list.items.erase(list.items.begin() + index, list.items.begin() + end);
    list.activeCount -= activeBefore;
    if (list.activeCount < 0) list.activeCount = 0;

    int insertAt = list.activeCount;
    if (done) {
        list.items.insert(list.items.begin() + insertAt, block.begin(), block.end());
    } else {
        list.items.insert(list.items.begin() + insertAt, block.begin(), block.end());
        list.activeCount += static_cast<int>(block.size());
    }
    NormalizeLevels(list.items, 0, list.activeCount);
    NormalizeLevels(list.items, list.activeCount, static_cast<int>(list.items.size()));
    NormalizeCollapsed(list);
}

void TodoModel::ClearCompleted() {
    TodoList& list = CurrentListMutable();
    list.items.erase(list.items.begin() + list.activeCount, list.items.end());
}

bool TodoModel::MoveActive(int from, int insertAt) {
    TodoList& list = CurrentListMutable();
    if (from < 0 || from >= list.activeCount) return false;
    int end = SubtreeEndInList(list, from, list.activeCount);
    int blockSize = end - from;
    if (blockSize <= 0) return false;

    if (insertAt < 0) insertAt = 0;
    if (insertAt > list.activeCount) insertAt = list.activeCount;
    if (insertAt >= from && insertAt <= end) return false;

    std::vector<TodoItem> block(list.items.begin() + from, list.items.begin() + end);
    list.items.erase(list.items.begin() + from, list.items.begin() + end);
    if (insertAt > end) insertAt -= blockSize;
    list.items.insert(list.items.begin() + insertAt, block.begin(), block.end());
    NormalizeLevels(list.items, 0, list.activeCount);
    NormalizeCollapsed(list);
    return true;
}

bool TodoModel::IndentItemUnder(int index, int parentIndex) {
    TodoList& list = CurrentListMutable();
    if (index <= 0 || index >= list.activeCount) return false;
    if (parentIndex < 0 || parentIndex >= index || parentIndex >= list.activeCount) return false;

    int current = list.items[(size_t)index].level;
    int target = current + 1;
    int maxByParent = list.items[(size_t)parentIndex].level + 1;
    if (target > maxByParent) target = maxByParent;
    target = ClampTodoLevel(target);
    if (target <= current) return false;

    int end = SubtreeEndInList(list, index, list.activeCount);
    int delta = target - current;
    list.items[(size_t)parentIndex].collapsed = false;
    for (int i = index; i < end; ++i)
        list.items[(size_t)i].level = ClampTodoLevel(list.items[(size_t)i].level + delta);
    NormalizeCollapsed(list);
    return true;
}

bool TodoModel::OutdentItem(int index) {
    TodoList& list = CurrentListMutable();
    if (index < 0 || index >= list.activeCount) return false;
    if (list.items[(size_t)index].level <= 0) return false;

    int end = SubtreeEndInList(list, index, list.activeCount);
    for (int i = index; i < end; ++i)
        list.items[(size_t)i].level = ClampTodoLevel(list.items[(size_t)i].level - 1);
    NormalizeCollapsed(list);
    return true;
}

bool TodoModel::HasChildren(int index) const {
    return SubtreeEnd(index) > index + 1;
}

bool TodoModel::ToggleCollapsed(int index) {
    TodoList& list = CurrentListMutable();
    if (index < 0 || index >= static_cast<int>(list.items.size())) return false;
    int limit = list.items[(size_t)index].done ? static_cast<int>(list.items.size()) : list.activeCount;
    if (SubtreeEndInList(list, index, limit) <= index + 1) {
        list.items[(size_t)index].collapsed = false;
        return false;
    }
    list.items[(size_t)index].collapsed = !list.items[(size_t)index].collapsed;
    return true;
}

int TodoModel::SubtreeEnd(int index) const {
    const TodoList& list = CurrentList();
    if (index < 0 || index >= static_cast<int>(list.items.size())) return index;
    int limit = list.items[(size_t)index].done ? static_cast<int>(list.items.size()) : list.activeCount;
    return SubtreeEndInList(list, index, limit);
}

int TodoModel::AddList(const std::wstring& title) {
    EnsureList();
    TodoList list;
    list.id = MakeListId();
    list.title = title.empty() ? kDefaultListTitle : title;
    list.completedExpanded = false;
    list.activeCount = 0;
    lists_.push_back(std::move(list));
    currentList_ = static_cast<int>(lists_.size()) - 1;
    return currentList_;
}

bool TodoModel::SetCurrentListIndex(int index) {
    EnsureList();
    if (index < 0 || index >= static_cast<int>(lists_.size())) return false;
    currentList_ = index;
    return true;
}

bool TodoModel::SetCurrentListId(const std::string& id) {
    EnsureList();
    for (size_t i = 0; i < lists_.size(); ++i) {
        if (lists_[i].id == id) {
            currentList_ = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

void TodoModel::SetCurrentCompletedExpanded(bool expanded) {
    CurrentListMutable().completedExpanded = expanded;
}

void TodoModel::ReplaceAll(std::vector<TodoItem> items, bool completedExpanded) {
    TodoList list;
    list.id = kDefaultListId;
    list.title = kDefaultListTitle;
    list.completedExpanded = completedExpanded;
    list.items = std::move(items);
    Normalize(list);

    lists_.clear();
    lists_.push_back(std::move(list));
    currentList_ = 0;
    nextListId_ = 1;
}

void TodoModel::ReplaceLists(std::vector<TodoList> lists, const std::string& currentId) {
    lists_.clear();
    for (TodoList& list : lists) {
        if (list.id.empty()) list.id = MakeListId();
        if (list.title.empty()) list.title = kDefaultListTitle;
        Normalize(list);
        lists_.push_back(std::move(list));
    }
    EnsureList();
    currentList_ = 0;
    if (!currentId.empty()) SetCurrentListId(currentId);
}

void TodoModel::EnsureList() {
    if (lists_.empty()) {
        TodoList list;
        list.id = kDefaultListId;
        list.title = kDefaultListTitle;
        lists_.push_back(std::move(list));
    }
    if (currentList_ < 0 || currentList_ >= static_cast<int>(lists_.size()))
        currentList_ = 0;

    int maxSeen = 0;
    for (const TodoList& list : lists_) {
        if (list.id.rfind("list-", 0) == 0) {
            int n = 0;
            if (std::sscanf(list.id.c_str() + 5, "%d", &n) == 1 && n > maxSeen)
                maxSeen = n;
        }
    }
    if (nextListId_ <= maxSeen) nextListId_ = maxSeen + 1;
}

void TodoModel::Normalize(TodoList& list) {
    for (TodoItem& item : list.items)
        item.level = ClampTodoLevel(item.level);
    auto mid = std::stable_partition(list.items.begin(), list.items.end(),
        [](const TodoItem& it) { return !it.done; });
    list.activeCount = static_cast<int>(mid - list.items.begin());
    NormalizeLevels(list.items, 0, list.activeCount);
    NormalizeLevels(list.items, list.activeCount, static_cast<int>(list.items.size()));
    NormalizeCollapsed(list);
}

std::string TodoModel::MakeListId() {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "list-%d", nextListId_);
    nextListId_++;
    return std::string(buf);
}
