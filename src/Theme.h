#pragma once
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#include <d2d1.h>
#else
using COLORREF = uint32_t;
constexpr COLORREF RGB(unsigned char r, unsigned char g, unsigned char b) {
    return static_cast<COLORREF>(r) | (static_cast<COLORREF>(g) << 8) | (static_cast<COLORREF>(b) << 16);
}
struct D2D1_COLOR_F {
    float r;
    float g;
    float b;
    float a;
};
namespace D2D1 {
inline D2D1_COLOR_F ColorF(float r, float g, float b, float a = 1.0f) {
    return D2D1_COLOR_F{ r, g, b, a };
}
}
#endif
#include <string>
#include <vector>

// 主题数据模型 + 固定布局尺寸常量。
//
// 颜色语义集中在 ThemeVisual：渲染层只读解析后的主题快照，不直接消费全局颜色常量。
// 尺寸与字体是固定设计常量，不开放给主题文件（命中区域 / 窗口几何 / 行高 / 字体族 / 动画
// 由程序控制，不属于皮肤）。
namespace Theme {

// ——————————————————————————— 固定设计常量 ———————————————————————————
// 逻辑像素（96dpi 基准），渲染时乘 dpiScale。主题文件不得修改这些值。
constexpr float kCorner     = 12.0f; // 窗口圆角半径
constexpr float kPadX       = 14.0f; // 左右内边距
constexpr float kTitleH     = 34.0f; // 顶部标题 / 拖动条高度
constexpr float kTabsH      = 42.0f; // 多列表标签栏高度
constexpr float kRowH       = 34.0f; // 条目行高
constexpr float kCheckSize  = 18.0f; // 复选框边长
constexpr float kFontSize   = 14.0f; // 正文字号
constexpr float kSmallFont  = 12.0f; // 小字号
constexpr float kResizeEdge = 8.0f;  // 缩放命中边缘宽度
constexpr float kFooterH    = 0.0f;  // 固定底栏高度；新增入口已移入内容区加号行
constexpr float kSectionH   = 26.0f; // 已完成折叠条高度

// 胶囊折叠态尺寸（仅 Capsule 形态使用）；半透明度移入 CapsuleSet::slimAlpha
constexpr float kCapsuleSlimW = 18.0f; // 细边胶囊折叠宽
constexpr float kCapsuleSlimH = 96.0f; // 细边胶囊折叠高
constexpr float kCapsuleDot   = 20.0f; // 圆点胶囊直径（折叠为正方）

constexpr wchar_t kFontFamily[] = L"Microsoft YaHei UI"; // 含中英文字形

// ——————————————————————————— 颜色转换 helper ———————————————————————————
// 0xRRGGBB -> D2D1_COLOR_F（Direct2D 消费）
D2D1_COLOR_F D2DColor(uint32_t rgb, float alpha = 1.0f);
// 0xRRGGBB -> COLORREF（GDI 消费，字节序为 0x00BBGGRR）
COLORREF GdiColor(uint32_t rgb);
// 把 fg 以 alpha 不透明度叠加到不透明 bg，返回不透明结果（用于预混 hover / pressed）
uint32_t Blend(uint32_t fg, uint32_t bg, float alpha);
// WCAG 相对对比度（>= 1.0），用于内置主题自检与自定义主题校验
float ContrastRatio(uint32_t a, uint32_t b);

// 过渡别名：等价于 D2DColor。渲染迁移完成后可删除。
inline D2D1_COLOR_F Color(uint32_t rgb, float alpha = 1.0f) { return D2DColor(rgb, alpha); }

// ——————————————————————————— 主题视觉结构 ———————————————————————————
enum class Source {
    BuiltIn,
    Custom
};

struct DisplayName {
    std::wstring zh;
    std::wstring en;
};

// 渲染层消费的全部颜色角色。所有 surface 取色都来自这里，不再有全局颜色常量。
struct ColorSet {
    uint32_t paper;          // 主窗口主体背景
    uint32_t paperElevated;  // 弹窗 / 菜单 / 主题管理窗口背景
    uint32_t paperEdge;      // 所有 surface 边框
    uint32_t text;           // 主文本
    uint32_t textWeak;       // 次要文本（计数 / 提示）
    uint32_t textDone;       // 已完成文本
    uint32_t divider;        // 分隔线
    uint32_t rowHover;       // 列表行 hover
    uint32_t menuHover;      // 菜单项 hover
    uint32_t buttonHover;    // 普通按钮 hover
    uint32_t buttonPressed;  // 普通按钮 pressed
    uint32_t disabledText;   // 禁用文本
    uint32_t checkBorder;    // 复选框边框
    uint32_t checkFill;      // 勾选填充
    uint32_t checkFillHover; // 勾选填充 hover
    uint32_t checkMark;      // 对勾
    uint32_t handle;         // 拖动手柄
    uint32_t handleHover;    // 拖动手柄 hover
    uint32_t danger;         // 删除 / 危险
    uint32_t dangerHover;    // 危险 hover
    uint32_t focusRing;      // 焦点环 / 拖放插入线
};

struct CapsuleSet {
    uint32_t slimPaper;      // 细边胶囊背景
    uint32_t slimEdge;       // 细边胶囊边框
    uint32_t slimText;       // 细边胶囊数字
    uint32_t dotActive;      // 圆点：有未完成项
    uint32_t dotIdle;        // 圆点：全部完成
    uint32_t dotEdge;        // 圆点边框
    uint32_t dotActiveHover; // 圆点 hover：有未完成项
    uint32_t dotIdleHover;   // 圆点 hover：全部完成
    uint32_t dotEdgeHover;   // 圆点边框 hover
    float    slimAlpha;      // 细边折叠静止不透明度（0.2 .. 1.0）
};

struct TraySet {
    uint32_t background; // 托盘图标底色
    uint32_t edge;       // 托盘图标边框
    uint32_t mark;       // 托盘图标主标记
    uint32_t badge;      // 托盘图标未完成提示点
};

// 已解析的主题快照：渲染层唯一读取对象。
struct ThemeVisual {
    std::string id;
    Source      source = Source::BuiltIn;
    DisplayName name;
    bool        dark = false;
    ColorSet    colors{};
    CapsuleSet  capsule{};
    TraySet     tray{};
};

// ——————————————————————————— 加载 issue / 运行时 notice ———————————————————————————
// 文件加载问题进入 ThemeIssue；系统读取失败、fallback、托盘生成失败、导出失败进入 ThemeNotice。
enum class ThemeIssueSeverity {
    Warning,
    Error
};

enum class ThemeIssueKind {
    ReadFailed,
    TooLarge,
    ParseFailed,
    InvalidSchema,
    InvalidId,
    DuplicateId,
    MissingField,
    InvalidColor,
    InvalidAlpha,
    ReservedField,
    UnknownField,
    ContrastFailed
};

struct ThemeIssue {
    std::wstring       fileName; // 仅文件名，不含完整路径
    ThemeIssueSeverity severity = ThemeIssueSeverity::Error;
    ThemeIssueKind     kind     = ThemeIssueKind::ParseFailed;
    std::wstring       detail;
};

struct ThemeNotice {
    std::wstring message;
};

// ——————————————————————————— 主题解析 ———————————————————————————
struct ResolveInput {
    std::string mode;         // builtin | custom | follow_system
    std::string themeId;      // builtin / custom 当前选择
    std::string lightThemeId; // follow_system 浅色
    std::string darkThemeId;  // follow_system 深色
    bool systemDark          = false;
    bool systemHighContrast  = false;
    const std::vector<ThemeVisual>* customThemes = nullptr;
};

struct ResolveResult {
    ThemeVisual  theme;            // 解析结果；失败时为内置 paper
    bool         fellBack = false; // 是否回退
    std::wstring message;          // 回退时记录失败的 theme id（供本地化 notice 使用）
};

// 按 mode + 系统状态解析出最终主题；解析失败回退内置 paper 并设置 fellBack / message。
ResolveResult ResolveTheme(const ResolveInput& input);

// ——————————————————————————— 系统状态读取 ———————————————————————————
// 读 HKCU\...\Themes\Personalize\AppsUseLightTheme：0=深色返回 true。
// 读取失败按浅色处理（返回 false），并把 *ok 置 false。
bool SystemUsesDarkMode(bool* ok = nullptr);
// 读 SPI_GETHIGHCONTRAST：HCF_HIGHCONTRASTON 时返回 true。
// 读取失败按未开启处理（返回 false），并把 *ok 置 false。
bool SystemHighContrastOn(bool* ok = nullptr);

} // namespace Theme
