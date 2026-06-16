#include "I18n.h"
#include <windows.h>

const wchar_t* T(Str key, Lang lang) {
    const bool zh = (lang == Lang::Zh);
    switch (key) {
        case Str::Show:          return zh ? L"显示便签"        : L"Show note";
        case Str::ModeNormal:    return zh ? L"普通窗口"        : L"Normal window";
        case Str::ModeDesktop:   return zh ? L"挂到桌面"        : L"On desktop";
        case Str::ModeCapsule:   return zh ? L"侧边胶囊"        : L"Side capsule";
        case Str::Autostart:     return zh ? L"开机自启"        : L"Start with Windows";
        case Str::ToggleLang:    return zh ? L"English"         : L"中文"; // 显示可切换到的目标语言
        case Str::Exit:          return zh ? L"退出"            : L"Exit";
        case Str::NewItem:       return zh ? L"＋ 新增一条"     : L"＋ New item";
        case Str::Completed:     return zh ? L"已完成"          : L"Completed";
        case Str::Clear:         return zh ? L"清空"            : L"Clear";
        case Str::DeleteItemMsg: return zh ? L"删除这一条？"    : L"Delete this item?";
        case Str::ClearAllMsg:   return zh ? L"清空所有已完成条目？此操作不可撤销。"
                                           : L"Clear all completed items? This cannot be undone.";
        case Str::LoadFailedMsg: return zh ? L"读取待办数据失败，原文件已备份为 data.txt.corrupt.bak，本次以空白开始。"
                                           : L"Could not read your data. The original was backed up as data.txt.corrupt.bak; starting empty.";
    }
    return L"";
}

Lang SystemDefaultLang() {
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_CHINESE ? Lang::Zh : Lang::En;
}
