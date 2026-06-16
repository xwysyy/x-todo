#include "TodoModel.h"
#include <algorithm>

int TodoModel::ActiveCount() const {
    return activeCount_;
}

int TodoModel::CompletedCount() const {
    return static_cast<int>(items_.size()) - activeCount_;
}

int TodoModel::AddActive(const std::wstring& text) {
    int at = activeCount_;
    items_.insert(items_.begin() + at, TodoItem{ text, false });
    ++activeCount_;
    return at;
}

void TodoModel::SetText(int index, const std::wstring& text) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    items_[index].text = text;
}

void TodoModel::Remove(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    if (!items_[index].done) --activeCount_; // 移除未完成项时收缩未完成段
    items_.erase(items_.begin() + index);
}

void TodoModel::SetDone(int index, bool done) {
    if (index < 0 || index >= static_cast<int>(items_.size())) return;
    if (items_[index].done == done) return;

    TodoItem item = items_[index];
    item.done = done;
    items_.erase(items_.begin() + index);
    // 移除后按目标状态调整未完成段长度，插入点恰落在未完成段末 / 已完成段首：
    //   勾选 -> 未完成段 -1，插入到已完成段最前；还原 -> 插入到未完成段末再 +1。
    if (done) {
        --activeCount_;
        items_.insert(items_.begin() + activeCount_, item);
    } else {
        items_.insert(items_.begin() + activeCount_, item);
        ++activeCount_;
    }
}

void TodoModel::ClearCompleted() {
    items_.erase(items_.begin() + activeCount_, items_.end());
}

void TodoModel::MoveActive(int from, int insertAt) {
    if (from < 0 || from >= activeCount_) return;

    TodoItem item = items_[from];
    items_.erase(items_.begin() + from);

    int maxAt = activeCount_ - 1; // 移除 from 后未完成段长度
    if (insertAt < 0) insertAt = 0;
    if (insertAt > maxAt) insertAt = maxAt;
    items_.insert(items_.begin() + insertAt, item);
}

void TodoModel::ReplaceAll(std::vector<TodoItem> items) {
    items_ = std::move(items);
    Normalize();
}

void TodoModel::Normalize() {
    auto mid = std::stable_partition(items_.begin(), items_.end(),
        [](const TodoItem& it) { return !it.done; });
    activeCount_ = static_cast<int>(mid - items_.begin());
}
