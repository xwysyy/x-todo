# T-A4: 全面主题与皮肤系统重构

> **版本 v1.1**：已纳入 GPT 5.5 Pro R1 复核的 must-fix 修订。v1 的总体方向保留，修订重点是收敛主题字段语义、修正 Store 精确解析、补齐原生 EDIT 与高对比边界、明确托盘图标生命周期，并避免把本地主题文件做成插件系统。
> **Self-contained spec.** 审核者与执行者不应依赖任何对话历史，任务背景、当前源码锚点、实现边界、验收方式都写在本文。
> **执行模型**：v1.1 已吸收 GPT 5.5 Pro R1 规格复核意见。建议把本版再交给 GPT 5.5 Pro 做一次只读复核，重点检查是否仍有过度设计或遗漏边界；复核意见合并后再由 Codex 实现；实现完成后做 R2 git diff 复核。
> **平台约束**：Windows-only（Win32 + Direct2D/DirectWrite，MSVC，`cmake -B build -A x64`）。当前开发环境为 Linux/WSL，无法实机编译或运行。验收分两层：源码机械核对 + Windows 实机视觉核验。深色主题、行内编辑框、托盘图标和系统明暗模式跟随必须在 Windows 上确认。

---

## §0 TL;DR

- **目标**：把 X-TODO 从固定浅色纸张视觉，重构为完整主题系统。主题系统覆盖主窗口、折叠胶囊、菜单、确认弹窗、行内编辑框、托盘图标和后续任务栏状态条。主题来源包括内置主题、自定义主题文件、系统明暗模式跟随。
- **核心决策**：执行彻底重构，不保留 `Theme::kPaper`、`Theme::kText` 这类全局颜色常量作为渲染消费路径。所有颜色消费统一读取解析后的 `ThemeVisual` 快照。尺寸、字体和命中区域保持固定设计常量，不开放给皮肤文件。
- **自定义主题**：主题文件位于 `%APPDATA%\x-todo\themes\`，格式为严格 JSON，扩展名为 `.xtheme`。主题文件只允许颜色、少量透明度和显示名称，不允许脚本、远程资源、图片路径、任意文件路径或布局参数。
- **系统跟随**：支持 `theme_mode=follow_system`，读取 Windows 应用明暗模式，响应 `WM_SETTINGCHANGE` 后重新解析浅色或深色主题；读取 Windows 高对比状态，响应 `WM_SYSCOLORCHANGE` 后重新解析主题。
- **工作量**：XL。预计新增 `Theme.cpp`、`ThemeCatalog.cpp`、`ThemeLoader.cpp`、`ThemeManagerWindow.cpp`、固定版本的 `nlohmann/json.hpp` 单头文件；修改 `MainWindow.h`、`MainWindow.cpp`、`MainWindowView.cpp`、`Store.h`、`Store.cpp`、`I18n.h`、`I18n.cpp`、`CMakeLists.txt`、README 双语文档。

---

## §1 Background

### 仓库

- **base commit**：`3a86764d5c1a3c6bf7bb61644443ae46024e76d8`
- **branch**：`main`
- **remote**：`origin https://github.com/xwysyy/X-TODO`
- **工作区状态**：本文不依赖工作区未提交内容。执行前必须重新确认 `git status` 和 `git diff --stat`；本文属于 docs 任务文档。
- **当前日期**：2026-06-18

### 当前应用结构

X-TODO 是 Win32 桌面待办，主窗口为无边框自绘 `WS_POPUP`，使用 Direct2D/DirectWrite 渲染，托盘常驻。当前三种挂载形态为 `Normal`、`Desktop`、`Capsule`，定义在 `src/MainWindow.h:16-20`。

当前主题相关结构：

- `src/Theme.h:5`：注释写明当前是“固定浅色纸张配色 + 布局尺寸”。
- `src/Theme.h:17-32`：所有颜色为 `constexpr uint32_t`。
- `src/Theme.h:34-52`：布局尺寸、胶囊尺寸、字体族也放在同一个 namespace。
- `src/MainWindowView.cpp:160-175`：`FillRect`、`StrokeRect`、`Text` 只接收裸 `uint32_t rgb`。
- `src/MainWindowView.cpp:177-365`：主窗口渲染直接消费 `Theme::kCheckFill`、`Theme::kText`、`Theme::kPaper` 等颜色。
- `src/MainWindow.cpp:184-246`：确认弹窗为 GDI 自绘，直接消费 `Theme::kDanger`、`Theme::kPaper`、`Theme::kText` 等颜色。
- `src/MainWindow.cpp:417-451`：自绘 popup 菜单直接消费 `Theme::kPaper`、`Theme::kDivider`、`Theme::kCheckFill` 等颜色。
- `src/MainWindow.cpp:797-802`：行内 `EDIT` 子控件的背景和文字颜色硬编码为 `RGB(0xFB, 0xF7, 0xEC)` 与 `RGB(0x33, 0x31, 0x2C)`，并缓存 `editBg_`。
- `src/MainWindow.cpp:889-895`：托盘图标直接加载 `resources/app.ico`。

当前持久化结构：

- `src/Store.h:10-20`：`UiState` 已保存完成区展开、置顶、挂载模式、语言、胶囊样式和胶囊吸附字段。
- `src/Store.cpp:127-218`：`Store::Load` 读取 `%APPDATA%\x-todo\data.txt`，并解析 `ui key=value` 行。
- `src/Store.cpp:220-258`：`Store::Save` 逐行保存 `ui key=value` 和 `item`。

当前菜单结构：

- `src/MainWindow.cpp:323-330`：`PopupMenuItem` 支持 `checked`、`danger`、`enabled`、`indent`，但没有 header、radio、说明项等类型。
- `src/MainWindow.cpp:942-969`：托盘菜单使用同一层列表。
- `src/MainWindow.cpp:971-993`：标题栏菜单使用同一层列表。
- `src/MainWindow.cpp:926-939`：命令编号已有 `1`、`2`、`3`、`10`、`11`、`20`、`30`、`31`。

### 重构动机

直接新增几套颜色会留下多个不一致表面：主窗口、弹窗、菜单、编辑框和胶囊有不同绘制技术；托盘图标和后续任务栏模式还有独立绘制入口。全面皮肤系统需要先定义主题数据模型和消费边界，再迁移所有颜色读取点。

本任务不追求内部接口兼容。当前颜色常量路径可以被删除或降级为内置主题定义，渲染层必须通过 `ThemeVisual` 快照取色。待办文本数据仍必须保留，不能因为主题字段异常丢失用户任务。

---

## §2 Goals

### 功能目标

1. 内置多套主题，覆盖浅色、深色和高对比视觉。
2. 支持用户自定义主题文件，主题文件可以被加载、验证、列出、应用、导出。
3. 支持系统明暗模式跟随，用户可分别选择浅色主题和深色主题。
4. 所有 UI 表面使用同一个已解析主题快照。
5. 主题加载失败不影响待办数据读取和主程序启动，错误应可见、可复核、可重新加载。
6. 程序仍保持单 exe；主题文件属于用户数据，不属于程序依赖资源。

### 工程目标

1. 颜色语义集中在 `ThemeVisual`，渲染层不直接依赖全局颜色常量。
2. 主题解析、主题目录扫描、内置主题表、系统明暗模式解析分模块实现。
3. 自定义主题输入有明确安全边界。
4. Windows 消费端有完整手工验证清单。
5. 后续 T-A3 任务栏状态条可以复用同一主题系统。

---

## §3 Locked Decisions

1. **彻底重构颜色消费路径**：渲染代码不得继续直接消费 `Theme::kPaper`、`Theme::kText`、`Theme::kDanger` 等颜色常量。
2. **尺寸不属于皮肤**：`kCorner`、`kPadX`、`kRowH`、`kFooterH`、`kCapsuleSlimW`、`kCapsuleDot`、`kFontFamily` 等布局与字体常量继续由程序控制。主题文件不得修改命中区域、窗口几何、行高、字体族和动画步数。
3. **自定义主题是数据，不是插件**：主题文件不能执行代码，不能读取外部路径，不能引用图片、URL、CSS、脚本或系统命令。
4. **无静默伪成功**：用户选择的主题缺失或无效时，程序可回退到内置 `paper` 以保证可启动，但必须记录 notice 并在主题管理窗口可见。
5. **深色主题为一等功能**：深色主题必须覆盖编辑框、确认弹窗、菜单、胶囊、托盘图标和系统明暗模式跟随，不能只换主窗口背景。
6. **内置主题永远可用**：`paper`、`graphite`、`contrast` 等内置主题不能被自定义主题覆盖。
7. **保留用户待办数据**：主题字段异常不得触发 `Store::Load` 的 corrupt 备份路径，不得导致 `item` 数据被清空或覆盖。
8. **菜单命令编号分区**：主题命令使用 `1000` 到 `1999` 范围，避免与现有命令和后续挂载模式扩展冲突。点击菜单中的具体主题会退出 `follow_system`，进入 `builtin` 或 `custom` 模式。
9. **托盘图标纳入主题系统**：托盘图标需要根据当前主题生成或带主题色标记。若动态图标生成失败，回退到 `resources/app.ico`，并记录可见错误。
10. **T-A3 兼容位点**：若任务栏状态条先于 T-A4 实现，T-A4 必须把任务栏状态条接入主题快照；若 T-A4 先实现，T-A3 状态条映射到 `paperElevated`、`paperEdge`、`text`、`textWeak`、`checkFill`、`danger`、`focusRing`，不提前新增单独 `StatusSet`。

---

## §4 Scope

### In

- 新增主题数据结构、主题选择结构、内置主题 catalog。
- 新增自定义主题目录扫描、JSON 解析、schema 校验、错误收集。
- 新增系统明暗模式读取和 `WM_SETTINGCHANGE` 响应。
- 新增主题管理窗口，列出内置主题、自定义主题、加载 issue、运行时 notice、重载入口和导出入口。
- 主窗口 Direct2D 渲染迁移到 `ThemeVisual`。
- 确认弹窗和 popup 菜单 GDI 绘制迁移到主题快照。
- 行内 `EDIT` 背景、文字、选区可见性和 brush 生命周期迁移。
- 折叠胶囊 Slim 与 Dot 使用主题色和主题透明度。
- 托盘图标生成接入主题。
- `Store` 增加主题选择字段并保存。
- README 双语更新，增加主题目录、主题文件示例和用户可见行为。

### Out

- 不实现脚本主题。
- 不实现图片背景主题。
- 不实现在线主题市场。
- 不允许主题文件改布局、字体、动画、热键或窗口形态。
- 不改 `TodoModel` 数据结构。
- 不做发布、tag、push。

---

## §5 New File Layout

建议文件结构：

```text
src/Theme.h              [REWRITE]  主题数据结构、固定尺寸常量、颜色转换 helper
src/Theme.cpp            [ADD]      主题解析后辅助函数、对比度、系统明暗模式 helper
src/ThemeCatalog.h       [ADD]      内置主题表与 theme id 查找接口
src/ThemeCatalog.cpp     [ADD]      内置主题定义
src/ThemeLoader.h        [ADD]      自定义主题扫描、JSON 加载、issue / notice 结构
src/ThemeLoader.cpp      [ADD]      文件读取、schema 校验、issue 收集
src/ThemeManagerWindow.h [ADD]      主题管理窗口声明
src/ThemeManagerWindow.cpp [ADD]    主题管理窗口绘制、命中、操作
third_party/nlohmann/json.hpp [ADD] 固定版本的 nlohmann/json 单头文件
```

`CMakeLists.txt` 需要把新增 `.cpp` 加入 `add_executable`。`third_party/nlohmann/json.hpp` 使用固定版本，并在 README 或 LICENSE 中补 MIT 许可说明。不得自写 JSON 子集解析器，除非 R1 复核后另行修改本规格。

---

## §6 Theme Data Model

### 6.1 固定设计常量

`Theme.h` 保留布局和字体常量：

```cpp
namespace Theme {

constexpr float kCorner = 12.0f;
constexpr float kPadX = 14.0f;
constexpr float kTitleH = 34.0f;
constexpr float kRowH = 34.0f;
constexpr float kCheckSize = 18.0f;
constexpr float kFontSize = 14.0f;
constexpr float kSmallFont = 12.0f;
constexpr float kResizeEdge = 6.0f;
constexpr float kFooterH = 32.0f;
constexpr float kSectionH = 26.0f;

constexpr float kCapsuleSlimW = 14.0f;
constexpr float kCapsuleSlimH = 96.0f;
constexpr float kCapsuleDot = 16.0f;

constexpr wchar_t kFontFamily[] = L"Microsoft YaHei UI";

}
```

这些常量不允许从主题文件加载。

### 6.2 主题选择

`Store.h` 的 `UiState` 增加：

```cpp
std::string themeMode = "builtin";       // builtin | custom | follow_system
std::string themeId = "paper";           // builtin 或 custom 的当前选择
std::string lightThemeId = "paper";      // follow_system 使用
std::string darkThemeId = "graphite";    // follow_system 使用
```

解释：

- `builtin`：只从内置主题表解析 `themeId`。
- `custom`：先从自定义主题表解析 `themeId`，失败时回退到 `paper` 并记录错误。
- `follow_system`：根据系统应用明暗模式选择 `lightThemeId` 或 `darkThemeId`。

### 6.3 主题视觉结构

建议结构：

```cpp
namespace Theme {

enum class Source {
    BuiltIn,
    Custom
};

struct DisplayName {
    std::wstring zh;
    std::wstring en;
};

struct ColorSet {
    uint32_t paper;
    uint32_t paperElevated;
    uint32_t paperEdge;
    uint32_t text;
    uint32_t textWeak;
    uint32_t textDone;
    uint32_t divider;
    uint32_t rowHover;
    uint32_t menuHover;
    uint32_t buttonHover;
    uint32_t buttonPressed;
    uint32_t disabledText;
    uint32_t checkBorder;
    uint32_t checkFill;
    uint32_t checkFillHover;
    uint32_t checkMark;
    uint32_t handle;
    uint32_t handleHover;
    uint32_t danger;
    uint32_t dangerHover;
    uint32_t focusRing;
};

struct CapsuleSet {
    uint32_t slimPaper;
    uint32_t slimEdge;
    uint32_t slimText;
    uint32_t dotActive;
    uint32_t dotIdle;
    uint32_t dotEdge;
    uint32_t dotActiveHover;
    uint32_t dotIdleHover;
    uint32_t dotEdgeHover;
    float slimAlpha;
};

struct TraySet {
    uint32_t background;
    uint32_t edge;
    uint32_t mark;
    uint32_t badge;
};

struct ThemeVisual {
    std::string id;
    Source source;
    DisplayName name;
    bool dark;
    ColorSet colors;
    CapsuleSet capsule;
    TraySet tray;
};

D2D1_COLOR_F D2DColor(uint32_t rgb, float alpha = 1.0f);
COLORREF GdiColor(uint32_t rgb);
uint32_t Blend(uint32_t fg, uint32_t bg, float alpha);

}
```

`Theme::Color` 可以保留为转换 helper，但建议改名为 `D2DColor`，避免与主题数据结构混淆。

### 6.4 当前主题快照

`MainWindow` 增加：

```cpp
Theme::ThemeVisual theme_;
std::vector<Theme::ThemeVisual> customThemes_;
std::vector<Theme::ThemeIssue> themeIssues_;
std::vector<Theme::ThemeNotice> themeNotices_;
```

`theme_` 是渲染层唯一读取对象。popup 菜单和确认弹窗打开时复制或持有当时的主题快照，不读取全局可变状态。

字段消费约束：

- 主窗口主体使用 `paper`。
- 确认弹窗、popup 菜单、主题管理窗口使用 `paperElevated`。
- 所有 surface 边框默认使用 `paperEdge`。
- 行 hover 使用 `rowHover`。
- 菜单 hover 使用 `menuHover`。
- 普通按钮 hover 和 pressed 使用 `buttonHover`、`buttonPressed`。
- disabled 文本使用 `disabledText`，不通过硬编码 alpha 混合生成。
- 本轮不承诺控制原生 `EDIT` 的选区色、插入光标色或 IME 候选窗口色；因此 public schema 不提供 `selection` 字段。
- 本轮不绘制自定义阴影；因此 public schema 不提供 `shadow` 字段。

---

## §7 Built-in Themes

内置主题至少包含：

1. `paper`：当前暖纸色，默认主题。
2. `mint`：浅绿低饱和主题。
3. `sky`：浅蓝灰主题。
4. `rose`：低饱和粉白主题。
5. `sand`：暖灰主题。
6. `graphite`：深灰主题，作为系统深色默认。
7. `ink`：高对比深色主题。
8. `contrast`：高对比浅色主题。

每套主题必须完整定义 `ColorSet`、`CapsuleSet`、`TraySet`。禁止使用“未填写则从 paper 继承”的运行时行为。内置主题缺字段应导致编译期或启动期自检失败。

内置主题质量要求：

- 正文与背景对比度不低于 7:1。
- 弱文本与背景对比度不低于 4.5:1。
- 危险文本与背景对比度不低于 4.5:1。
- checkMark 与 checkFill 对比度不低于 3:1。
- 深色主题的 `rowHover`、`menuHover`、`buttonHover`、`buttonPressed` 必须是最终消费色，不允许实现时在渲染处继续写死 alpha 混合。
- `danger` 在深色和浅色主题中都必须可见。
- Dot 胶囊的填充与边框分别校验，不能只校验 `dotActive` 与背景。

---

## §8 Custom Theme Format

### 8.1 文件位置

主题目录：

```text
%APPDATA%\x-todo\themes\
```

规则：

- 只扫描该目录的第一层。
- 只读取扩展名 `.xtheme` 和 `.json` 的普通文件。
- 单文件大小上限 64KB。
- 总加载数量上限 128。
- 不跟随 reparse point，不递归扫描子目录。
- 文件读取失败记录 `ThemeIssue`。

### 8.2 JSON schema

示例：

```json
{
  "schema": 1,
  "id": "custom.morning-paper",
  "name": {
    "zh": "晨纸",
    "en": "Morning Paper"
  },
  "dark": false,
  "colors": {
    "paper": "#fbf7ec",
    "paperElevated": "#fffaf0",
    "paperEdge": "#eae1cc",
    "text": "#33312c",
    "textWeak": "#9a9384",
    "textDone": "#aaa395",
    "divider": "#e7decb",
    "rowHover": "#f3ecd9",
    "menuHover": "#f3ecd9",
    "buttonHover": "#f1e6d2",
    "buttonPressed": "#ead9bd",
    "disabledText": "#bdb5a5",
    "checkBorder": "#b8ae97",
    "checkFill": "#7c9a6b",
    "checkFillHover": "#6e8a5d",
    "checkMark": "#ffffff",
    "handle": "#c4bba6",
    "handleHover": "#b0a78f",
    "danger": "#c8755e",
    "dangerHover": "#a95745",
    "focusRing": "#7c9a6b"
  },
  "capsule": {
    "slimPaper": "#fbf7ec",
    "slimEdge": "#eae1cc",
    "slimText": "#33312c",
    "dotActive": "#7c9a6b",
    "dotIdle": "#c4bba6",
    "dotEdge": "#eae1cc",
    "dotActiveHover": "#6e8a5d",
    "dotIdleHover": "#b0a78f",
    "dotEdgeHover": "#d8ceb9",
    "slimAlpha": 0.6
  },
  "tray": {
    "background": "#fbf7ec",
    "edge": "#eae1cc",
    "mark": "#7c9a6b",
    "badge": "#c8755e"
  }
}
```

### 8.3 校验规则

- `schema` 必须为数字 `1`。
- `id` 必须完整匹配 `[A-Za-z0-9._-]`，长度为 3 到 64。
- 自定义主题 id 必须以 `custom.` 开头。
- `id` 禁止包含 `..`，禁止以 `.` 或 `-` 结尾。
- 自定义主题不能覆盖内置 id。
- `name.zh` 与 `name.en` 必须存在，显示时长度截断到 32 个字符。
- 所有颜色必须完整匹配 `^#[0-9A-Fa-f]{6}$`。
- `slimAlpha` 必须在 `0.2` 到 `1.0`。
- 缺任一必填字段时整套主题无效。
- JSON 顶层未知字段记录 warning；`colors`、`capsule`、`tray` 内部未知字段作为 error，整套主题无效，避免拼写错误被静默忽略。
- reserved 字段检查递归且大小写不敏感。发现 `script`、`url`、`uri`、`image`、`path`、`file`、`command`、`css`、`font`、`layout`、`animation`、`hotkey` 时整套主题无效。

---

## §9 Theme Loader

### 9.1 ThemeIssue And ThemeNotice

文件加载问题与运行时主题问题分开记录。文件问题进入 `ThemeIssue`，系统明暗读取失败、主题 fallback、托盘图标生成失败、导出失败这类运行时问题进入 `ThemeNotice`。

```cpp
namespace Theme {

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
    std::wstring fileName;
    ThemeIssueSeverity severity;
    ThemeIssueKind kind;
    std::wstring detail;
};

struct ThemeNotice {
    std::wstring message;
};

}
```

issue UI 只展示文件名，不展示完整路径，也不展示原始 JSON 片段，避免暴露用户名、敏感目录结构或用户内容。

### 9.2 Loader 接口

```cpp
namespace Theme {

struct LoadResult {
    std::vector<ThemeVisual> themes;
    std::vector<ThemeIssue> issues;
};

std::wstring ThemeDirectory();
LoadResult LoadCustomThemes();
bool ExportTheme(const ThemeVisual& theme, const std::wstring& path, ThemeIssue* error);

}
```

`ThemeDirectory()` 使用 `%APPDATA%\x-todo\themes\`。目录不存在时，启动阶段可以不创建；用户打开主题管理或导出主题时再创建。

导出规则：

- 导出内置主题时，默认 id 改写为 `custom.exported-paper`、`custom.exported-graphite` 这类可重新加载的自定义 id。
- 导出自定义主题时保留 id。若目标目录已有同 id 文件，不覆盖，除非用户明确确认。
- 导出的 `.xtheme` 必须能被本 loader 重新加载并通过 schema 校验。
- 导出文件名不能直接使用 theme id 拼路径。文件名只允许 `[A-Za-z0-9._-]`，并通过独立安全文件名生成函数产生。

---

## §10 Persistence

### 10.1 Store Save

`Store::Save` 追加：

```text
ui theme_mode=builtin
ui theme_id=paper
ui light_theme_id=paper
ui dark_theme_id=graphite
```

保存顺序建议放在 `ui lang=` 后、胶囊字段前。

### 10.2 Store Load

`Store::Load` 的 `ui ` 分支增加：

- `theme_mode=`：只接受 `builtin`、`custom`、`follow_system`。
- `theme_id=`：只接受合法 id 字符集。
- `light_theme_id=`：只接受合法 id 字符集。
- `dark_theme_id=`：只接受合法 id 字符集。

非法主题字段保留默认值，不触发 corrupt。`item` 行继续正常加载。

实现必须把 `ui ` 行解析从多处 `line.find(L"...")` 升级为精确 `key=value`。禁止用 `line.find(L"theme_id=")` 直接解析主题字段，因为 `theme_id=` 会误匹配 `light_theme_id=` 和 `dark_theme_id=`。

蓝图：

```cpp
struct UiKeyValue {
    std::wstring key;
    std::wstring value;
    bool valid = false;
};

UiKeyValue ParseExactUiKeyValue(const std::wstring& body) {
    size_t eq = body.find(L'=');
    if (eq == std::wstring::npos || eq == 0) return {};
    UiKeyValue kv;
    kv.key = body.substr(0, eq);
    kv.value = body.substr(eq + 1);
    while (!kv.value.empty() && (kv.value.back() == L' ' || kv.value.back() == L'\r'))
        kv.value.pop_back();
    kv.valid = true;
    return kv;
}
```

`Store::Load` 中的 `ui ` 分支应按 key 精确分发：

```cpp
if (StartsWith(line, L"ui ")) {
    UiKeyValue kv = ParseExactUiKeyValue(line.substr(3));
    if (!kv.valid) continue;
    if (kv.key == L"theme_mode") { ... }
    else if (kv.key == L"theme_id") { ... }
    else if (kv.key == L"light_theme_id") { ... }
    else if (kv.key == L"dark_theme_id") { ... }
}
```

迁移主题字段时，已有 `completed_expanded`、`always_on_top`、`mount`、`lang`、`capsule_*` 字段也应逐步纳入同一个精确 key/value 分发，避免继续扩大子串匹配路径。

### 10.3 数据文件版本

保持 `XTODO v1` 容器格式即可。主题字段是附加 `ui key=value` 行，不需要改文件头。执行者不得因为本任务改变 `item` 行格式。

---

## §11 Theme Resolution

新增解析函数：

```cpp
namespace Theme {

struct ResolveInput {
    std::string mode;
    std::string themeId;
    std::string lightThemeId;
    std::string darkThemeId;
    bool systemDark;
    bool systemHighContrast;
    const std::vector<ThemeVisual>* customThemes;
};

struct ResolveResult {
    ThemeVisual theme;
    bool fellBack;
    std::wstring message;
};

ResolveResult ResolveTheme(const ResolveInput& input);

}
```

解析规则：

1. `builtin`：从内置主题表找 `themeId`。
2. `custom`：从自定义主题表找 `themeId`。
3. `follow_system`：`systemDark == true` 时解析 `darkThemeId`，否则解析 `lightThemeId`。
4. 解析失败时使用内置 `paper`。
5. 解析失败必须设置 `fellBack=true` 和 `message`。
6. `MainWindow` 保存 `message`，主题管理面板展示最近一次 fallback 原因。

主题 id 解析规则：

- id 以 `custom.` 开头时，只从自定义主题表解析。
- 其他 id 只从内置主题表解析。
- 自定义主题不能覆盖内置 id。
- `follow_system` 中的 `lightThemeId` 和 `darkThemeId` 同样遵守上述规则。

高对比策略：

- 若 `systemHighContrast == true` 且用户没有显式选择主题，解析到内置 `contrast`。
- 若用户已显式选择非高对比主题，保持用户选择，并新增 runtime notice，提示 Windows 高对比已开启，当前主题可能不可访问。
- `themeMode == follow_system` 时，`systemHighContrast == true` 优先解析 `contrast`，除非用户在主题管理窗口显式设置过高对比覆盖策略。第一版不实现高对比覆盖策略 UI。

菜单语义：

- 菜单中的具体内置主题项会设置 `themeMode=builtin` 并写 `themeId`。
- 菜单中的具体自定义主题项会设置 `themeMode=custom` 并写 `themeId`。
- 菜单中的 `Follow system` 只切换 `themeMode=follow_system`，不修改 `lightThemeId` 或 `darkThemeId`。
- 设置 `lightThemeId` 和 `darkThemeId` 只能在主题管理窗口中完成。

---

## §12 Applying Themes

### 12.1 MainWindow 新方法

`MainWindow.h` 增加：

```cpp
void ReloadThemes();
void ApplyResolvedTheme(bool persist);
void SetThemeMode(const std::string& mode);
void SetThemeId(const std::string& id);
void SetFollowSystemThemes(const std::string& lightId, const std::string& darkId);
const Theme::ThemeVisual& ThemeVisual() const { return theme_; }
```

`ApplyResolvedTheme` 流程：

1. 读取系统明暗模式。
2. 读取 Windows 高对比状态。
3. 调用 `Theme::ResolveTheme`。
4. 更新 `theme_`。
5. 删除 `editBg_` 并置空。
6. 若 `edit_` 正在显示，调用 `InvalidateRect(edit_, nullptr, TRUE)`。
7. 调用 `UpdateLayeredState()`，让 Slim 胶囊透明度使用新主题。
8. 调用 `RefreshTrayIcon()`，重建主题化托盘图标。
9. 若 T-A3 已实现任务栏状态条，刷新 `taskbarHwnd_`。
10. 若主题管理窗口已打开，通知它重绘。
11. `InvalidateRect(hwnd_, nullptr, FALSE)`。
12. `persist == true` 时 `ScheduleSave()`。

### 12.2 初始化顺序

`MainWindow::Create()` 建议顺序：

1. `Store::Load(model_, geom_, ui_)`。
2. `ReloadThemes()`。
3. `ApplyResolvedTheme(false)`。
4. 初始化语言、胶囊样式、挂载模式。
5. 创建 D2D 资源。
6. 添加托盘图标。
7. `ApplyMountMode()`。

若资源创建必须早于主题应用，执行者需要在 spec 复核后说明原因，并保证 `AddTrayIcon()` 使用到的主题已准备好。

---

## §13 Rendering Migration

### 13.1 MainWindowView

所有颜色读取改为 `theme_.colors` 或 `theme_.capsule`。

示例：

```cpp
rt_->Clear(Theme::D2DColor(theme_.colors.paper));
Text(s, r.text, theme_.colors.text, textFormat_);
```

折叠胶囊：

- Slim 背景：`theme_.capsule.slimPaper`
- Slim 边框：`theme_.capsule.slimEdge`
- Slim 数字：`theme_.capsule.slimText`
- Dot active：`theme_.capsule.dotActive`
- Dot idle：`theme_.capsule.dotIdle`
- Dot active hover：`theme_.capsule.dotActiveHover`
- Dot idle hover：`theme_.capsule.dotIdleHover`

`UpdateLayeredState()` 中 Slim alpha 改为 `theme_.capsule.slimAlpha`。

### 13.2 GDI Confirm

`ConfirmState` 增加主题快照：

```cpp
Theme::ThemeVisual theme;
```

`ShowThemedConfirm` 签名改为：

```cpp
bool ShowThemedConfirm(HWND owner, const wchar_t* text, Lang lang, bool danger,
                       const Theme::ThemeVisual& theme);
```

`DrawButton`、`ConfirmProc` 不再读取 `Theme::kDanger` 等颜色常量。

确认弹窗打开后使用打开时的主题快照。若系统明暗模式或高对比状态在确认弹窗打开期间变化，不强制刷新该弹窗；关闭后下次打开使用新主题。

### 13.3 GDI Popup Menu

`PopupMenuState` 增加主题快照：

```cpp
Theme::ThemeVisual theme;
```

`ShowPopupMenu` 签名改为：

```cpp
UINT ShowPopupMenu(HWND owner, POINT pt, const std::vector<PopupMenuItem>& items,
                   bool alignRight, const Theme::ThemeVisual& theme);
```

`PopupMenuItem` 建议扩展：

```cpp
enum class PopupItemKind {
    Command,
    Separator,
    Header,
    Radio,
    Check,
    SubtleCommand
};
```

主题组使用 Header + Radio，不继续把 header 伪装成 disabled command。

普通 popup 菜单打开后使用打开时的主题快照。若系统明暗模式或高对比状态在菜单打开期间变化，不强制刷新该菜单；关闭后下次打开使用新主题。

### 13.4 EDIT 子控件

`WM_CTLCOLOREDIT` 使用主题：

```cpp
SetBkColor(hdc, Theme::GdiColor(theme_.colors.paper));
SetTextColor(hdc, Theme::GdiColor(theme_.colors.text));
if (!editBg_) editBg_ = CreateSolidBrush(Theme::GdiColor(theme_.colors.paper));
return (LRESULT)editBg_;
```

切主题时必须：

```cpp
if (editBg_) {
    DeleteObject(editBg_);
    editBg_ = nullptr;
}
```

T-A4 第一轮只承诺原生 `EDIT` 的背景和文字跟随主题。插入光标、选区、IME 候选窗口由系统控制，不能通过 public theme schema 承诺稳定控制。深色主题下光标、选区和 IME 候选窗口必须作为 Windows 实机阻断项核验。若不可见，R2 前必须选择补救路线：RichEdit、自绘输入框，或深色主题编辑态临时使用系统色。

### 13.5 托盘图标

`CreateTrayIconHandle()` 改为按主题绘制小图标：

- 创建 16、24、32 像素兼容位图。
- 背景使用 `theme_.tray.background`。
- 边框使用 `theme_.tray.edge`。
- 勾选或短横使用 `theme_.tray.mark`。
- 有未完成项时可以加 `theme_.tray.badge` 小点。
- 生成失败时加载 `resources/app.ico`，记录错误。

Explorer 重启后 `AddTrayIcon()` 会重建图标，必须使用当前主题。

新增：

```cpp
void RefreshTrayIcon();
```

生命周期规则：

- 主题切换时生成新 `HICON`。
- 若 `trayAdded_ == true`，优先调用 `Shell_NotifyIconW(NIM_MODIFY, &nid_)`。
- `NIM_MODIFY` 成功后销毁旧 `nid_.hIcon`，并保存新 icon 到 `nid_.hIcon`。
- `NIM_MODIFY` 失败时销毁新 icon，保留旧 `nid_.hIcon`，记录 runtime notice。
- 动态图标生成失败时加载 `resources/app.ico`，并记录 runtime notice。
- fallback icon 也遵守同一所有权规则：成功交给 `nid_` 后由 `RemoveTrayIcon()` 或下次替换销毁；失败则立即 `DestroyIcon()`。
- Explorer 重启后的 `AddTrayIcon()` 必须使用当前 `theme_`。

---

## §14 System Theme Follow

### 14.1 系统明暗模式读取

读取注册表：

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
AppsUseLightTheme
```

规则：

- `AppsUseLightTheme == 0` 表示深色。
- `AppsUseLightTheme == 1` 表示浅色。
- 读取失败时按浅色处理，并记录 warning。

### 14.2 高对比状态读取

使用 `SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRASTW), &hc, 0)` 读取高对比状态。

规则：

- `HCF_HIGHCONTRASTON` 打开时，`ResolveInput::systemHighContrast = true`。
- 读取失败时按未开启处理，并新增 runtime notice。
- 高对比不是普通内置主题选择；它是系统可访问性状态，参与主题解析。

### 14.3 运行时变化

`WndProc` 增加或扩展 `WM_SETTINGCHANGE` 处理：

- 当 `themeMode == follow_system` 时重新读取系统明暗模式。
- 若 resolved theme id 变化，调用 `ApplyResolvedTheme(false)`。
- 对主窗口、胶囊、托盘图标、主题管理窗口、任务栏状态条同步刷新。

不要因为任意 `WM_SETTINGCHANGE` 都重载自定义主题目录。重载自定义主题由用户在主题管理面板触发。

`WndProc` 增加 `WM_SYSCOLORCHANGE` 处理：

- 重新读取高对比状态。
- 调用 `ApplyResolvedTheme(false)`。

`WM_THEMECHANGED` 第一版不强制处理。若实现中使用 UxTheme handles 或 themed common controls，则必须在收到 `WM_THEMECHANGED` 后释放并重新打开相关 theme handle。

第一版不使用 `RegNotifyChangeKeyValue`。除非 Windows 实机验证证明 `WM_SETTINGCHANGE` 漏掉应用明暗模式变化，否则不要引入注册表通知。

---

## §15 Menu And Theme Manager

### 15.1 菜单结构

托盘菜单和标题栏菜单都增加主题组：

```text
皮肤
  跟随系统
  暖纸
  薄荷
  天空
  玫瑰
  沙色
  石墨
  墨色
  高对比
  自定义主题...
```

英文：

```text
Theme
  Follow system
  Paper
  Mint
  Sky
  Rose
  Sand
  Graphite
  Ink
  High contrast
  Custom themes...
```

命令分区：

```cpp
constexpr UINT kCmdThemeFollowSystem = 1000;
constexpr UINT kCmdThemeBuiltinBase = 1100;
constexpr UINT kCmdThemeCustomBase = 1300;
constexpr UINT kCmdThemeManager = 1900;
constexpr UINT kCmdThemeReload = 1901;
```

自定义主题超过菜单可读数量时，只在菜单显示前 8 个，完整列表放在主题管理面板。

### 15.2 主题管理窗口

新增独立自绘工具窗口，类名建议 `XTodoThemeManagerWindow`。不要复用现有 `ShowPopupMenu` 线性菜单机制。现有 popup 菜单只适合单层命令列表，不适合主题列表、错误列表、按钮和文件操作入口。

窗口显示：

- 当前主题模式。
- 当前解析出的主题 id。
- 内置主题列表。
- 自定义主题列表。
- 加载 issue 列表。
- 运行时 notice 列表。
- 重新加载主题按钮。
- 打开主题目录按钮。
- 导出当前主题按钮。
- 将当前主题设为浅色跟随主题按钮。
- 将当前主题设为深色跟随主题按钮。

窗口行为：

- 点击内置或自定义主题立即应用并保存。
- 点击重新加载只重新扫描 `%APPDATA%\x-todo\themes\`。
- 点击打开主题目录使用 `ShellExecuteW` 打开目录。
- 点击导出当前主题生成 `.xtheme` 文件。
- 点击“设为浅色跟随主题”只写 `lightThemeId`。
- 点击“设为深色跟随主题”只写 `darkThemeId`。

主题管理窗口本身必须使用当前主题渲染。若主题变化发生在窗口打开期间，窗口应实时重绘。

---

## §16 I18n

`Str` 增加：

```cpp
ThemeHeader,
ThemeFollowSystem,
ThemePaper,
ThemeMint,
ThemeSky,
ThemeRose,
ThemeSand,
ThemeGraphite,
ThemeInk,
ThemeContrast,
ThemeCustom,
ThemeManager,
ThemeReload,
ThemeOpenFolder,
ThemeExportCurrent,
ThemeIssues,
ThemeNotices,
ThemeSetLightFollow,
ThemeSetDarkFollow,
ThemeFallbackNotice,
ThemeHighContrastNotice
```

中英文文案必须同步。品牌名 `X-TODO` 不翻译。

---

## §17 Security Rules

### MUST DO

- 文件大小限制为 64KB。
- 主题数量限制为 128。
- 只扫描普通文件。
- 扫描顺序按文件名排序，保证确定性。
- 重复 id 时，第一个合法文件生效，后续文件记录 `DuplicateId` error。
- 不递归扫描。
- 主题目录项带 `FILE_ATTRIBUTE_REPARSE_POINT` 时跳过。主题目录本身若是 reparse point，记录 notice 并跳过自定义主题加载。
- 颜色格式严格完整匹配 `^#[0-9A-Fa-f]{6}$`。
- 主题 id 严格完整匹配字符集和长度规则。
- UI 只展示文件名，不展示完整路径。
- 所有 JSON 字符串转宽字符时校验 UTF-8。
- 解析失败不影响待办数据。
- 导出主题时避免覆盖已有文件，除非用户明确确认。
- `colors`、`capsule`、`tray` 内部未知字段作为 error；顶层未知字段作为 warning。
- reserved 字段检查递归且大小写不敏感。

### MUST NOT DO

- 不执行主题文件中的任何内容。
- 不下载远程资源。
- 不读取主题文件中声明的路径。
- 不加载图片。
- 不把主题 id 拼接进文件路径。
- 不在错误文案中显示 access token、完整用户目录、原始 JSON 片段或系统敏感路径。
- 不允许主题文件控制字体、布局、动画、热键或窗口形态。

---

## §18 Implementation Plan

1. 新增主题数据结构和内置主题 catalog，先不接 UI。
2. 引入固定版本 `third_party/nlohmann/json.hpp`，补 MIT 许可说明。
3. 新增 `Theme::ResolveTheme`，把 `paper` 作为默认 resolved theme。
4. 修改 `MainWindow` 持有 `theme_` 快照。
5. 迁移 `MainWindowView.cpp` 的 Direct2D 主窗口颜色消费。
6. 迁移确认弹窗 GDI 绘制。
7. 迁移 popup 菜单 GDI 绘制，并升级 `PopupMenuItem` 类型。
8. 迁移 `WM_CTLCOLOREDIT` 和 `editBg_` 生命周期。
9. 迁移胶囊颜色与 Slim alpha。
10. 增加 `RefreshTrayIcon()`，迁移托盘图标生成。
11. 增加 `UiState` 字段，并把 `Store::Load` 的 `ui ` 分支升级为精确 key/value 解析。
12. 增加菜单主题命令和 i18n 文案。
13. 增加自定义主题 loader、issue、notice。
14. 增加 `ThemeManagerWindow`。
15. 增加系统明暗模式读取、Windows 高对比读取、`WM_SETTINGCHANGE`、`WM_SYSCOLORCHANGE` 响应。
16. 更新 README 双语文档。
17. 执行源码机械核查和 Windows 手工验证。

每一步提交前都应保证程序可编译。若环境无法编译，执行者必须至少给出机械核查命令和 Windows 端待验证清单。

---

## §19 Mechanical Acceptance Criteria

### 19.1 结构

- **[AC-1]** `CMakeLists.txt` 包含新增主题模块 `.cpp`。
- **[AC-2]** `src/Theme.h` 不再定义渲染层直接消费的 `kPaper`、`kText`、`kDanger` 等颜色常量。
- **[AC-3]** `ThemeVisual`、`ColorSet`、`CapsuleSet`、`TraySet` 存在。
- **[AC-4]** `ThemeCatalog` 至少定义 8 个内置主题。
- **[AC-5]** `ThemeLoader` 有文件大小、数量、id、颜色格式和 schema 校验。

### 19.2 渲染迁移

- **[AC-6]** `rg -n "Theme::k(Paper|PaperEdge|Text|TextWeak|TextDone|CheckBorder|CheckFill|CheckFillHover|CheckMark|Danger|Divider|Hover|Handle|HandleHover|DragGhost)\\b" src` 不能命中渲染消费路径。`Theme::kCheckSize` 等尺寸常量允许保留。
- **[AC-7]** `WM_CTLCOLOREDIT` 不再包含硬编码 `RGB(0xFB, 0xF7, 0xEC)` 或 `RGB(0x33, 0x31, 0x2C)`。
- **[AC-8]** `ConfirmState` 和 `PopupMenuState` 持有主题快照或等价不可变主题数据。
- **[AC-9]** `CreateTrayIconHandle()` 使用当前主题，且失败时有 fallback 和错误记录。
- **[AC-10]** `UpdateLayeredState()` 使用 `theme_.capsule.slimAlpha`。
- **[AC-10A]** `RefreshTrayIcon()` 存在，并处理 `NIM_MODIFY`、旧 `HICON` 销毁、新 `HICON` 失败回滚。

### 19.3 持久化

- **[AC-11]** `UiState` 包含 `themeMode`、`themeId`、`lightThemeId`、`darkThemeId`。
- **[AC-12]** `Store::Load` 和 `Store::Save` 对四个主题字段读写对称。
- **[AC-13]** 非法主题字段不触发 corrupt 备份路径。
- **[AC-14]** 缺主题字段的数据文件可正常加载待办项。
- **[AC-14A]** `Store::Load` 主题字段使用精确 key/value 解析，`rg -n "line\\.find\\(L\\\".*theme_id" src/Store.cpp` 无命中。

### 19.4 UI

- **[AC-15]** 托盘菜单和标题栏菜单都包含主题组。
- **[AC-16]** 主题管理窗口可列出内置主题、自定义主题、加载 issue 和运行时 notice。
- **[AC-17]** 主题管理窗口可以重新加载主题目录。
- **[AC-18]** 中英文文案完整，不出现空字符串。
- **[AC-18A]** 主题管理窗口可以设置 `lightThemeId` 和 `darkThemeId`，菜单具体主题项会退出 `follow_system`。

### 19.5 安全

- **[AC-19]** 主题 loader 不读取主题 JSON 中声明的任意路径。
- **[AC-20]** 主题 loader 不发起网络请求。
- **[AC-21]** 主题错误 UI 不展示完整主题文件路径。
- **[AC-22]** 自定义主题 id 不能覆盖内置主题 id。
- **[AC-23]** `WM_SYSCOLORCHANGE`、`SPI_GETHIGHCONTRAST` 至少各有一处实现引用。
- **[AC-24]** 导出内置主题时生成 `custom.exported-*` id，导出文件可重新加载。

---

## §20 Windows Manual Acceptance

### 20.1 基础主题切换

1. 启动程序，默认主题为 `paper`。
2. 从标题栏菜单切换 `mint`，主窗口颜色变化。
3. 从托盘菜单切换 `graphite`，主窗口、菜单、确认弹窗同步变化。
4. 重启程序，恢复上次主题。

### 20.2 编辑状态

1. 新增一条待办并保持编辑框打开。
2. 打开菜单切换主题。
3. 编辑框背景、文字、光标、选区保持可见。
4. 回车提交后文本颜色与列表一致。

### 20.3 弹窗和菜单

1. 删除一条待办，确认弹窗使用当前主题。
2. 在深色主题下，确认按钮、取消按钮、危险色可见。
3. 菜单 hover、checked、disabled、header 视觉清晰。

### 20.4 胶囊

1. 切换到 Capsule Slim，折叠态背景、边框、数字使用当前主题。
2. Slim 静止透明度使用主题 `slimAlpha`。
3. Slim hover 后不透明或主题指定视觉正常。
4. 切换到 Dot，active、idle、hover 色使用当前主题。
5. 拖动吸附后主题状态不丢失。

### 20.5 自定义主题

1. 在 `%APPDATA%\x-todo\themes\` 放入合法 `.xtheme`。
2. 点击重新加载，主题出现在管理面板。
3. 应用自定义主题，所有 UI 表面同步变化。
4. 放入非法 JSON，程序不崩溃，错误出现在管理面板。
5. 删除当前自定义主题文件并重新加载，程序回退到 `paper`，错误可见。

### 20.6 系统跟随

1. 设置为跟随系统。
2. Windows 应用模式为浅色时使用 `lightThemeId`。
3. Windows 应用模式为深色时使用 `darkThemeId`。
4. 切换系统模式后，不重启程序即可更新主题。

### 20.7 托盘图标

1. 切换主题后托盘图标同步更新。
2. Explorer 重启后托盘图标恢复为当前主题。
3. 动态图标生成失败时，程序仍有可用托盘图标。

### 20.8 DPI And Multi-monitor

1. 100%、125%、150%、200% DPI 下切换主题，主窗口、菜单、确认弹窗、胶囊、托盘图标不模糊、不错位。
2. 混合 DPI 多显示器中移动窗口后切换主题，编辑框与菜单仍对齐。
3. 胶囊在不同显示器边缘吸附后切换主题，位置与透明度不丢失。

### 20.9 High Contrast And IME

1. Windows 高对比打开后启动程序，文字、按钮、菜单 hover、编辑框可见。
2. 运行中切换高对比，主窗口和主题管理窗口刷新。
3. 深色主题下使用中文 IME 输入、选词、提交、取消编辑。
4. 深色主题下拖选文字、Shift+Arrow 选择文字，光标和选区均可见。若不可见，必须按 §13.4 选择补救路线。

### 20.10 Theme File Boundaries

1. 非法 UTF-8 文件被拒绝，程序不崩溃。
2. 65KB 文件被拒绝。
3. 第 129 个主题被拒绝或跳过，并记录 issue。
4. 重复 id 行为确定，第一个合法文件生效，后续重复 id 记录 issue。
5. 缺字段、颜色拼写错误、嵌套 reserved 字段都被拒绝。
6. 主题目录内 junction、symlink、reparse point 被跳过。
7. 只读或被占用文件不会阻塞启动。
8. 导出内置主题后重新加载，作为 `custom.*` 主题出现。
9. 菜单或确认弹窗打开期间切换系统明暗模式，已打开弹窗可保持旧主题；关闭后重新打开使用新主题。

---

## §21 GPT 5.5 Pro v1.1 Review Questions

请重点复核以下问题：

1. `ThemeVisual` 是否覆盖了当前所有颜色语义，是否仍存在缺失角色或过度抽象。
2. 固定版本 `nlohmann/json.hpp` 与 MIT 许可说明是否足够，是否还需要 vendor 版本号记录。
3. 自定义主题 schema 是否足够严格，是否还存在路径读取、资源加载、错误信息泄露或过度防护。
4. 深色主题下原生 `EDIT` 子控件是否存在光标、选区、输入法候选框或系统高对比冲突。
5. 托盘图标按主题动态生成的生命周期规则是否足够，是否还有 `HICON` 泄漏边界。
6. `follow_system` 响应 `WM_SETTINGCHANGE`、高对比响应 `WM_SYSCOLORCHANGE` 是否足够，是否仍应避免注册表通知。
7. `ThemeManagerWindow` 的独立窗口方案是否与当前 Win32 架构匹配。
8. 若 T-A3 任务栏模式先落地，字段映射是否足够接入任务栏状态条。
9. `Store` 继续使用 `XTODO v1` 容器格式并升级精确 key/value 解析是否合理。
10. 验收矩阵是否还缺少 DPI、多显示器、Explorer 重启、IME、reparse point 或异常文件边界。

---

## §22 Verification Commands

源码机械核查建议：

```powershell
rg -n "Theme::k(Paper|PaperEdge|Text|TextWeak|TextDone|CheckBorder|CheckFill|CheckFillHover|CheckMark|Danger|Divider|Hover|Handle|HandleHover|DragGhost)\b" src
rg -n "RGB\(0xFB|RGB\(0x33|CreateSolidBrush\(RGB" src
rg -n "theme_mode|theme_id|light_theme_id|dark_theme_id" src/Store.*
rg -n "line\.find\(L\".*theme_id" src/Store.cpp
rg -n "ThemeVisual|ColorSet|CapsuleSet|TraySet|LoadCustomThemes|ResolveTheme" src
rg -n "WM_SETTINGCHANGE|AppsUseLightTheme|WM_SYSCOLORCHANGE|SPI_GETHIGHCONTRAST" src
rg -n "DestroyIcon|Shell_NotifyIconW\(NIM_MODIFY|RefreshTrayIcon" src/MainWindow.cpp
rg -n "custom\.exported|ExportTheme" src/Theme* src/MainWindow.cpp
rg -n "http://|https://|ShellExecute|CreateFileW" src/Theme* src/MainWindow.cpp
```

Windows 编译：

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

若实现者在 Linux/WSL 环境无法编译，必须报告“机械验证到源码结构；Windows 编译与实机视觉待用户验证”。

---

## §23 Non-goals And Stop Conditions

### Non-goals

- 不设计主题市场。
- 不支持 CSS。
- 不支持图片背景。
- 不允许主题控制布局或动画。
- 不在本任务中重写 TodoModel。
- 不在本任务中实现 T-A3 任务栏模式。

### Stop Conditions

遇到以下情况必须停下并回报：

1. `nlohmann/json` 固定版本或 MIT 许可说明无法落地。
2. 深色主题下原生 EDIT 无法保证文字或光标可见。
3. 动态托盘图标在当前 Win32 图标路径中产生资源泄漏。
4. 主题 loader 需要读取主题目录外的文件。
5. 主题管理窗口需要引入非 Win32 原生 UI 框架。
6. 实现中发现主题字段会导致待办数据被覆盖或清空。
