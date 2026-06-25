#include "Store.h"
#include "StoreFormat.h"

#include <windows.h>
#include <shlobj.h>
#include <cwctype>
#include <string>
#include <utility>

namespace {

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

std::wstring TrimTrailingSlash(std::wstring path) {
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    return path;
}

std::wstring FullPath(std::wstring path) {
    if (path.empty()) return path;
    DWORD len = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (len == 0) return TrimTrailingSlash(std::move(path));
    std::wstring out(len + 1, L'\0');
    DWORD got = GetFullPathNameW(path.c_str(), len + 1, out.data(), nullptr);
    if (got == 0 || got > len) return TrimTrailingSlash(std::move(path));
    out.resize(got);
    return TrimTrailingSlash(std::move(out));
}

std::wstring LowerPath(std::wstring path) {
    for (wchar_t& ch : path)
        ch = static_cast<wchar_t>(towlower(ch));
    return path;
}

bool SamePathText(const std::wstring& a, const std::wstring& b) {
    return LowerPath(FullPath(a)) == LowerPath(FullPath(b));
}

bool DirectoryUsable(const std::wstring& dir) {
    if (dir.empty()) return false;
    DWORD attrs = GetFileAttributesW(dir.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

std::wstring Store::DataDirectoryPath() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        return L"";
    std::wstring dir = std::wstring(appdata) + L"\\x-todo";
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在返回失败但无碍
    return dir;
}

std::wstring Store::DataFilePath() {
    std::wstring dir = DataDirectoryPath();
    if (dir.empty()) return L"";
    return dir + L"\\data.json";
}

std::wstring Store::BackupTargetPath(const std::wstring& backupDir) {
    if (backupDir.empty()) return L"";
    std::wstring dir = backupDir;
    if (dir.back() != L'\\' && dir.back() != L'/') dir += L"\\";
    return dir + L"data.json";
}

LoadResult Store::Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui) {
    std::vector<ReminderLogEntry> ignored;
    return Load(model, calendar, geom, ui, ignored);
}

LoadResult Store::Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui,
                       std::vector<ReminderLogEntry>& reminderLog) {
    std::wstring path = DataFilePath();
    if (path.empty()) return LoadResult::Missing;

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        reminderLog.clear();
        return LoadResult::Missing; // 首次运行，无数据文件
    }

    std::string bytes;
    if (!ReadAllBytes(path, bytes)) {
        // 文件存在但读取失败：备份原文件（不覆盖已有备份），避免之后保存把它弄丢
        CopyFileW(path.c_str(), (path + L".corrupt.bak").c_str(), TRUE);
        return LoadResult::Failed;
    }

    // 解析失败（非合法 JSON / 嵌套过深 / 顶层非对象，疑似被外部写坏）：备份并报失败，避免后续保存清空它
    if (!StoreFormat::Parse(bytes, model, calendar, geom, ui, reminderLog)) {
        CopyFileW(path.c_str(), (path + L".corrupt.bak").c_str(), TRUE);
        return LoadResult::Failed;
    }
    return LoadResult::Loaded;
}

bool Store::Save(const TodoModel& model, const CalendarModel& calendar,
                 const WindowGeometry& geom, const UiState& ui) {
    const std::vector<ReminderLogEntry> emptyLog;
    return Save(model, calendar, geom, ui, emptyLog);
}

bool Store::Save(const TodoModel& model, const CalendarModel& calendar,
                 const WindowGeometry& geom, const UiState& ui,
                 const std::vector<ReminderLogEntry>& reminderLog) {
    std::wstring path = DataFilePath();
    if (path.empty()) return false;
    return WriteAllBytesAtomic(path, StoreFormat::Serialize(model, calendar, geom, ui, reminderLog));
}

Store::BackupResult Store::BackupDataFileTo(const std::wstring& backupDir) {
    if (!DirectoryUsable(backupDir)) return Store::BackupResult::InvalidDirectory;

    std::wstring source = DataFilePath();
    if (source.empty()) return Store::BackupResult::ReadFailed;

    std::wstring target = BackupTargetPath(backupDir);
    if (target.empty()) return Store::BackupResult::InvalidDirectory;
    if (SamePathText(source, target)) return Store::BackupResult::SameAsDataFile;

    std::string bytes;
    if (!ReadAllBytes(source, bytes)) return Store::BackupResult::ReadFailed;
    if (!WriteAllBytesAtomic(target, bytes)) return Store::BackupResult::WriteFailed;
    return Store::BackupResult::Succeeded;
}
