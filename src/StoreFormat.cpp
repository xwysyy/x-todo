#include "StoreFormat.h"

#include <algorithm>
#include <cmath>
#include <codecvt>
#include <locale>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace StoreFormat {
namespace {

constexpr int kMaxNestDepth = 32; // 合法数据嵌套 <= 4，防篡改/损坏文件让递归解析栈溢出

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    try {
#ifdef _WIN32
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
#endif
        return conv.from_bytes(s);
    } catch (...) {
        return std::wstring(s.begin(), s.end());
    }
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    try {
#ifdef _WIN32
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
#endif
        return conv.to_bytes(w);
    } catch (...) {
        return std::string(w.begin(), w.end());
    }
}

// parse 前字符级扫描最大括号嵌套深度（不递归），挡掉深嵌套 JSON，
// 避免 nlohmann 递归下降解析栈溢出。镜像 ThemeLoader 的同名守护。
int MaxBracketDepth(const std::string& s) {
    int depth = 0, maxDepth = 0;
    bool inStr = false, esc = false;
    for (char c : s) {
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
        } else if (c == '"') {
            inStr = true;
        } else if (c == '{' || c == '[') {
            if (++depth > maxDepth) maxDepth = depth;
        } else if (c == '}' || c == ']') {
            if (depth > 0) --depth;
        }
    }
    return maxDepth;
}

bool IsValidThemeId(const std::string& v) {
    if (v.size() < 3 || v.size() > 64) return false;
    for (char c : v) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// 防御性取值：损坏但可解析的 JSON 不得在读取中途抛异常，缺失/类型不符回退默认。
bool GetBool(const json& o, const char* key, bool def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
}

int GetInt(const json& o, const char* key, int def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_number_integer()) ? it->get<int>() : def;
}

long long GetLongLong(const json& o, const char* key, long long def) {
    auto it = o.find(key);
    return (it != o.end() && it->is_number_integer()) ? it->get<long long>() : def;
}

std::string GetUtf8(const json& o, const char* key) {
    auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::wstring GetWide(const json& o, const char* key) {
    return Utf8ToWide(GetUtf8(o, key));
}

} // namespace

bool Parse(const std::string& utf8, TodoModel& model, CalendarModel& calendar,
           WindowGeometry& geom, UiState& ui) {
    // 空文件或纯空白：当作首次运行，以安全默认值启动并视为成功。
    const bool blank = std::all_of(utf8.begin(), utf8.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
    if (blank) {
        model.ReplaceLists({}, "");
        calendar.ReplaceBlocks({});
        return true;
    }

    if (MaxBracketDepth(utf8) > kMaxNestDepth) return false;

    json root = json::parse(utf8, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) return false;

    auto winIt = root.find("window");
    if (winIt != root.end() && winIt->is_object()) {
        const json& w = *winIt;
        geom = { GetInt(w, "x", 0), GetInt(w, "y", 0),
                 GetInt(w, "w", 0), GetInt(w, "h", 0), true };
    }

    std::string selectedListId;
    auto uiIt = root.find("ui");
    if (uiIt != root.end() && uiIt->is_object()) {
        const json& u = *uiIt;
        ui.alwaysOnTop = GetBool(u, "alwaysOnTop", ui.alwaysOnTop);
        {
            std::string v = GetUtf8(u, "mount");
            if (v == "normal" || v == "desktop" || v == "capsule") ui.mountMode = v;
            else if (v == "taskbar") ui.mountMode = "normal";
        }
        {
            std::string v = GetUtf8(u, "lang");
            if (v == "zh" || v == "en") ui.lang = v;
        }
        {
            std::string v = GetUtf8(u, "themeMode");
            if (v == "builtin" || v == "custom" || v == "follow_system") ui.themeMode = v;
        }
        { std::string v = GetUtf8(u, "themeId");      if (IsValidThemeId(v)) ui.themeId = v; }
        { std::string v = GetUtf8(u, "lightThemeId"); if (IsValidThemeId(v)) ui.lightThemeId = v; }
        { std::string v = GetUtf8(u, "darkThemeId");  if (IsValidThemeId(v)) ui.darkThemeId = v; }
        {
            std::string v = GetUtf8(u, "capsuleStyle");
            if (v == "slim" || v == "dot" || v == "bar" || v == "pip") ui.capsuleStyle = v;
        }
        {
            std::string v = GetUtf8(u, "capsuleDockEdge");
            if (v == "left" || v == "right") ui.capsuleDockEdge = v;
        }
        {
            auto it = u.find("capsuleDockT");
            if (it != u.end() && it->is_number()) {
                double d = it->get<double>();
                if (std::isfinite(d)) ui.capsuleDockT = std::clamp(d, 0.0, 1.0);
            }
        }
        {
            auto it = u.find("capsuleMonitor");
            if (it != u.end() && it->is_string()) ui.capsuleMonitor = it->get<std::string>();
        }
        {
            std::string v = GetUtf8(u, "activeView");
            if (v == "list" || v == "calendar") ui.activeView = v;
        }
        {
            std::string v = GetUtf8(u, "calendarDay");
            if (IsValidCalendarDayKey(v)) ui.calendarDay = v;
        }
        {
            std::string v = GetUtf8(u, "calendarView");
            if (v == "day") ui.calendarView = CalendarViewMode::Day;
            else if (v == "week") ui.calendarView = CalendarViewMode::Week;
            else if (v == "month") ui.calendarView = CalendarViewMode::Month;
        }
        ui.backupDir = GetWide(u, "backupDir");
        {
            long long ts = GetLongLong(u, "backupLastEpoch", 0);
            ui.backupLastEpoch = ts > 0 ? ts : 0;
        }
        selectedListId = GetUtf8(u, "currentList");
    }

    std::vector<CalendarBlock> blocks;
    auto calIt = root.find("calendar");
    if (calIt != root.end() && calIt->is_array()) {
        for (const json& b : *calIt) {
            if (!b.is_object()) continue;
            std::string day = GetUtf8(b, "day");
            if (!IsValidCalendarDayKey(day)) continue; // 其余归一化（id 去重、分钟 clamp）交 ReplaceBlocks
            CalendarBlock block;
            block.id = GetInt(b, "id", 0);
            block.day = day;
            block.startMinute = GetInt(b, "start", 0);
            block.endMinute = GetInt(b, "end", 0);
            block.title = GetWide(b, "title");
            blocks.push_back(std::move(block));
        }
    }

    std::vector<TodoList> lists;
    auto listsIt = root.find("lists");
    if (listsIt != root.end() && listsIt->is_array()) {
        for (const json& l : *listsIt) {
            if (!l.is_object()) continue;
            TodoList list;
            list.id = GetUtf8(l, "id");
            list.title = GetWide(l, "title");
            list.completedExpanded = GetBool(l, "completedExpanded", false);
            auto itemsIt = l.find("items");
            if (itemsIt != l.end() && itemsIt->is_array()) {
                for (const json& it : *itemsIt) {
                    if (!it.is_object()) continue;
                    TodoItem item;
                    item.text = GetWide(it, "text");
                    item.done = GetBool(it, "done", false);
                    item.level = ClampTodoLevel(GetInt(it, "level", 0));
                    item.collapsed = GetBool(it, "collapsed", false);
                    list.items.push_back(std::move(item));
                }
            }
            lists.push_back(std::move(list)); // 排序、activeCount、层级与折叠归一化交 ReplaceLists
        }
    }

    model.ReplaceLists(std::move(lists), selectedListId);
    calendar.ReplaceBlocks(std::move(blocks));
    return true;
}

std::string Serialize(const TodoModel& model, const CalendarModel& calendar,
                      const WindowGeometry& geom, const UiState& ui) {
    json root = json::object();

    if (geom.valid) {
        json w = json::object();
        w["x"] = geom.x;
        w["y"] = geom.y;
        w["w"] = geom.w;
        w["h"] = geom.h;
        root["window"] = std::move(w);
    }

    json u = json::object();
    u["alwaysOnTop"] = ui.alwaysOnTop;
    u["mount"] = ui.mountMode;
    if (!ui.lang.empty()) u["lang"] = ui.lang;
    u["themeMode"] = ui.themeMode;
    u["themeId"] = ui.themeId;
    u["lightThemeId"] = ui.lightThemeId;
    u["darkThemeId"] = ui.darkThemeId;
    u["capsuleStyle"] = ui.capsuleStyle;
    u["capsuleDockEdge"] = ui.capsuleDockEdge;
    u["capsuleDockT"] = ui.capsuleDockT;
    if (!ui.capsuleMonitor.empty()) u["capsuleMonitor"] = ui.capsuleMonitor;
    u["activeView"] = (ui.activeView == "calendar") ? "calendar" : "list";
    if (IsValidCalendarDayKey(ui.calendarDay)) u["calendarDay"] = ui.calendarDay;
    u["calendarView"] = (ui.calendarView == CalendarViewMode::Week)    ? "week"
                        : (ui.calendarView == CalendarViewMode::Month) ? "month"
                                                                       : "day";
    if (!ui.backupDir.empty()) u["backupDir"] = WideToUtf8(ui.backupDir);
    if (ui.backupLastEpoch > 0) u["backupLastEpoch"] = ui.backupLastEpoch;
    u["currentList"] = model.CurrentList().id;
    root["ui"] = std::move(u);

    json cal = json::array();
    for (const CalendarBlock& block : calendar.Blocks()) {
        if (!IsValidCalendarDayKey(block.day)) continue;
        json b = json::object();
        b["id"] = block.id;
        b["day"] = block.day;
        b["start"] = ClampCalendarMinute(block.startMinute);
        b["end"] = ClampCalendarMinute(block.endMinute);
        b["title"] = WideToUtf8(block.title);
        cal.push_back(std::move(b));
    }
    root["calendar"] = std::move(cal);

    json lists = json::array();
    for (const TodoList& list : model.Lists()) {
        json items = json::array();
        for (const TodoItem& it : list.items) {
            json item = json::object();
            item["text"] = WideToUtf8(it.text);
            item["done"] = it.done;
            item["level"] = ClampTodoLevel(it.level);
            item["collapsed"] = it.collapsed;
            items.push_back(std::move(item));
        }
        json l = json::object();
        l["id"] = list.id;
        l["title"] = WideToUtf8(list.title);
        l["completedExpanded"] = list.completedExpanded;
        l["items"] = std::move(items);
        lists.push_back(std::move(l));
    }
    root["lists"] = std::move(lists);

    return root.dump(2);
}

} // namespace StoreFormat
