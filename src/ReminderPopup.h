#pragma once

#include "I18n.h"
#include "ReminderTypes.h"
#include "Theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <vector>

namespace ReminderPopup {

bool Show(HWND owner, const std::vector<ReminderCandidate>& reminders, Lang lang,
          const Theme::ThemeVisual& theme, ID2D1Factory* d2dFactory,
          IDWriteFactory* dwriteFactory, UINT openMessage);

} // namespace ReminderPopup
