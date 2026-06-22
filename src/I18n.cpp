#include "I18n.h"
#ifdef _WIN32
#include <windows.h>
#endif

const wchar_t* T(Str key, Lang lang) {
    const bool zh = (lang == Lang::Zh);
    switch (key) {
        case Str::Show:          return zh ? L"显示便签"        : L"Show note";
        case Str::ModeNormal:    return zh ? L"普通窗口"        : L"Normal window";
        case Str::ModeDesktop:   return zh ? L"挂到桌面"        : L"On desktop";
        case Str::ModeCapsule:   return zh ? L"侧边胶囊"        : L"Side capsule";
        case Str::StyleSlim:     return zh ? L"细边"            : L"Slim bar";
        case Str::StyleDot:      return zh ? L"圆点"            : L"Dot";
        case Str::Autostart:     return zh ? L"开机自启"        : L"Start with Windows";
        case Str::ToggleLang:    return zh ? L"English"         : L"中文"; // 显示可切换到的目标语言
        case Str::Exit:          return zh ? L"退出"            : L"Exit";
        case Str::Calendar:      return zh ? L"日历"            : L"Calendar";
        case Str::CalendarToday: return zh ? L"今天"            : L"Today";
        case Str::EmptyListTitle:return zh ? L"这个列表还没有待办" : L"No tasks here yet";
        case Str::EmptyActivePrompt:
                                  return zh ? L"新建一条，开始安排要做的事" : L"Add one to start planning.";
        case Str::NewTask:       return zh ? L"新建待办"        : L"New task";
        case Str::ListNamePrompt:return zh ? L"标签页名称"      : L"List name";
        case Str::ListDefault:   return zh ? L"默认"            : L"Inbox";
        case Str::ListRename:    return zh ? L"重命名标签页"    : L"Rename list";
        case Str::ListDelete:    return zh ? L"删除标签页"      : L"Delete list";
        case Str::ListDeleteMsg: return zh ? L"删除这个标签页及其中所有待办？此操作不可撤销。"
                                           : L"Delete this list and all its items? This cannot be undone.";
        case Str::Completed:     return zh ? L"已完成"          : L"Completed";
        case Str::Clear:         return zh ? L"清空"            : L"Clear";
        case Str::DeleteItemMsg: return zh ? L"删除这一条？"    : L"Delete this item?";
        case Str::ClearAllMsg:   return zh ? L"清空所有已完成条目？此操作不可撤销。"
                                           : L"Clear all completed items? This cannot be undone.";
        case Str::ConfirmOk:     return zh ? L"确认"            : L"OK";
        case Str::ConfirmCancel: return zh ? L"取消"            : L"Cancel";
        case Str::LoadFailedMsg: return zh ? L"读取待办数据失败，原文件已备份为 data.txt.corrupt.bak，本次以空白开始。"
                                           : L"Could not read your data. The original was backed up as data.txt.corrupt.bak; starting empty.";

        case Str::ThemeHeader:        return zh ? L"皮肤"           : L"Theme";
        case Str::ThemeFollowSystem:  return zh ? L"跟随系统"       : L"Follow system";
        case Str::ThemePaper:         return zh ? L"暖纸"           : L"Paper";
        case Str::ThemeMint:          return zh ? L"薄荷"           : L"Mint";
        case Str::ThemeSky:           return zh ? L"天空"           : L"Sky";
        case Str::ThemeRose:          return zh ? L"玫瑰"           : L"Rose";
        case Str::ThemeSand:          return zh ? L"沙色"           : L"Sand";
        case Str::ThemeGraphite:      return zh ? L"石墨"           : L"Graphite";
        case Str::ThemeInk:           return zh ? L"墨色"           : L"Ink";
        case Str::ThemeContrast:      return zh ? L"高对比"         : L"High contrast";
        case Str::ThemeCustom:        return zh ? L"自定义主题…"    : L"Custom themes…";
        case Str::ThemeManager:       return zh ? L"主题管理"       : L"Theme manager";
        case Str::ThemeReload:        return zh ? L"重新加载主题"   : L"Reload themes";
        case Str::ThemeOpenFolder:    return zh ? L"打开主题目录"   : L"Open theme folder";
        case Str::ThemeExportCurrent: return zh ? L"导出当前主题"   : L"Export current theme";
        case Str::ThemeIssues:        return zh ? L"加载问题"       : L"Load issues";
        case Str::ThemeNotices:       return zh ? L"运行时提示"     : L"Notices";
        case Str::ThemeSetLightFollow:return zh ? L"设为浅色跟随主题" : L"Set as light follow theme";
        case Str::ThemeSetDarkFollow: return zh ? L"设为深色跟随主题" : L"Set as dark follow theme";
        case Str::ThemeFallbackNotice:return zh ? L"主题不可用，已回退到暖纸"
                                                : L"Theme unavailable; fell back to Paper";
        case Str::ThemeHighContrastNotice:
                                      return zh ? L"Windows 高对比已开启，当前主题可能不易辨认"
                                                : L"Windows High Contrast is on; the current theme may be hard to read";
        case Str::Count:
            break;
    }
    return L"";
}

Lang SystemDefaultLang() {
#ifdef _WIN32
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_CHINESE ? Lang::Zh : Lang::En;
#else
    return Lang::En;
#endif
}
