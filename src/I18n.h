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
    Calendar,     // 日历标签
    AllDay,       // 全天行
    EmptyListTitle,// 当前列表为空
    EmptyActivePrompt,// 点击创建第一条
    ListNamePrompt,// 标签页名称输入提示
    ListDefault,  // 默认标签名
    ListRename,   // 重命名标签页
    ListDelete,   // 删除标签页
    ListDeleteMsg,// 删除标签页确认正文
    Completed,    // 已完成
    Clear,        // 清空
    DeleteItemMsg,// 删除确认正文
    ClearAllMsg,  // 清空确认正文
    ConfirmOk,    // 确认按钮
    ConfirmCancel,// 取消按钮
    LoadFailedMsg,// 数据读取失败提示

    // —— 主题 ——
    ThemeHeader,             // 菜单"皮肤"分组标题
    ThemeFollowSystem,       // 跟随系统
    ThemePaper,              // 暖纸
    ThemeMint,               // 薄荷
    ThemeSky,                // 天空
    ThemeRose,               // 玫瑰
    ThemeSand,               // 沙色
    ThemeGraphite,           // 石墨
    ThemeInk,                // 墨色
    ThemeContrast,           // 高对比
    ThemeCustom,             // 自定义主题…（菜单入口）
    ThemeManager,            // 主题管理窗口标题
    ThemeReload,             // 重新加载主题
    ThemeOpenFolder,         // 打开主题目录
    ThemeExportCurrent,      // 导出当前主题
    ThemeIssues,             // 加载 issue 列表标题
    ThemeNotices,            // 运行时 notice 列表标题
    ThemeSetLightFollow,     // 设为浅色跟随主题
    ThemeSetDarkFollow,      // 设为深色跟随主题
    ThemeFallbackNotice,     // 主题回退提示
    ThemeHighContrastNotice, // 高对比开启提示

    Count
};

const wchar_t* T(Str key, Lang lang);
Lang SystemDefaultLang(); // 按系统 UI 语言：中文 -> Zh，其余 -> En
