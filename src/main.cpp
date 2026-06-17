#include <windows.h>
#include <commctrl.h>
#include "MainWindow.h"

// GUI 子系统入口（WIN32 可执行，无控制台窗口）
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    // 单实例：已在运行则唤起已有窗口后退出
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"x-todo-singleton-mutex");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr))
            PostMessageW(existing, WM_APP_SHOW, 0, 0);
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
            window.Show(false); // 冷启动：胶囊模式保持折叠（唤起才展开）
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            code = (int)msg.wParam;
        }
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return code;
}
