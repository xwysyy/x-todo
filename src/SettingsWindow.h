#pragma once

#include "I18n.h"
#include "Theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <functional>
#include <string>

namespace Settings {

struct Host {
    Lang lang = Lang::Zh;
    Theme::ThemeVisual theme;
    bool autostart = false;
    std::wstring backupDir;
    long long backupLastEpoch = 0;
    std::wstring backupStatus;

    std::function<void(Lang)> setLanguage;
    std::function<void(bool)> setAutostart;
    std::function<void(HWND)> chooseBackupFolder;
    std::function<void()> disableBackup;
};

void ShowSettingsWindow(HWND owner, Host& host,
                        ID2D1Factory* d2dFactory, IDWriteFactory* dwrite);

} // namespace Settings
