#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace WindowsNotifier {

inline constexpr wchar_t kAppUserModelId[] = L"xwysyy.XTodo";
inline constexpr wchar_t kShortcutName[] = L"X-TODO.lnk";

std::wstring ToastLaunchArgsForBlock(const std::string& day, int blockId);
std::wstring EscapeToastXml(const std::wstring& text);
std::wstring ToastXmlForBlock(const std::string& day, int blockId,
                              const std::wstring& title, const std::wstring& body);

#ifdef _WIN32
bool ShowTrayBalloon(NOTIFYICONDATAW& nid, const std::wstring& title,
                     const std::wstring& body);
bool EnsureToastShortcut(const std::wstring& exePath, std::wstring* error);
bool ShowToast(const std::string& day, int blockId, const std::wstring& title,
               const std::wstring& body, std::wstring* error);
#endif

} // namespace WindowsNotifier
