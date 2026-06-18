#include "Store.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

bool IsTaskbarStrategy(const std::wstring& v) {
    return v == L"popup_shell_noowner" ||
           v == L"popup_shell_owner" ||
           v == L"child_shell_setparent" ||
           v == L"create_child_shell" ||
           v == L"popup_traynotify" ||
           v == L"child_traynotify" ||
           v == L"popup_taskhost" ||
           v == L"topmost_overlay" ||
           v == L"appbar_edge" ||
           v == L"trafficmonitor_layered_shell";
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

    std::vector<TodoItem> items;
    for (const auto& line : SplitLines(text)) {
        if (StartsWith(line, L"item ")) {
            // 格式：item <0|1> <escaped text>
            wchar_t flag = line.size() > 5 ? line[5] : L'0';
            std::wstring rest = line.size() >= 7 ? line.substr(7) : L"";
            TodoItem it;
            it.done = (flag == L'1');
            it.text = Unescape(rest);
            items.push_back(std::move(it));
        } else if (StartsWith(line, L"win ")) {
            int x = 0, y = 0, w = 0, h = 0;
            if (swscanf_s(line.c_str() + 4, L"%d %d %d %d", &x, &y, &w, &h) == 4) {
                geom = { x, y, w, h, true };
            }
        } else if (StartsWith(line, L"ui ")) {
            if (line.find(L"completed_expanded=1") != std::wstring::npos)
                ui.completedExpanded = true;
            else if (line.find(L"completed_expanded=0") != std::wstring::npos)
                ui.completedExpanded = false;
            if (line.find(L"always_on_top=1") != std::wstring::npos)
                ui.alwaysOnTop = true;
            else if (line.find(L"always_on_top=0") != std::wstring::npos)
                ui.alwaysOnTop = false;
            size_t mp = line.find(L"mount=");
            if (mp != std::wstring::npos) {
                std::wstring v = line.substr(mp + 6);
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                if (v == L"normal" || v == L"desktop" || v == L"capsule" || v == L"taskbar")
                    ui.mountMode = std::string(v.begin(), v.end());
            }
            size_t lp = line.find(L"lang=");
            if (lp != std::wstring::npos) {
                std::wstring v = line.substr(lp + 5);
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                if (v == L"zh" || v == L"en")
                    ui.lang = std::string(v.begin(), v.end());
            }
            size_t sp = line.find(L"capsule_style=");
            if (sp != std::wstring::npos) {
                std::wstring v = line.substr(sp + wcslen(L"capsule_style="));
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                if (v == L"slim" || v == L"dot")
                    ui.capsuleStyle = std::string(v.begin(), v.end());
            }
            size_t ep = line.find(L"capsule_dock_edge=");
            if (ep != std::wstring::npos) {
                std::wstring v = line.substr(ep + wcslen(L"capsule_dock_edge="));
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                if (v == L"left" || v == L"right")
                    ui.capsuleDockEdge = std::string(v.begin(), v.end());
            }
            size_t tp = line.find(L"capsule_dock_t=");
            if (tp != std::wstring::npos) {
                std::wstring v = line.substr(tp + wcslen(L"capsule_dock_t="));
                wchar_t* end = nullptr;
                double d = wcstod(v.c_str(), &end);
                if (end != v.c_str() && std::isfinite(d))
                    ui.capsuleDockT = std::clamp(d, 0.0, 1.0);
            }
            size_t cp = line.find(L"capsule_monitor=");
            if (cp != std::wstring::npos) {
                std::wstring v = line.substr(cp + wcslen(L"capsule_monitor="));
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                ui.capsuleMonitor = WideToUtf8(Unescape(v));
            }
            size_t tm = line.find(L"taskbar_monitor=");
            if (tm != std::wstring::npos) {
                std::wstring v = line.substr(tm + wcslen(L"taskbar_monitor="));
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                ui.taskbarMonitor = WideToUtf8(Unescape(v));
            }
            size_t tdt = line.find(L"taskbar_dock_t=");
            if (tdt != std::wstring::npos) {
                std::wstring v = line.substr(tdt + wcslen(L"taskbar_dock_t="));
                wchar_t* end = nullptr;
                double d = wcstod(v.c_str(), &end);
                if (end != v.c_str() && std::isfinite(d))
                    ui.taskbarDockT = std::clamp(d, 0.0, 1.0);
            }
            size_t twd = line.find(L"taskbar_width=");
            if (twd != std::wstring::npos) {
                std::wstring v = line.substr(twd + wcslen(L"taskbar_width="));
                ui.taskbarWidth = std::clamp((int)wcstol(v.c_str(), nullptr, 10), 160, 520);
            }
            size_t tst = line.find(L"taskbar_strategy=");
            if (tst != std::wstring::npos) {
                std::wstring v = line.substr(tst + wcslen(L"taskbar_strategy="));
                while (!v.empty() && (v.back() == L' ' || v.back() == L'\r')) v.pop_back();
                if (IsTaskbarStrategy(v))
                    ui.taskbarStrategy = std::string(v.begin(), v.end());
            }
        }
    }
    model.ReplaceAll(std::move(items));
    return LoadResult::Loaded;
}

bool Store::Save(const TodoModel& model, const WindowGeometry& geom, const UiState& ui) {
    std::wstring path = DataFilePath();
    if (path.empty()) return false;

    std::wstring text = L"XTODO v1\n";
    if (geom.valid) {
        wchar_t buf[128];
        swprintf_s(buf, L"win %d %d %d %d\n", geom.x, geom.y, geom.w, geom.h);
        text += buf;
    }
    text += ui.completedExpanded ? L"ui completed_expanded=1\n"
                                 : L"ui completed_expanded=0\n";
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
    text += L"ui capsule_style=" + std::wstring(ui.capsuleStyle.begin(), ui.capsuleStyle.end()) + L"\n";
    text += L"ui capsule_dock_edge=" + std::wstring(ui.capsuleDockEdge.begin(), ui.capsuleDockEdge.end()) + L"\n";
    {
        wchar_t buf[64];
        swprintf_s(buf, L"ui capsule_dock_t=%.6f\n", ui.capsuleDockT);
        text += buf;
    }
    if (!ui.capsuleMonitor.empty())
        text += L"ui capsule_monitor=" + Escape(Utf8ToWide(ui.capsuleMonitor)) + L"\n";
    if (!ui.taskbarMonitor.empty())
        text += L"ui taskbar_monitor=" + Escape(Utf8ToWide(ui.taskbarMonitor)) + L"\n";
    {
        wchar_t tb[64];
        swprintf_s(tb, L"ui taskbar_dock_t=%.6f\n", ui.taskbarDockT);
        text += tb;
    }
    text += L"ui taskbar_width=" + std::to_wstring(ui.taskbarWidth) + L"\n";
    text += L"ui taskbar_strategy=" + std::wstring(ui.taskbarStrategy.begin(), ui.taskbarStrategy.end()) + L"\n";
    for (const auto& it : model.Items()) {
        text += L"item ";
        text += it.done ? L"1 " : L"0 ";
        text += Escape(it.text);
        text += L"\n";
    }
    return WriteAllBytesAtomic(path, WideToUtf8(text));
}
