#include "I18n.h"
#include "test_framework.h"

#include <array>
#include <cstddef>
#include <vector>

using namespace xtodo_test;

namespace {

const Str kAllStrings[] = {
    Str::Show, Str::ModeNormal, Str::ModeDesktop, Str::ModeCapsule,
    Str::StyleSlim, Str::StyleDot, Str::StyleBar, Str::StylePip, Str::Settings, Str::SettingsGeneral,
    Str::SettingsDataBackup, Str::SettingsClose, Str::SettingOn, Str::SettingOff,
    Str::Language, Str::Autostart,
    Str::Exit, Str::Calendar, Str::CalendarToday,
    Str::CalendarModeDay, Str::CalendarModeWeek, Str::CalendarModeMonth,
    Str::CalendarBlockDelete, Str::CalendarBlockDeleteMsg,
    Str::EmptyListTitle, Str::EmptyActivePrompt, Str::NewTask, Str::ListNamePrompt,
    Str::ListDefault, Str::ListRename, Str::ListDelete,
    Str::ListDeleteMsg, Str::Completed, Str::Clear, Str::DeleteItemMsg,
    Str::ClearAllMsg, Str::ConfirmOk, Str::ConfirmCancel, Str::LoadFailedMsg,
    Str::AutoBackup, Str::BackupFolder, Str::BackupChangeFolder,
    Str::BackupLast, Str::BackupNever, Str::BackupDisabled, Str::BackupReady,
    Str::BackupFailed, Str::BackupChooseFailed, Str::BackupSameFolder,
    Str::ThemeHeader,
    Str::ThemeFollowSystem, Str::ThemePaper, Str::ThemeMint, Str::ThemeSky,
    Str::ThemeRose, Str::ThemeSand, Str::ThemeCustom, Str::ThemeManager,
    Str::ThemeReload, Str::ThemeOpenFolder, Str::ThemeExportCurrent,
    Str::ThemeIssues, Str::ThemeNotices, Str::ThemeSetLightFollow,
    Str::ThemeSetDarkFollow, Str::ThemeFallbackNotice,
};

constexpr size_t kAllStringCount = sizeof(kAllStrings) / sizeof(kAllStrings[0]);
static_assert(kAllStringCount == static_cast<size_t>(Str::Count),
              "kAllStrings must list every Str value");

void EveryDeclaredStringHasChineseAndEnglishText() {
    std::array<bool, static_cast<size_t>(Str::Count)> seen{};
    for (Str key : kAllStrings) {
        const size_t index = static_cast<size_t>(key);
        EXPECT_TRUE(index < seen.size());
        EXPECT_FALSE(seen[index]);
        seen[index] = true;
        EXPECT_TRUE(T(key, Lang::Zh)[0] != L'\0');
        EXPECT_TRUE(T(key, Lang::En)[0] != L'\0');
    }
    for (bool value : seen) {
        EXPECT_TRUE(value);
    }
}

void KeyTranslationsMatchBehavioralExpectations() {
    EXPECT_EQ(std::wstring(T(Str::ListDefault, Lang::Zh)), std::wstring(L"默认"));
    EXPECT_EQ(std::wstring(T(Str::ListDefault, Lang::En)), std::wstring(L"Inbox"));
    EXPECT_TRUE(std::wstring(T(Str::LoadFailedMsg, Lang::En)).find(L"corrupt.bak") != std::wstring::npos);
    EXPECT_TRUE(std::wstring(T(Str::LoadFailedMsg, Lang::Zh)).find(L"corrupt.bak") != std::wstring::npos);
}

void SystemDefaultLangReturnsKnownEnumValue() {
    const Lang lang = SystemDefaultLang();
    EXPECT_TRUE(lang == Lang::Zh || lang == Lang::En);
}

const TestCase kTests[] = {
    {"EveryDeclaredStringHasChineseAndEnglishText", EveryDeclaredStringHasChineseAndEnglishText},
    {"KeyTranslationsMatchBehavioralExpectations", KeyTranslationsMatchBehavioralExpectations},
    {"SystemDefaultLangReturnsKnownEnumValue", SystemDefaultLangReturnsKnownEnumValue},
};

} // namespace

int main() {
    return RunTests("i18n", kTests);
}
