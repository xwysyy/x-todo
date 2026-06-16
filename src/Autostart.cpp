#include "Autostart.h"

#include <windows.h>
#include <string>

namespace {
const wchar_t* kRunKey    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValueName = L"x-todo";

std::wstring ExePathQuoted() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    return L"\"" + p + L"\"";
}
} // namespace

bool Autostart::IsEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    LONG r = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS && type == REG_SZ;
}

bool Autostart::SetEnabled(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG r;
    if (on) {
        std::wstring v = ExePathQuoted();
        r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(v.c_str()),
                           static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, kValueName);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS; // 本就没有也算成功
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}
