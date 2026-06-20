#pragma once
#include <string>
#include <vector>

struct TodoItem {
    std::wstring text;
    bool done = false;
    int level = 0;
    bool collapsed = false;
};

struct TodoList {
    std::string id;
    std::wstring title;
    bool completedExpanded = false;
    std::vector<TodoItem> items;
    int activeCount = 0;
};

inline constexpr int kMaxTodoLevel = 3;

inline int ClampTodoLevel(int level) {
    if (level < 0) return 0;
    if (level > kMaxTodoLevel) return kMaxTodoLevel;
    return level;
}

// 待办数据。每个列表内的不变量：未完成项始终排在前段，已完成项排在后段。
class TodoModel {
public:
    TodoModel();

    const std::vector<TodoItem>& Items() const { return CurrentList().items; }
    int Count() const { return static_cast<int>(CurrentList().items.size()); }
    int ActiveCount() const;    // 未完成数量（= 已完成段起始下标）
    int CompletedCount() const; // 已完成数量
    int TotalActiveCount() const;

    int  AddActive(const std::wstring& text, int level = -1); // 未完成段末尾追加，返回其下标
    void SetText(int index, const std::wstring& text);
    void Remove(int index);                   // 删除该项及其子树
    void SetDone(int index, bool done);       // 勾选 / 还原该项及其子树，以独立块重排到对应段
    void ClearCompleted();
    bool MoveActive(int from, int insertAt);  // 未完成段内移动整棵子树，insertAt 为移动前的插入边界
    bool IndentItemUnder(int index, int parentIndex);
    bool OutdentItem(int index);
    bool HasChildren(int index) const;
    bool ToggleCollapsed(int index);
    int  SubtreeEnd(int index) const;

    const std::vector<TodoList>& Lists() const { return lists_; }
    int ListCount() const { return static_cast<int>(lists_.size()); }
    int CurrentListIndex() const { return currentList_; }
    const TodoList& CurrentList() const;
    const TodoList* ListAt(int index) const;
    int AddList(const std::wstring& title);
    bool SetCurrentListIndex(int index);
    bool SetCurrentListId(const std::string& id);
    void SetCurrentCompletedExpanded(bool expanded);

    void ReplaceAll(std::vector<TodoItem> items, bool completedExpanded = false); // 旧单列表迁移入口
    void ReplaceLists(std::vector<TodoList> lists, const std::string& currentId);

private:
    TodoList& CurrentListMutable();
    void EnsureList();
    void Normalize(TodoList& list); // 稳定地把未完成项移到前段，维持不变量
    std::string MakeListId();
    std::vector<TodoList> lists_;
    int currentList_ = 0;
    int nextListId_ = 1;
};
