#pragma once
#include <string>
#include <vector>

struct TodoItem {
    std::wstring text;
    bool done = false;
};

// 待办数据。不变量：未完成项始终排在前段，已完成项排在后段。
class TodoModel {
public:
    const std::vector<TodoItem>& Items() const { return items_; }
    int Count() const { return static_cast<int>(items_.size()); }
    int ActiveCount() const;    // 未完成数量（= 已完成段起始下标）
    int CompletedCount() const; // 已完成数量

    int  AddActive(const std::wstring& text); // 未完成段末尾追加，返回其下标
    void SetText(int index, const std::wstring& text);
    void Remove(int index);
    void SetDone(int index, bool done);       // 勾选 / 还原，并重排到对应段
    void ClearCompleted();
    void MoveActive(int from, int insertAt);  // 未完成段内重排：移除 from 后插入到 insertAt

    void ReplaceAll(std::vector<TodoItem> items); // 加载时整体替换（会规整顺序）

private:
    void Normalize(); // 稳定地把未完成项移到前段，维持不变量
    std::vector<TodoItem> items_;
    int activeCount_ = 0; // 未完成数量（已完成段起始下标），随增删改维护，避免每次全量扫描
};
