#include "SettingsWindow.h"
#include "ReminderSettingsPolicy.h"
#include "ThemedWindowControls.h"

#include <windowsx.h>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

namespace Settings {
namespace {

constexpr wchar_t kSettingsClass[] = L"XTodoSettingsWindow";
namespace Ui = ThemedWindow;

template <class T> void SafeRelease(T** p) {
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

enum class RowKind {
    Section,
    Language,
    ToggleAutostart,
    ToggleReminderEnable,
    ToggleReminderBeforeStart5,
    ToggleReminderHalfway,
    ToggleReminderInAppPopup,
    ToggleReminderCapsulePulse,
    ToggleReminderSystemNotification,
    ToggleReminderTaskSchedulerFallback,
    ToggleReminderCatchUp,
    ToggleBackup,
    Folder,
    Status
};
enum class Action {
    None,
    LangZh,
    LangEn,
    Autostart,
    ReminderEnable,
    ReminderBeforeStart5,
    ReminderHalfway,
    ReminderInAppPopup,
    ReminderCapsulePulse,
    ReminderSystemNotification,
    ReminderTaskSchedulerFallback,
    ReminderCatchUp,
    BackupToggle,
    BackupFolder,
    Close
};

int ClampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

struct Row {
    RowKind kind = RowKind::Status;
    std::wstring label;
    std::wstring value;
    int top = 0;
    int height = 0;
};

struct State {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    Host* host = nullptr;
    std::vector<Row> rows;
    Action hover = Action::None;
    int scroll = 0;
    int contentH = 0;
    int w = 0;
    int h = 0;
    int headerH = 0;
    bool done = false;
    ID2D1Factory* d2dFactory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* titleFmt = nullptr;
    IDWriteTextFormat* sectionFmt = nullptr;
    IDWriteTextFormat* rowFmt = nullptr;
    IDWriteTextFormat* smallLeftFmt = nullptr;
    IDWriteTextFormat* smallRightFmt = nullptr;
    IDWriteTextFormat* smallCenterFmt = nullptr;
};

std::wstring FormatBackupTime(long long epoch, Lang lang) {
    if (epoch <= 0) return T(Str::BackupNever, lang);
    std::time_t value = static_cast<std::time_t>(epoch);
    std::tm local{};
    if (localtime_s(&local, &value) != 0) return T(Str::BackupNever, lang);

    wchar_t buf[64]{};
    if (wcsftime(buf, 64, L"%Y-%m-%d %H:%M", &local) == 0)
        return T(Str::BackupNever, lang);
    return buf;
}

void BuildRows(State& s) {
    Host& h = *s.host;
    s.rows.clear();
    auto add = [&](RowKind kind, std::wstring label, std::wstring value = L"") {
        Row row;
        row.kind = kind;
        row.label = std::move(label);
        row.value = std::move(value);
        s.rows.push_back(std::move(row));
    };

    add(RowKind::Section, T(Str::SettingsGeneral, h.lang));
    add(RowKind::Language, T(Str::Language, h.lang));
    add(RowKind::ToggleAutostart, T(Str::Autostart, h.lang));

    add(RowKind::Section, T(Str::SettingsReminders, h.lang));
    add(RowKind::ToggleReminderEnable, T(Str::ReminderEnable, h.lang));
    add(RowKind::ToggleReminderBeforeStart5, T(Str::ReminderBeforeStart5, h.lang));
    add(RowKind::ToggleReminderHalfway, T(Str::ReminderHalfway, h.lang));
    add(RowKind::ToggleReminderInAppPopup, T(Str::ReminderInAppPopup, h.lang));
    add(RowKind::ToggleReminderCapsulePulse, T(Str::ReminderCapsulePulse, h.lang));
    add(RowKind::ToggleReminderSystemNotification, T(Str::ReminderSystemNotification, h.lang));
    add(RowKind::ToggleReminderTaskSchedulerFallback,
        T(Str::ReminderTaskSchedulerFallback, h.lang));
    add(RowKind::ToggleReminderCatchUp, T(Str::ReminderCatchUp, h.lang));
    if (!h.reminderNotificationStatus.empty())
        add(RowKind::Status, L"", h.reminderNotificationStatus);
    if (!h.reminderSchedulerStatus.empty())
        add(RowKind::Status, L"", h.reminderSchedulerStatus);

    add(RowKind::Section, T(Str::SettingsDataBackup, h.lang));
    add(RowKind::ToggleBackup, T(Str::AutoBackup, h.lang));
    if (!h.backupDir.empty()) {
        add(RowKind::Folder, T(Str::BackupFolder, h.lang), h.backupDir);
        add(RowKind::Status, T(Str::BackupLast, h.lang), FormatBackupTime(h.backupLastEpoch, h.lang));
        if (!h.backupStatus.empty())
            add(RowKind::Status, L"", h.backupStatus);
    } else {
        add(RowKind::Status, T(Str::BackupLast, h.lang), T(Str::BackupDisabled, h.lang));
        if (!h.backupStatus.empty())
            add(RowKind::Status, L"", h.backupStatus);
    }

}

void LayoutRows(State& s) {
    s.headerH = Ui::Px(s.hwnd, 42);
    const int pad = Ui::Px(s.hwnd, 14);
    const int sectionH = Ui::Px(s.hwnd, 22);
    const int rowH = Ui::Px(s.hwnd, 38);
    const int statusH = Ui::Px(s.hwnd, 30);
    const int sectionGap = Ui::Px(s.hwnd, 10);
    int y = s.headerH + Ui::Px(s.hwnd, 8);
    bool firstSection = true;
    for (Row& row : s.rows) {
        if (row.kind == RowKind::Section) {
            if (!firstSection) y += sectionGap;
            firstSection = false;
        }
        row.top = y;
        switch (row.kind) {
            case RowKind::Section: row.height = sectionH; break;
            case RowKind::Status: row.height = statusH; break;
            default: row.height = rowH; break;
        }
        y += row.height;
    }
    s.contentH = y + pad;
}

void ClampScroll(State& s) {
    int maxScroll = s.contentH - s.h;
    if (maxScroll < 0) maxScroll = 0;
    if (s.scroll < 0) s.scroll = 0;
    if (s.scroll > maxScroll) s.scroll = maxScroll;
}

RECT HeaderCloseRect(const State& s) {
    const int size = Ui::Px(s.hwnd, 28);
    const int right = s.w - Ui::Px(s.hwnd, 8);
    const int top = Ui::Px(s.hwnd, 7);
    return RECT{ right - size, top, right, top + size };
}

RECT CardRect(const State& s, size_t sectionIndex) {
    size_t first = sectionIndex + 1;
    if (first >= s.rows.size() || s.rows[first].kind == RowKind::Section)
        return RECT{ 0, 0, 0, 0 };

    size_t last = first;
    while (last + 1 < s.rows.size() && s.rows[last + 1].kind != RowKind::Section)
        ++last;

    const int inset = Ui::Px(s.hwnd, 12);
    return RECT{ inset, s.rows[first].top - s.scroll, s.w - inset,
                 s.rows[last].top - s.scroll + s.rows[last].height };
}

bool LastInCard(const State& s, size_t rowIndex) {
    return rowIndex + 1 >= s.rows.size() || s.rows[rowIndex + 1].kind == RowKind::Section;
}

RECT RowRect(const State& s, const Row& row, int inset = 24) {
    const int x = Ui::Px(s.hwnd, static_cast<float>(inset));
    return RECT{ x, row.top - s.scroll, s.w - x, row.top - s.scroll + row.height };
}

RECT ToggleRect(const State& s, const Row& row) {
    RECT rr = RowRect(s, row);
    const int w = Ui::Px(s.hwnd, 42);
    const int h = Ui::Px(s.hwnd, 22);
    const int right = rr.right;
    const int top = rr.top + (row.height - h) / 2;
    return RECT{ right - w, top, right, top + h };
}

void LanguageRects(const State& s, const Row& row, RECT& zh, RECT& en) {
    RECT rr = RowRect(s, row);
    const int w = Ui::Px(s.hwnd, 154);
    const int h = Ui::Px(s.hwnd, 28);
    const int top = rr.top + (row.height - h) / 2;
    zh = RECT{ rr.right - w, top, rr.right - w / 2, top + h };
    en = RECT{ rr.right - w / 2, top, rr.right, top + h };
}

RECT FolderButtonRect(const State& s, const Row& row) {
    RECT rr = RowRect(s, row);
    const int w = Ui::Px(s.hwnd, 68);
    const int h = Ui::Px(s.hwnd, 26);
    const int top = rr.top + (row.height - h) / 2;
    return RECT{ rr.right - w, top, rr.right, top + h };
}

bool PtIn(const RECT& rect, int x, int y) {
    POINT pt{ x, y };
    return PtInRect(&rect, pt) != FALSE;
}

Action ActionAt(State& s, int x, int y) {
    if (PtIn(HeaderCloseRect(s), x, y)) return Action::Close;

    const int docY = y + s.scroll;
    for (const Row& row : s.rows) {
        if (docY < row.top || docY >= row.top + row.height) continue;
        switch (row.kind) {
            case RowKind::Language: {
                RECT zh{}, en{};
                LanguageRects(s, row, zh, en);
                if (PtIn(zh, x, y)) return Action::LangZh;
                if (PtIn(en, x, y)) return Action::LangEn;
                return Action::None;
            }
            case RowKind::ToggleAutostart:
                return PtIn(RowRect(s, row), x, y) ? Action::Autostart : Action::None;
            case RowKind::ToggleReminderEnable:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderEnable : Action::None;
            case RowKind::ToggleReminderBeforeStart5:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderBeforeStart5 : Action::None;
            case RowKind::ToggleReminderHalfway:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderHalfway : Action::None;
            case RowKind::ToggleReminderInAppPopup:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderInAppPopup : Action::None;
            case RowKind::ToggleReminderCapsulePulse:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderCapsulePulse : Action::None;
            case RowKind::ToggleReminderSystemNotification:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderSystemNotification : Action::None;
            case RowKind::ToggleReminderTaskSchedulerFallback:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderTaskSchedulerFallback : Action::None;
            case RowKind::ToggleReminderCatchUp:
                return PtIn(RowRect(s, row), x, y) ? Action::ReminderCatchUp : Action::None;
            case RowKind::ToggleBackup:
                return PtIn(RowRect(s, row), x, y) ? Action::BackupToggle : Action::None;
            case RowKind::Folder:
                return PtIn(FolderButtonRect(s, row), x, y) ? Action::BackupFolder : Action::None;
            default:
                return Action::None;
        }
    }
    return Action::None;
}

bool CreateTextFormats(State& s) {
    return Ui::CreateTextFormat(s.dwrite, s.hwnd, 12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.titleFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.sectionFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 10.5f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.rowFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 9.2f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.smallLeftFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 9.2f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_TRAILING,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.smallRightFmt) &&
           Ui::CreateTextFormat(s.dwrite, s.hwnd, 9.2f, DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_TEXT_ALIGNMENT_CENTER,
                                DWRITE_PARAGRAPH_ALIGNMENT_CENTER, &s.smallCenterFmt);
}

void ReleaseDrawingResources(State& s) {
    SafeRelease(&s.smallCenterFmt);
    SafeRelease(&s.smallRightFmt);
    SafeRelease(&s.smallLeftFmt);
    SafeRelease(&s.rowFmt);
    SafeRelease(&s.sectionFmt);
    SafeRelease(&s.titleFmt);
    SafeRelease(&s.brush);
    SafeRelease(&s.rt);
}

void DrawDivider(State& s, RECT rect, const Theme::ColorSet& c) {
    RECT line{ rect.left, rect.bottom - 1, rect.right, rect.bottom };
    Ui::FillRect(s.rt, s.brush, line, c.divider);
}

void DrawCloseButton(State& s, HWND hwnd, const Theme::ColorSet& c) {
    RECT btn = HeaderCloseRect(s);
    if (s.hover == Action::Close)
        Ui::FillRoundedRect(s.rt, s.brush, btn, static_cast<float>(Ui::Px(hwnd, 8)), c.buttonHover);
    const int cx = (btn.left + btn.right) / 2;
    const int cy = (btn.top + btn.bottom) / 2;
    const int half = Ui::Px(hwnd, 5);
    Ui::DrawLine(s.rt, s.brush, static_cast<float>(cx - half), static_cast<float>(cy - half),
                 static_cast<float>(cx + half), static_cast<float>(cy + half),
                 c.textWeak, static_cast<float>(Ui::Px(hwnd, 1.5f)));
    Ui::DrawLine(s.rt, s.brush, static_cast<float>(cx + half), static_cast<float>(cy - half),
                 static_cast<float>(cx - half), static_cast<float>(cy + half),
                 c.textWeak, static_cast<float>(Ui::Px(hwnd, 1.5f)));
}

void DrawPill(State& s, HWND hwnd, RECT rect, const std::wstring& text, bool selected,
              bool hovered, const Theme::ColorSet& c) {
    const uint32_t fill = selected ? c.checkFill : (hovered ? c.buttonHover : c.paper);
    const uint32_t textColor = selected ? c.checkMark : c.text;
    const float radius = static_cast<float>(Ui::Px(hwnd, 7));
    Ui::FillRoundedRect(s.rt, s.brush, rect, radius, fill);
    Ui::StrokeRoundedRect(s.rt, s.brush, rect, radius, selected ? c.checkFill : c.paperEdge);
    Ui::RenderText(s.rt, s.brush, text, rect, s.smallCenterFmt, textColor);
}

void DrawToggle(State& s, HWND hwnd, RECT rect, bool on, bool hovered,
                const Theme::ColorSet& c) {
    const uint32_t fill = on ? c.checkFill
                             : (hovered ? c.buttonHover : Theme::Blend(c.paperEdge, c.paperElevated, 0.25f));
    const float radius = static_cast<float>(Ui::Px(hwnd, 11));
    Ui::FillRoundedRect(s.rt, s.brush, rect, radius, fill);
    Ui::StrokeRoundedRect(s.rt, s.brush, rect, radius, on ? c.checkFill : c.paperEdge);

    const int knob = Ui::Px(hwnd, 16);
    const int pad = Ui::Px(hwnd, 3);
    const int knobLeft = on ? rect.right - pad - knob : rect.left + pad;
    RECT k{ knobLeft, rect.top + pad, knobLeft + knob, rect.top + pad + knob };
    Ui::FillRoundedRect(s.rt, s.brush, k, static_cast<float>(knob) / 2.0f,
                      on ? c.checkMark : c.paperElevated);
}

void Paint(State& s) {
    PAINTSTRUCT ps{};
    BeginPaint(s.hwnd, &ps);
    if ((!s.rt || !s.brush) &&
        !Ui::CreateDeviceResources(s.hwnd, s.d2dFactory, &s.rt, &s.brush)) {
        EndPaint(s.hwnd, &ps);
        return;
    }
    RECT rc{};
    GetClientRect(s.hwnd, &rc);

    const Theme::ColorSet& c = s.host->theme.colors;
    s.rt->BeginDraw();
    s.rt->SetTransform(D2D1::Matrix3x2F::Identity());
    Ui::FillColor(s.rt, c.paper);

    RECT titleRect{ Ui::Px(s.hwnd, 14), 0, s.w - Ui::Px(s.hwnd, 44), s.headerH };
    Ui::RenderText(s.rt, s.brush, T(Str::Settings, s.host->lang), titleRect, s.titleFmt, c.text);
    DrawCloseButton(s, s.hwnd, c);

    for (size_t i = 0; i < s.rows.size(); ++i) {
        if (s.rows[i].kind != RowKind::Section) continue;
        RECT card = CardRect(s, i);
        if (card.bottom <= card.top || card.bottom < 0 || card.top > rc.bottom) continue;
        const float radius = static_cast<float>(Ui::Px(s.hwnd, 10));
        Ui::FillRoundedRect(s.rt, s.brush, card, radius, c.paperElevated);
        Ui::StrokeRoundedRect(s.rt, s.brush, card, radius, c.paperEdge);
    }

    for (size_t i = 0; i < s.rows.size(); ++i) {
        const Row& row = s.rows[i];
        const int top = row.top - s.scroll;
        if (top + row.height < 0 || top > rc.bottom) continue;
        RECT rr = RowRect(s, row);
        switch (row.kind) {
            case RowKind::Section: {
                RECT sectionRect{ Ui::Px(s.hwnd, 14), rr.top, rr.right, rr.bottom };
                Ui::RenderText(s.rt, s.brush, row.label, sectionRect, s.sectionFmt, c.textWeak);
                break;
            }
            case RowKind::Language: {
                Ui::RenderText(s.rt, s.brush, row.label, rr, s.rowFmt, c.text);
                RECT zh{}, en{};
                LanguageRects(s, row, zh, en);
                RECT segment{ zh.left, zh.top, en.right, en.bottom };
                const float radius = static_cast<float>(Ui::Px(s.hwnd, 8));
                Ui::FillRoundedRect(s.rt, s.brush, segment, radius, c.paper);
                Ui::StrokeRoundedRect(s.rt, s.brush, segment, radius, c.paperEdge);
                DrawPill(s, s.hwnd, zh, L"中文", s.host->lang == Lang::Zh,
                         s.hover == Action::LangZh, c);
                DrawPill(s, s.hwnd, en, L"English", s.host->lang == Lang::En,
                         s.hover == Action::LangEn, c);
                Ui::DrawLine(s.rt, s.brush, static_cast<float>(zh.right),
                             static_cast<float>(zh.top + Ui::Px(s.hwnd, 4)),
                             static_cast<float>(zh.right),
                             static_cast<float>(zh.bottom - Ui::Px(s.hwnd, 4)),
                             c.paperEdge);
                if (!LastInCard(s, i))
                    DrawDivider(s, RECT{ rr.left, rr.top, rr.right, rr.bottom }, c);
                break;
            }
            case RowKind::ToggleAutostart:
            case RowKind::ToggleReminderEnable:
            case RowKind::ToggleReminderBeforeStart5:
            case RowKind::ToggleReminderHalfway:
            case RowKind::ToggleReminderInAppPopup:
            case RowKind::ToggleReminderCapsulePulse:
            case RowKind::ToggleReminderSystemNotification:
            case RowKind::ToggleReminderTaskSchedulerFallback:
            case RowKind::ToggleReminderCatchUp:
            case RowKind::ToggleBackup: {
                bool on = false;
                Action act = Action::None;
                switch (row.kind) {
                    case RowKind::ToggleAutostart:
                        on = s.host->autostart;
                        act = Action::Autostart;
                        break;
                    case RowKind::ToggleReminderEnable:
                        on = s.host->reminders.enabled;
                        act = Action::ReminderEnable;
                        break;
                    case RowKind::ToggleReminderBeforeStart5:
                        on = s.host->reminders.beforeStart5;
                        act = Action::ReminderBeforeStart5;
                        break;
                    case RowKind::ToggleReminderHalfway:
                        on = s.host->reminders.halfway;
                        act = Action::ReminderHalfway;
                        break;
                    case RowKind::ToggleReminderInAppPopup:
                        on = s.host->reminders.inAppPopup;
                        act = Action::ReminderInAppPopup;
                        break;
                    case RowKind::ToggleReminderCapsulePulse:
                        on = s.host->reminders.capsulePulse;
                        act = Action::ReminderCapsulePulse;
                        break;
                    case RowKind::ToggleReminderSystemNotification:
                        on = s.host->reminders.systemNotification;
                        act = Action::ReminderSystemNotification;
                        break;
                    case RowKind::ToggleReminderTaskSchedulerFallback:
                        on = s.host->reminders.taskSchedulerFallback;
                        act = Action::ReminderTaskSchedulerFallback;
                        break;
                    case RowKind::ToggleReminderCatchUp:
                        on = s.host->reminders.catchUpAfterResume;
                        act = Action::ReminderCatchUp;
                        break;
                    case RowKind::ToggleBackup:
                        on = !s.host->backupDir.empty();
                        act = Action::BackupToggle;
                        break;
                    default:
                        break;
                }
                if (s.hover == act) {
                    RECT hover{ rr.left - Ui::Px(s.hwnd, 8), rr.top + Ui::Px(s.hwnd, 4),
                                rr.right + Ui::Px(s.hwnd, 8), rr.bottom - Ui::Px(s.hwnd, 4) };
                    Ui::FillRoundedRect(s.rt, s.brush, hover,
                                      static_cast<float>(Ui::Px(s.hwnd, 8)), c.menuHover);
                }
                Ui::RenderText(s.rt, s.brush, row.label, rr, s.rowFmt, c.text);
                RECT toggle = ToggleRect(s, row);
                RECT value{ toggle.left - Ui::Px(s.hwnd, 42), rr.top, toggle.left - Ui::Px(s.hwnd, 8), rr.bottom };
                Ui::RenderText(s.rt, s.brush, on ? T(Str::SettingOn, s.host->lang)
                                                  : T(Str::SettingOff, s.host->lang),
                               value, s.smallRightFmt, c.textWeak);
                DrawToggle(s, s.hwnd, toggle, on, s.hover == act, c);
                if (!LastInCard(s, i))
                    DrawDivider(s, RECT{ rr.left, rr.top, rr.right, rr.bottom }, c);
                break;
            }
            case RowKind::Folder: {
                Ui::RenderText(s.rt, s.brush, row.label, rr, s.rowFmt, c.text);
                RECT btn = FolderButtonRect(s, row);
                RECT pathRect{ rr.left + Ui::Px(s.hwnd, 84), rr.top,
                               btn.left - Ui::Px(s.hwnd, 8), rr.bottom };
                std::wstring shown = Ui::ElideMiddle(s.dwrite, s.smallLeftFmt, row.value,
                                                     static_cast<float>(pathRect.right - pathRect.left));
                Ui::RenderText(s.rt, s.brush, shown, pathRect, s.smallLeftFmt, c.textWeak);
                DrawPill(s, s.hwnd, btn, T(Str::BackupChangeFolder, s.host->lang),
                         false, s.hover == Action::BackupFolder, c);
                if (!LastInCard(s, i))
                    DrawDivider(s, RECT{ rr.left, rr.top, rr.right, rr.bottom }, c);
                break;
            }
            case RowKind::Status: {
                if (!row.label.empty()) {
                    RECT labelRect = rr;
                    labelRect.right = rr.left + Ui::Px(s.hwnd, 104);
                    Ui::RenderText(s.rt, s.brush, row.label, labelRect, s.smallLeftFmt, c.textWeak);
                    RECT valueRect{ labelRect.right, rr.top, rr.right, rr.bottom };
                    Ui::RenderText(s.rt, s.brush, row.value, valueRect, s.smallRightFmt, c.textWeak);
                } else {
                    Ui::RenderText(s.rt, s.brush, row.value, rr, s.smallLeftFmt, c.textWeak);
                }
                break;
            }
            default:
                break;
        }
    }

    Ui::StrokeRoundedRect(s.rt, s.brush, RECT{ 0, 0, rc.right, rc.bottom },
                        static_cast<float>(Ui::Px(s.hwnd, 12)), c.paperEdge);

    HRESULT hr = s.rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        SafeRelease(&s.brush);
        SafeRelease(&s.rt);
    }
    EndPaint(s.hwnd, &ps);
}

void Rebuild(State& s) {
    BuildRows(s);
    LayoutRows(s);
    ClampScroll(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
}

void DoAction(State& s, Action action) {
    Host& h = *s.host;
    auto applyReminders = [&]() {
        if (h.setReminderSettings) h.setReminderSettings(h.reminders);
    };
    switch (action) {
        case Action::LangZh:
            if (h.setLanguage) h.setLanguage(Lang::Zh);
            break;
        case Action::LangEn:
            if (h.setLanguage) h.setLanguage(Lang::En);
            break;
        case Action::Autostart:
            if (h.setAutostart) h.setAutostart(!h.autostart);
            break;
        case Action::ReminderEnable:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::Enable))
                applyReminders();
            break;
        case Action::ReminderBeforeStart5:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::BeforeStart5))
                applyReminders();
            break;
        case Action::ReminderHalfway:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::Halfway))
                applyReminders();
            break;
        case Action::ReminderInAppPopup:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::InAppPopup))
                applyReminders();
            break;
        case Action::ReminderCapsulePulse:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::CapsulePulse))
                applyReminders();
            break;
        case Action::ReminderSystemNotification:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::SystemNotification))
                applyReminders();
            break;
        case Action::ReminderTaskSchedulerFallback:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::TaskSchedulerFallback))
                applyReminders();
            break;
        case Action::ReminderCatchUp:
            if (ApplyReminderSettingAction(h.reminders, ReminderSettingAction::CatchUp))
                applyReminders();
            break;
        case Action::BackupToggle:
            if (h.backupDir.empty()) {
                if (h.chooseBackupFolder) h.chooseBackupFolder(s.hwnd);
            } else {
                if (h.disableBackup) h.disableBackup();
            }
            break;
        case Action::BackupFolder:
            if (h.chooseBackupFolder) h.chooseBackupFolder(s.hwnd);
            break;
        case Action::Close:
            s.done = true;
            DestroyWindow(s.hwnd);
            return;
        default:
            return;
    }
    Rebuild(s);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = static_cast<State*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        return TRUE;
    }
    if (!s) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT:
            Paint(*s);
            return 0;
        case WM_SIZE:
            if (s->rt) {
                UINT width = LOWORD(lp);
                UINT height = HIWORD(lp);
                if (width > 0 && height > 0)
                    s->rt->Resize(D2D1::SizeU(width, height));
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            Action hit = ActionAt(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit != s->hover) {
                s->hover = hit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (s->hover != Action::None) {
                s->hover = Action::None;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            s->scroll -= (delta / 120) * Ui::Px(hwnd, 36);
            ClampScroll(*s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
            DoAction(*s, ActionAt(*s, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)));
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                s->done = true;
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            if (PtIn(HeaderCloseRect(*s), pt.x, pt.y)) return HTCLIENT;
            if (pt.y >= 0 && pt.y < s->headerH) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_CLOSE:
            s->done = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            s->done = true;
            ReleaseDrawingResources(*s);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterSettingsClass() {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kSettingsClass;
    if (RegisterClassExW(&wc)) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

void ShowSettingsWindow(HWND owner, Host& host,
                        ID2D1Factory* d2dFactory, IDWriteFactory* dwrite) {
    if (!RegisterSettingsClass() || !d2dFactory || !dwrite) return;

    State state{};
    state.owner = owner;
    state.host = &host;
    state.d2dFactory = d2dFactory;
    state.dwrite = dwrite;

    UINT dpi = owner ? GetDpiForWindow(owner) : 96;
    state.w = MulDiv(390, dpi, 96);
    state.h = MulDiv(430, dpi, 96);

    int x = MulDiv(120, dpi, 96);
    int y = MulDiv(80, dpi, 96);
    HMONITOR mon = MonitorFromWindow(owner ? owner : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (mon && GetMonitorInfoW(mon, &mi)) {
        if (owner) {
            RECT ownerRect{};
            GetWindowRect(owner, &ownerRect);
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - state.w) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - state.h) / 2;
        } else {
            x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - state.w) / 2;
            y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - state.h) / 2;
        }
        x = ClampInt(x, mi.rcWork.left, mi.rcWork.right - state.w);
        y = ClampInt(y, mi.rcWork.top, mi.rcWork.bottom - state.h);
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kSettingsClass,
                                L"X-TODO", WS_POPUP | WS_CLIPCHILDREN,
                                x, y, state.w, state.h,
                                owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return;
    Ui::ApplyPopupRoundShape(hwnd, state.w, state.h, Ui::Px(hwnd, 16));
    if (!Ui::CreateDeviceResources(hwnd, d2dFactory, &state.rt, &state.brush) ||
        !CreateTextFormats(state)) {
        ReleaseDrawingResources(state);
        DestroyWindow(hwnd);
        return;
    }

    BuildRows(state);
    LayoutRows(state);
    ClampScroll(state);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg{};
    BOOL got = TRUE;
    while (!state.done && (got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));

    if (IsWindow(hwnd)) DestroyWindow(hwnd);
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

} // namespace Settings
