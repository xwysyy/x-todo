#include "Store.h"
#include "StoreFormat.h"

#include <windows.h>
#include <shlobj.h>
#include <string>

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

} // namespace

std::wstring Store::DataFilePath() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        return L"";
    std::wstring dir = std::wstring(appdata) + L"\\x-todo";
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在返回失败但无碍
    return dir + L"\\data.txt";
}

LoadResult Store::Load(TodoModel& model, CalendarModel& calendar, WindowGeometry& geom, UiState& ui) {
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

    StoreFormat::ParseText(text, model, calendar, geom, ui);
    return LoadResult::Loaded;
}

bool Store::Save(const TodoModel& model, const CalendarModel& calendar,
                 const WindowGeometry& geom, const UiState& ui) {
    std::wstring path = DataFilePath();
    if (path.empty()) return false;
    return WriteAllBytesAtomic(path, WideToUtf8(StoreFormat::SerializeText(model, calendar, geom, ui)));
}
