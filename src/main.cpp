#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <vector>
#include "LaunchCommand.h"
#include "MainWindow.h"

namespace {

LaunchCommand ParseLaunchCommand() {
    LaunchCommand command;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return command;
    std::vector<const wchar_t*> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    command = ParseLaunchArgs(argc, args.data());
    LocalFree(argv);
    return command;
}

} // namespace

// GUI 子系统入口（WIN32 可执行，无控制台窗口）
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const LaunchCommand launch = ParseLaunchCommand();

    // 单实例：已在运行则唤起已有窗口后退出
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"x-todo-singleton-mutex");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            if (launch.reminderCheck) {
                PostMessageW(existing, WM_APP_REMINDER_CHECK, 0, 0);
            } else if (launch.openReminder && launch.blockId > 0) {
                PostMessageW(existing, WM_APP_REMINDER_OPEN,
                             static_cast<WPARAM>(launch.blockId), 0);
            } else {
                PostMessageW(existing, WM_APP_SHOW, 0, 0);
            }
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    int code = 0;
    {
        MainWindow window;
        if (!window.Create()) {
            code = 1;
        } else {
            if (launch.reminderCheck) {
                window.RunReminderCheckOnce(true);
            } else if (launch.openReminder && launch.blockId > 0) {
                window.OpenReminderTarget(launch.blockId);
            } else {
                window.InitialShow();
            }
            if (!launch.reminderCheck) {
                MSG msg;
                while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                code = (int)msg.wParam;
            }
        }
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return code;
}
