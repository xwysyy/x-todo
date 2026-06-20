#include "Store.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// 把换行 / 制表 / 反斜杠转义，保证一条目占一行
std::wstring Escape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': break;
            case L'\t': out += L"\\t"; break;
            default:    out += c;       break;
        }
    }
    return out;
}

std::wstring Unescape(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'\\' && i + 1 < in.size()) {
            wchar_t n = in[++i];
            if (n == L'n')       out += L'\n';
            else if (n == L't')  out += L'\t';
            else if (n == L'\\') out += L'\\';
            else                 out += n;
        } else {
            out += in[i];
        }
    }
    return out;
}

bool ReadAllBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1 << 24)) { // 上限 16MB
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    bool ok = true;
    if (!out.empty()) {
        DWORD read = 0;
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
    }
    CloseHandle(h);
    return ok;
}

bool WriteAllBytesAtomic(const std::wstring& path, const std::string& bytes) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (!bytes.empty()) {
        DWORD written = 0;
        ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &written, nullptr) && written == bytes.size();
    }
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else if (c != L'\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool StartsWith(const std::wstring& s, const wchar_t* p) {
    size_t n = wcslen(p);
    return s.size() >= n && wcsncmp(s.c_str(), p, n) == 0;
}

bool ParseIntToken(const std::wstring& token, int& value) {
    if (token.empty()) return false;
    wchar_t* end = nullptr;
    long parsed = wcstol(token.c_str(), &end, 10);
    if (end != token.c_str() + token.size()) return false;
    value = static_cast<int>(parsed);
    return true;
}

// 精确解析单条 "ui key=value" 的 body（已去掉前缀 "ui "）。
struct UiKeyValue {
    std::wstring key;
    std::wstring value;
    bool valid = false;
};

UiKeyValue ParseExactUiKeyValue(const std::wstring& body) {
    size_t eq = body.find(L'=');
    if (eq == std::wstring::npos || eq == 0) return {};
    UiKeyValue kv;
    kv.key = body.substr(0, eq);
    kv.value = body.substr(eq + 1);
    while (!kv.value.empty() && (kv.value.back() == L' ' || kv.value.back() == L'\r'))
        kv.value.pop_back();
    kv.valid = true;
    return kv;
}

// 主题 id 合法字符集校验：[A-Za-z0-9._-]，长度 3..64（与 ThemeLoader schema 一致）。
bool IsValidThemeId(const std::wstring& v) {
    if (v.size() < 3 || v.size() > 64) return false;
    for (wchar_t c : v) {
        bool ok = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                  (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-';
        if (!ok) return false;
    }
    return true;
}

} // namespace

std::wstring Store::DataFilePath() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        return L"";
    std::wstring dir = std::wstring(appdata) + L"\\x-todo";
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在返回失败但无碍
    return dir + L"\\data.txt";
}

LoadResult Store::Load(TodoModel& model, WindowGeometry& geom, UiState& ui) {
    std::wstring path = DataFilePath();
    if (path.empty()) return LoadResult::Missing;

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return LoadResult::Missing; // 首次运行，无数据文件

    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        // 文件存在但读取失败：备份原文件（不覆盖已有备份），避免之后保存把它弄丢
        CopyFileW(path.c_str(), (path + L".corrupt.bak").c_str(), TRUE);
        return LoadResult::Failed;
    }
    std::wstring text = Utf8ToWide(bytes);

    // 非空但不是本程序格式（疑似被外部写坏）：备份（不覆盖已有备份）并报失败，避免后续保存清空它
    if (!text.empty() && text.compare(0, 5, L"XTODO") != 0) {
        CopyFileW(path.c_str(), (path + L".corrupt.bak").c_str(), TRUE);
        return LoadResult::Failed;
    }

    const bool v2 = StartsWith(text, L"XTODO v2");
    const bool v3 = StartsWith(text, L"XTODO v3");
    const bool v4 = StartsWith(text, L"XTODO v4");
    const bool multiList = v2 || v3 || v4;
    std::vector<TodoItem> legacyItems;
    std::vector<TodoList> lists;
    TodoList* currentList = nullptr;
    std::string selectedListId;

    auto ensureCurrentList = [&]() -> TodoList& {
        if (!currentList) {
            TodoList list;
            list.id = "inbox";
            list.title = L"默认";
            lists.push_back(std::move(list));
            currentList = &lists.back();
        }
        return *currentList;
    };

    for (const auto& line : SplitLines(text)) {
        if (StartsWith(line, L"item ")) {
            // v2 格式：item <0|1> <escaped text>
            // v3 格式：item <0|1> <level> <escaped text>
            // v4 格式：item <0|1> <level> <collapsed> <escaped text>
            wchar_t flag = line.size() > 5 ? line[5] : L'0';
            int level = 0;
            bool collapsed = false;
            std::wstring rest;
            if (v3 || v4) {
                size_t levelStart = 7;
                size_t levelEnd = (line.size() > levelStart) ? line.find(L' ', levelStart) : std::wstring::npos;
                if (levelEnd != std::wstring::npos) {
                    int parsed = 0;
                    bool levelParsed = false;
                    if (ParseIntToken(line.substr(levelStart, levelEnd - levelStart), parsed)) {
                        level = parsed;
                        rest = line.substr(levelEnd + 1);
                        levelParsed = true;
                    } else {
                        rest = line.substr(levelStart);
                    }
                    if (v4 && levelParsed) {
                        size_t collapsedStart = levelEnd + 1;
                        size_t collapsedEnd = (line.size() > collapsedStart) ? line.find(L' ', collapsedStart) : std::wstring::npos;
                        if (collapsedEnd != std::wstring::npos) {
                            std::wstring token = line.substr(collapsedStart, collapsedEnd - collapsedStart);
                            if (token == L"0" || token == L"1") {
                                collapsed = token == L"1";
                                rest = line.substr(collapsedEnd + 1);
                            }
                        }
                    } else if (!v4) {
                        rest = line.substr(levelEnd + 1);
                    }
                } else if (line.size() > levelStart) {
                    rest = line.substr(levelStart);
                }
            } else {
                rest = line.size() >= 7 ? line.substr(7) : L"";
            }
            TodoItem it;
            it.done = (flag == L'1');
            it.level = ClampTodoLevel(level);
            it.collapsed = collapsed;
            it.text = Unescape(rest);
            if (multiList) ensureCurrentList().items.push_back(std::move(it));
            else           legacyItems.push_back(std::move(it));
        } else if (StartsWith(line, L"list ")) {
            std::wstring body = line.substr(5);
            size_t first = body.find(L' ');
            size_t second = (first == std::wstring::npos) ? std::wstring::npos : body.find(L' ', first + 1);
            if (first != std::wstring::npos && second != std::wstring::npos && second > first + 1) {
                TodoList list;
                list.id = WideToUtf8(Unescape(body.substr(0, first)));
                list.completedExpanded = body[first + 1] == L'1';
                list.title = Unescape(body.substr(second + 1));
                lists.push_back(std::move(list));
                currentList = &lists.back();
            }
        } else if (StartsWith(line, L"win ")) {
            int x = 0, y = 0, w = 0, h = 0;
            if (swscanf_s(line.c_str() + 4, L"%d %d %d %d", &x, &y, &w, &h) == 4) {
                geom = { x, y, w, h, true };
            }
        } else if (StartsWith(line, L"ui ")) {
            // 精确 key=value 分发：子串匹配会把 theme_id 误命中 light_theme_id / dark_theme_id
            UiKeyValue kv = ParseExactUiKeyValue(line.substr(3));
            if (!kv.valid) continue;
            const std::wstring& k = kv.key;
            const std::wstring& v = kv.value;
            if (k == L"completed_expanded") {
                ui.completedExpanded = (v == L"1");
            } else if (k == L"current_list") {
                selectedListId = WideToUtf8(Unescape(v));
            } else if (k == L"always_on_top") {
                ui.alwaysOnTop = (v == L"1");
            } else if (k == L"mount") {
                if (v == L"normal" || v == L"desktop" || v == L"capsule")
                    ui.mountMode = std::string(v.begin(), v.end());
                else if (v == L"taskbar")
                    ui.mountMode = "normal"; // 旧版任务栏模式已移除，回退普通窗口
            } else if (k == L"lang") {
                if (v == L"zh" || v == L"en")
                    ui.lang = std::string(v.begin(), v.end());
            } else if (k == L"theme_mode") {
                if (v == L"builtin" || v == L"custom" || v == L"follow_system")
                    ui.themeMode = std::string(v.begin(), v.end());
            } else if (k == L"theme_id") {
                if (IsValidThemeId(v)) ui.themeId = std::string(v.begin(), v.end());
            } else if (k == L"light_theme_id") {
                if (IsValidThemeId(v)) ui.lightThemeId = std::string(v.begin(), v.end());
            } else if (k == L"dark_theme_id") {
                if (IsValidThemeId(v)) ui.darkThemeId = std::string(v.begin(), v.end());
            } else if (k == L"capsule_style") {
                if (v == L"slim" || v == L"dot")
                    ui.capsuleStyle = std::string(v.begin(), v.end());
            } else if (k == L"capsule_dock_edge") {
                if (v == L"left" || v == L"right")
                    ui.capsuleDockEdge = std::string(v.begin(), v.end());
            } else if (k == L"capsule_dock_t") {
                wchar_t* end = nullptr;
                double d = wcstod(v.c_str(), &end);
                if (end != v.c_str() && std::isfinite(d))
                    ui.capsuleDockT = std::clamp(d, 0.0, 1.0);
            } else if (k == L"capsule_monitor") {
                ui.capsuleMonitor = WideToUtf8(Unescape(v));
            }
            // 未知 ui key：忽略，保留默认值，不触发 corrupt 备份路径
        }
    }
    if (multiList) model.ReplaceLists(std::move(lists), selectedListId);
    else           model.ReplaceAll(std::move(legacyItems), ui.completedExpanded);
    return LoadResult::Loaded;
}

bool Store::Save(const TodoModel& model, const WindowGeometry& geom, const UiState& ui) {
    std::wstring path = DataFilePath();
    if (path.empty()) return false;

    std::wstring text = L"XTODO v4\n";
    if (geom.valid) {
        wchar_t buf[128];
        swprintf_s(buf, L"win %d %d %d %d\n", geom.x, geom.y, geom.w, geom.h);
        text += buf;
    }
    text += ui.alwaysOnTop ? L"ui always_on_top=1\n"
                           : L"ui always_on_top=0\n";
    {
        std::wstring mw(ui.mountMode.begin(), ui.mountMode.end()); // 仅 ASCII，安全
        text += L"ui mount=" + mw + L"\n";
    }
    if (!ui.lang.empty()) {
        std::wstring lw(ui.lang.begin(), ui.lang.end());
        text += L"ui lang=" + lw + L"\n";
    }
    // 主题字段（id 仅含 ASCII [A-Za-z0-9._-]，begin/end 转宽字符安全）
    text += L"ui theme_mode=" + std::wstring(ui.themeMode.begin(), ui.themeMode.end()) + L"\n";
    text += L"ui theme_id=" + std::wstring(ui.themeId.begin(), ui.themeId.end()) + L"\n";
    text += L"ui light_theme_id=" + std::wstring(ui.lightThemeId.begin(), ui.lightThemeId.end()) + L"\n";
    text += L"ui dark_theme_id=" + std::wstring(ui.darkThemeId.begin(), ui.darkThemeId.end()) + L"\n";
    text += L"ui capsule_style=" + std::wstring(ui.capsuleStyle.begin(), ui.capsuleStyle.end()) + L"\n";
    text += L"ui capsule_dock_edge=" + std::wstring(ui.capsuleDockEdge.begin(), ui.capsuleDockEdge.end()) + L"\n";
    {
        wchar_t buf[64];
        swprintf_s(buf, L"ui capsule_dock_t=%.6f\n", ui.capsuleDockT);
        text += buf;
    }
    if (!ui.capsuleMonitor.empty())
        text += L"ui capsule_monitor=" + Escape(Utf8ToWide(ui.capsuleMonitor)) + L"\n";

    const TodoList& current = model.CurrentList();
    text += L"ui current_list=" + Escape(Utf8ToWide(current.id)) + L"\n";

    for (const auto& list : model.Lists()) {
        text += L"list ";
        text += Escape(Utf8ToWide(list.id));
        text += list.completedExpanded ? L" 1 " : L" 0 ";
        text += Escape(list.title);
        text += L"\n";
        for (const auto& it : list.items) {
            text += L"item ";
            text += it.done ? L"1 " : L"0 ";
            wchar_t levelBuf[16];
            swprintf_s(levelBuf, L"%d %d ", ClampTodoLevel(it.level), it.collapsed ? 1 : 0);
            text += levelBuf;
            text += Escape(it.text);
            text += L"\n";
        }
    }
    return WriteAllBytesAtomic(path, WideToUtf8(text));
}
