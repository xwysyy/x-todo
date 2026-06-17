#pragma once

// 界面语言与文案。品牌名 X-TODO 不翻译，不在此表内。
enum class Lang { Zh, En };

enum class Str {
    Show,         // 显示便签
    ModeNormal,   // 普通窗口
    ModeDesktop,  // 挂到桌面
    ModeCapsule,  // 侧边胶囊
    StyleSlim,    // 胶囊样式：细边
    StyleDot,     // 胶囊样式：圆点
    Autostart,    // 开机自启
    ToggleLang,   // 语言切换项（显示目标语言名）
    Exit,         // 退出
    NewItem,      // ＋ 新增一条
    Completed,    // 已完成
    Clear,        // 清空
    DeleteItemMsg,// 删除确认正文
    ClearAllMsg,  // 清空确认正文
    LoadFailedMsg // 数据读取失败提示
};

const wchar_t* T(Str key, Lang lang);
Lang SystemDefaultLang(); // 按系统 UI 语言：中文 -> Zh，其余 -> En
