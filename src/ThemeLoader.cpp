#include "ThemeLoader.h"
#include "ThemeCatalog.h"

#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <cstring>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace Theme {
namespace {

constexpr unsigned long long kMaxFileSize = 64ull * 1024; // 64KB
constexpr size_t kMaxThemes = 128;
constexpr size_t kScanLimit = 512;   // 枚举候选硬上限，防病态目录拖慢启动 / 重载
constexpr int    kMaxNestDepth = 16; // JSON 嵌套深度上限（合法主题 <= 3），防递归解析 / reserved 递归栈溢出

bool IsIdCharA(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

// 严格 ^#[0-9A-Fa-f]{6}$ → 0xRRGGBB
bool ParseHexColor(const std::string& s, uint32_t& out) {
    if (s.size() != 7 || s[0] != '#') return false;
    uint32_t v = 0;
    for (int i = 1; i < 7; ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = v;
    return true;
}

// parse 前字符级扫描最大括号嵌套深度（不递归），用于挡掉深嵌套 JSON，
// 避免 nlohmann 递归下降解析与 HasReservedRecursive 递归遍历栈溢出。
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

// UTF-8 → 宽字符，严格校验；失败返回 false
bool Utf8ToWideChecked(const std::string& s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return false;
    out.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return true;
}

std::wstring TruncateDisplay(std::wstring s, size_t maxLen) {
    if (s.size() > maxLen) s.resize(maxLen);
    return s;
}

// reserved 字段（递归、大小写不敏感）
bool IsReservedKey(const std::string& key) {
    static const char* kReserved[] = {
        "script","url","uri","image","path","file","command","css","font","layout","animation","hotkey"
    };
    std::string lk = key;
    for (auto& c : lk) c = (char)std::tolower((unsigned char)c);
    for (auto* r : kReserved) if (lk == r) return true;
    return false;
}
bool HasReservedRecursive(const json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (IsReservedKey(it.key())) return true;
            if (HasReservedRecursive(it.value())) return true;
        }
    } else if (j.is_array()) {
        for (const auto& e : j) if (HasReservedRecursive(e)) return true;
    }
    return false;
}

void AddIssue(LoadResult& res, const std::wstring& file, ThemeIssueSeverity sev,
              ThemeIssueKind kind, const std::wstring& detail) {
    res.issues.push_back(ThemeIssue{ file, sev, kind, detail });
}

// 校验自定义 id（spec §8.3）
bool ValidateId(const std::string& id, std::wstring& err) {
    if (id.size() < 3 || id.size() > 64) { err = L"id 长度需为 3..64"; return false; }
    for (char c : id) if (!IsIdCharA(c)) { err = L"id 含非法字符"; return false; }
    if (id.rfind("custom.", 0) != 0)     { err = L"id 必须以 custom. 开头"; return false; }
    if (id.find("..") != std::string::npos) { err = L"id 不得包含 .."; return false; }
    if (id.back() == '.' || id.back() == '-') { err = L"id 不得以 . 或 - 结尾"; return false; }
    return true;
}

// 解析一个颜色子对象（colors/capsule/tray 共用）：字段表 + 是否允许 slimAlpha
struct FieldSpec { const char* key; uint32_t* dst; };

// 检测对象内未知字段（不在 allowed 列表）→ error
bool HasUnknownInner(const json& obj, const std::vector<std::string>& allowed) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            return true;
    }
    return false;
}

bool GetColor(const json& obj, const char* key, uint32_t& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return false;
    return ParseHexColor(it->get<std::string>(), out);
}

// 解析整套主题。返回 false 表示无效（已记录 issue）。
bool ParseTheme(const json& j, const std::wstring& file, LoadResult& res, ThemeVisual& out) {
    if (!j.is_object()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::ParseFailed, L"顶层不是对象");
        return false;
    }
    // reserved 字段（递归、大小写不敏感）→ 整套无效
    if (HasReservedRecursive(j)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::ReservedField, L"含保留字段");
        return false;
    }
    // schema == 1
    auto schemaIt = j.find("schema");
    if (schemaIt == j.end() || !schemaIt->is_number_integer() || schemaIt->get<long long>() != 1) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidSchema, L"schema 必须为数字 1");
        return false;
    }
    // id
    auto idIt = j.find("id");
    if (idIt == j.end() || !idIt->is_string()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 id");
        return false;
    }
    std::string id = idIt->get<std::string>();
    std::wstring idErr;
    if (!ValidateId(id, idErr)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidId, idErr);
        return false;
    }
    if (IsBuiltInId(id)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidId, L"不得覆盖内置主题 id");
        return false;
    }
    // name.zh / name.en（UTF-8 校验，显示截断 32）
    auto nameIt = j.find("name");
    if (nameIt == j.end() || !nameIt->is_object()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 name");
        return false;
    }
    auto zhIt = nameIt->find("zh");
    auto enIt = nameIt->find("en");
    if (zhIt == nameIt->end() || !zhIt->is_string() || enIt == nameIt->end() || !enIt->is_string()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 name.zh / name.en");
        return false;
    }
    std::wstring zh, en;
    if (!Utf8ToWideChecked(zhIt->get<std::string>(), zh) ||
        !Utf8ToWideChecked(enIt->get<std::string>(), en)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::ParseFailed, L"name 含非法 UTF-8");
        return false;
    }
    // dark
    auto darkIt = j.find("dark");
    if (darkIt == j.end() || !darkIt->is_boolean()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 dark");
        return false;
    }
    // colors / capsule / tray 必须为对象
    auto colorsIt = j.find("colors");
    auto capsuleIt = j.find("capsule");
    auto trayIt = j.find("tray");
    if (colorsIt == j.end() || !colorsIt->is_object() ||
        capsuleIt == j.end() || !capsuleIt->is_object() ||
        trayIt == j.end() || !trayIt->is_object()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 colors / capsule / tray");
        return false;
    }

    ThemeVisual t;
    t.id = id;
    t.source = Source::Custom;
    t.name.zh = TruncateDisplay(zh, 32);
    t.name.en = TruncateDisplay(en, 32);
    t.dark = darkIt->get<bool>();

    // colors（21 字段）
    const std::vector<std::string> colorKeys = {
        "paper","paperElevated","paperEdge","text","textWeak","textDone","divider",
        "rowHover","menuHover","buttonHover","buttonPressed","disabledText","checkBorder",
        "checkFill","checkFillHover","checkMark","handle","handleHover","danger","dangerHover","focusRing"
    };
    if (HasUnknownInner(*colorsIt, colorKeys)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::UnknownField, L"colors 含未知字段");
        return false;
    }
    ColorSet& c = t.colors;
    uint32_t* colorDst[] = {
        &c.paper,&c.paperElevated,&c.paperEdge,&c.text,&c.textWeak,&c.textDone,&c.divider,
        &c.rowHover,&c.menuHover,&c.buttonHover,&c.buttonPressed,&c.disabledText,&c.checkBorder,
        &c.checkFill,&c.checkFillHover,&c.checkMark,&c.handle,&c.handleHover,&c.danger,&c.dangerHover,&c.focusRing
    };
    for (size_t i = 0; i < colorKeys.size(); ++i) {
        if (!GetColor(*colorsIt, colorKeys[i].c_str(), *colorDst[i])) {
            AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidColor,
                     L"colors." + std::wstring(colorKeys[i].begin(), colorKeys[i].end()) + L" 缺失或非法");
            return false;
        }
    }

    // capsule（9 颜色 + slimAlpha）
    const std::vector<std::string> capsuleColorKeys = {
        "slimPaper","slimEdge","slimText","dotActive","dotIdle","dotEdge",
        "dotActiveHover","dotIdleHover","dotEdgeHover"
    };
    std::vector<std::string> capsuleAllowed = capsuleColorKeys;
    capsuleAllowed.push_back("slimAlpha");
    if (HasUnknownInner(*capsuleIt, capsuleAllowed)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::UnknownField, L"capsule 含未知字段");
        return false;
    }
    CapsuleSet& cap = t.capsule;
    uint32_t* capDst[] = {
        &cap.slimPaper,&cap.slimEdge,&cap.slimText,&cap.dotActive,&cap.dotIdle,&cap.dotEdge,
        &cap.dotActiveHover,&cap.dotIdleHover,&cap.dotEdgeHover
    };
    for (size_t i = 0; i < capsuleColorKeys.size(); ++i) {
        if (!GetColor(*capsuleIt, capsuleColorKeys[i].c_str(), *capDst[i])) {
            AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidColor,
                     L"capsule." + std::wstring(capsuleColorKeys[i].begin(), capsuleColorKeys[i].end()) + L" 缺失或非法");
            return false;
        }
    }
    auto alphaIt = capsuleIt->find("slimAlpha");
    if (alphaIt == capsuleIt->end() || !alphaIt->is_number()) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::MissingField, L"缺少 capsule.slimAlpha");
        return false;
    }
    double alpha = alphaIt->get<double>();
    if (!(alpha >= 0.2 && alpha <= 1.0)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidAlpha, L"slimAlpha 需在 0.2..1.0");
        return false;
    }
    cap.slimAlpha = (float)alpha;

    // tray（4 颜色）
    const std::vector<std::string> trayKeys = { "background","edge","mark","badge" };
    if (HasUnknownInner(*trayIt, trayKeys)) {
        AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::UnknownField, L"tray 含未知字段");
        return false;
    }
    TraySet& tr = t.tray;
    uint32_t* trayDst[] = { &tr.background,&tr.edge,&tr.mark,&tr.badge };
    for (size_t i = 0; i < trayKeys.size(); ++i) {
        if (!GetColor(*trayIt, trayKeys[i].c_str(), *trayDst[i])) {
            AddIssue(res, file, ThemeIssueSeverity::Error, ThemeIssueKind::InvalidColor,
                     L"tray." + std::wstring(trayKeys[i].begin(), trayKeys[i].end()) + L" 缺失或非法");
            return false;
        }
    }

    // 顶层未知字段 → warning（不致命）
    const std::vector<std::string> topKeys = { "schema","id","name","dark","colors","capsule","tray" };
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find(topKeys.begin(), topKeys.end(), it.key()) == topKeys.end())
            AddIssue(res, file, ThemeIssueSeverity::Warning, ThemeIssueKind::UnknownField,
                     L"顶层未知字段：" + std::wstring(it.key().begin(), it.key().end()));
    }

    out = std::move(t);
    return true;
}

// 读取整个文件为字节（≤ kMaxFileSize）；返回 0=成功，1=过大，2=读失败，3=reparse 拒绝
int ReadThemeFile(const std::wstring& path, std::string& bytes) {
    // FILE_FLAG_OPEN_REPARSE_POINT：打开 reparse point 本身而非跟随，再从 handle 校验，
    // 杜绝枚举与打开之间被换成 symlink / junction 跟随到目录外（path-check-then-open 竞争）。
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 2;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info)) { CloseHandle(h); return 2; }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { CloseHandle(h); return 3; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return 2; }
    if ((unsigned long long)size.QuadPart > kMaxFileSize) { CloseHandle(h); return 1; }
    bytes.resize((size_t)size.QuadPart);
    bool ok = true;
    if (!bytes.empty()) {
        DWORD read = 0;
        ok = ReadFile(h, bytes.data(), (DWORD)bytes.size(), &read, nullptr) && read == bytes.size();
    }
    CloseHandle(h);
    return ok ? 0 : 2;
}

std::wstring SafeFileName(const std::string& id) {
    std::wstring out;
    for (char c : id) if (IsIdCharA(c)) out += (wchar_t)c;
    if (out.empty()) out = L"theme";
    return out;
}

} // namespace

std::wstring ThemeDirectory() {
    wchar_t appdata[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata)))
        return L"";
    return std::wstring(appdata) + L"\\x-todo\\themes\\";
}

LoadResult LoadCustomThemes() {
    LoadResult res;
    std::wstring dir = ThemeDirectory();
    if (dir.empty()) return res;

    // 目录不存在：无自定义主题（不创建）
    DWORD dirAttr = GetFileAttributesW(dir.c_str());
    if (dirAttr == INVALID_FILE_ATTRIBUTES || !(dirAttr & FILE_ATTRIBUTE_DIRECTORY))
        return res;
    // 目录本身是 reparse point：记录并跳过
    if (dirAttr & FILE_ATTRIBUTE_REPARSE_POINT) {
        AddIssue(res, L"themes", ThemeIssueSeverity::Warning, ThemeIssueKind::ReadFailed,
                 L"主题目录是 reparse point，已跳过");
        return res;
    }

    // 先收集候选文件名，排序后处理（确定性）
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue; // 不跟随 reparse
            std::wstring name = fd.cFileName;
            size_t dot = name.find_last_of(L'.');
            if (dot == std::wstring::npos) continue;
            std::wstring ext = name.substr(dot);
            for (auto& ch : ext) ch = (wchar_t)towlower(ch);
            if (ext != L".xtheme" && ext != L".json") continue;
            files.push_back(name);
            if (files.size() >= kScanLimit) break; // bound 枚举，病态目录不再无界收集 / 排序
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(files.begin(), files.end());
    // 病态目录可能堆上千个坏文件：cap 候选处理数（不只成功加载数），避免拖慢启动 / 重载
    bool truncated = files.size() > kMaxThemes;
    if (truncated) files.resize(kMaxThemes);

    std::vector<std::string> seenIds;
    for (const std::wstring& name : files) {
        std::wstring full = dir + name;
        std::string bytes;
        int rc = ReadThemeFile(full, bytes);
        if (rc == 1) { AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::TooLarge, L"文件超过 64KB"); continue; }
        if (rc == 3) { AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::ReadFailed, L"reparse point，已跳过"); continue; }
        if (rc == 2) { AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::ReadFailed, L"读取失败"); continue; }

        // 整体 UTF-8 校验（JSON 必须是合法 UTF-8）
        std::wstring probe;
        if (!Utf8ToWideChecked(bytes, probe)) {
            AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::ParseFailed, L"非法 UTF-8");
            continue;
        }
        // parse 前用字符扫描挡掉深嵌套，避免 nlohmann 递归解析与 reserved 递归栈溢出
        if (MaxBracketDepth(bytes) > kMaxNestDepth) {
            AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::ParseFailed, L"JSON 嵌套过深");
            continue;
        }
        json j = json::parse(bytes, nullptr, false); // 不抛异常，失败返回 discarded
        if (j.is_discarded()) {
            AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::ParseFailed, L"JSON 解析失败");
            continue;
        }
        ThemeVisual t;
        if (!ParseTheme(j, name, res, t)) continue;

        // 重复 id：第一个合法文件生效，后续记录 DuplicateId
        if (std::find(seenIds.begin(), seenIds.end(), t.id) != seenIds.end()) {
            AddIssue(res, name, ThemeIssueSeverity::Error, ThemeIssueKind::DuplicateId,
                     L"重复的主题 id，已忽略");
            continue;
        }
        seenIds.push_back(t.id);
        res.themes.push_back(std::move(t));
    }
    if (truncated)
        AddIssue(res, L"themes", ThemeIssueSeverity::Warning, ThemeIssueKind::TooLarge,
                 L"主题文件超过 128 个，仅处理前 128 个");
    return res;
}

bool ExportTheme(const ThemeVisual& theme, const std::wstring& destDir, ThemeIssue* error) {
    auto fail = [&](ThemeIssueKind kind, const std::wstring& detail) {
        if (error) *error = ThemeIssue{ L"", ThemeIssueSeverity::Error, kind, detail };
        return false;
    };
    if (destDir.empty()) return fail(ThemeIssueKind::ReadFailed, L"导出目录无效");

    // 内置主题导出改写为可重新加载的 custom.exported-* id
    std::string outId = theme.id;
    if (theme.source == Source::BuiltIn) outId = "custom.exported-" + theme.id;
    std::wstring idErr;
    if (!ValidateId(outId, idErr)) return fail(ThemeIssueKind::InvalidId, idErr);

    std::wstring dir = destDir;
    if (dir.back() != L'\\') dir += L'\\';
    CreateDirectoryW(dir.c_str(), nullptr); // 导出时才创建（已存在返回失败但无碍）

    // handle 绑定校验：打开目录 handle，从 handle 确认是目录、非 reparse point，并取规范 final 路径，
    // 作为后续“创建出的文件确实落在此目录内”的比较基准（防 path-check-then-create 的 junction 竞争）。
    HANDLE dirH = CreateFileW(dir.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (dirH == INVALID_HANDLE_VALUE) return fail(ThemeIssueKind::ReadFailed, L"导出目录不可用");
    BY_HANDLE_FILE_INFORMATION dirInfo{};
    bool dirOk = GetFileInformationByHandle(dirH, &dirInfo) &&
                 (dirInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                 !(dirInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    std::wstring dirFinal(1024, L'\0');
    DWORD dn = dirOk ? GetFinalPathNameByHandleW(dirH, dirFinal.data(), 1023, FILE_NAME_NORMALIZED) : 0;
    CloseHandle(dirH);
    if (!dirOk) return fail(ThemeIssueKind::ReadFailed, L"导出目录是 reparse point 或不可用");
    if (dn == 0 || dn >= 1024) return fail(ThemeIssueKind::ReadFailed, L"无法解析导出目录");
    dirFinal.resize(dn);
    while (!dirFinal.empty() && dirFinal.back() == L'\\') dirFinal.pop_back();

    std::wstring fileName = SafeFileName(outId) + L".xtheme";
    std::wstring fullPath = dir + fileName;
    if (GetFileAttributesW(fullPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return fail(ThemeIssueKind::DuplicateId, L"目标文件已存在，未覆盖");

    auto hex = [](uint32_t v) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06x", v & 0xFFFFFF);
        return std::string(buf);
    };
    auto w2u = [](const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    };
    const ColorSet& c = theme.colors;
    const CapsuleSet& cap = theme.capsule;
    const TraySet& tr = theme.tray;
    json j = {
        { "schema", 1 },
        { "id", outId },
        { "name", { { "zh", w2u(theme.name.zh) }, { "en", w2u(theme.name.en) } } },
        { "dark", theme.dark },
        { "colors", {
            { "paper", hex(c.paper) }, { "paperElevated", hex(c.paperElevated) }, { "paperEdge", hex(c.paperEdge) },
            { "text", hex(c.text) }, { "textWeak", hex(c.textWeak) }, { "textDone", hex(c.textDone) },
            { "divider", hex(c.divider) }, { "rowHover", hex(c.rowHover) }, { "menuHover", hex(c.menuHover) },
            { "buttonHover", hex(c.buttonHover) }, { "buttonPressed", hex(c.buttonPressed) },
            { "disabledText", hex(c.disabledText) }, { "checkBorder", hex(c.checkBorder) },
            { "checkFill", hex(c.checkFill) }, { "checkFillHover", hex(c.checkFillHover) },
            { "checkMark", hex(c.checkMark) }, { "handle", hex(c.handle) }, { "handleHover", hex(c.handleHover) },
            { "danger", hex(c.danger) }, { "dangerHover", hex(c.dangerHover) }, { "focusRing", hex(c.focusRing) }
        }},
        { "capsule", {
            { "slimPaper", hex(cap.slimPaper) }, { "slimEdge", hex(cap.slimEdge) }, { "slimText", hex(cap.slimText) },
            { "dotActive", hex(cap.dotActive) }, { "dotIdle", hex(cap.dotIdle) }, { "dotEdge", hex(cap.dotEdge) },
            { "dotActiveHover", hex(cap.dotActiveHover) }, { "dotIdleHover", hex(cap.dotIdleHover) },
            { "dotEdgeHover", hex(cap.dotEdgeHover) }, { "slimAlpha", cap.slimAlpha }
        }},
        { "tray", {
            { "background", hex(tr.background) }, { "edge", hex(tr.edge) },
            { "mark", hex(tr.mark) }, { "badge", hex(tr.badge) }
        }}
    };
    std::string text = j.dump(2);

    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail(ThemeIssueKind::ReadFailed, L"无法创建导出文件");
    // 创建后从 handle 取 final 路径，确认其父目录就是已校验的目录 final 路径；
    // 若目录在校验后被换成 junction，父目录会不一致 —— 删除已创建文件并失败。
    std::wstring fileFinal(1024, L'\0');
    DWORD ffn = GetFinalPathNameByHandleW(h, fileFinal.data(), 1023, FILE_NAME_NORMALIZED);
    bool inside = false;
    if (ffn > 0 && ffn < 1024) {
        fileFinal.resize(ffn);
        size_t slash = fileFinal.find_last_of(L'\\');
        if (slash != std::wstring::npos)
            inside = (_wcsicmp(fileFinal.substr(0, slash).c_str(), dirFinal.c_str()) == 0);
    }
    if (!inside) { CloseHandle(h); DeleteFileW(fullPath.c_str()); return fail(ThemeIssueKind::ReadFailed, L"导出路径越界，已拒绝"); }
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == text.size();
    CloseHandle(h);
    if (!ok) { DeleteFileW(fullPath.c_str()); return fail(ThemeIssueKind::ReadFailed, L"写入导出文件失败"); }
    return true;
}

} // namespace Theme
