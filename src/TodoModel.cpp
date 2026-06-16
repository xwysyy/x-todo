#include "TodoModel.h"
#include <algorithm>

int TodoModel::ActiveCount() const {
    int n = 0;
    for (const auto& it : items_)
        if (!it.done) ++n;
    return n;
}

int TodoModel::CompletedCount() const {
    return static_cast<int>(items_.size()) - ActiveCount();
}

int TodoModel::AddActive(const std::wstring& text) {
    int at = ActiveCount();
    items_.insert(items_.begin() + at, TodoItem{ text, false });
    return at;
}

void TodoModel::SetText(int index, const std::wstring& text) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    items_[index].text = text;
}

void TodoModel::Remove(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    items_.erase(items_.begin() + index);
}

void TodoModel::SetDone(int index, bool done) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    if (items_[index].done == done) return;

    TodoItem item = items_[index];
    item.done = done;
    items_.erase(items_.begin() + index);
    // 移除后，新的未完成数量正好是目标插入点：
    //   勾选 -> 落在已完成段最前；还原 -> 落在未完成段最后。
    int at = ActiveCount();
    items_.insert(items_.begin() + at, item);
}

void TodoModel::ClearCompleted() {
    int at = ActiveCount();
    items_.erase(items_.begin() + at, items_.end());
}

void TodoModel::MoveActive(int from, int insertAt) {
    int active = ActiveCount();
    if (from < 0 || from >= active) return;

    TodoItem item = items_[from];
    items_.erase(items_.begin() + from);

    int maxAt = ActiveCount(); // 移除后未完成段长度
    if (insertAt < 0) insertAt = 0;
    if (insertAt > maxAt) insertAt = maxAt;
    items_.insert(items_.begin() + insertAt, item);
}

void TodoModel::ReplaceAll(std::vector<TodoItem> items) {
    items_ = std::move(items);
    Normalize();
}

void TodoModel::Normalize() {
    std::stable_partition(items_.begin(), items_.end(),
        [](const TodoItem& it) { return !it.done; });
}
