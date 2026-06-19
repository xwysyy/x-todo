# T-A4 实现总结：全面主题与皮肤系统

> 配套任务规格见 [T-A4.md](T-A4.md)。本文记录实际实现范围、关键决策、与规格的偏差、codex 审查迭代结果，以及 Windows 实机待验证项，供后续维护与复审参考。
>
> 开发环境为 Linux/WSL，无法实机编译。交付层级为：源码结构 + 机械核查通过 + Windows 实机视觉核验待验证。

## 概述

X-TODO 从固定浅色纸张视觉重构为完整主题系统。渲染层不再消费全局颜色常量，所有 surface（主窗口、折叠胶囊、菜单、确认弹窗、行内编辑框、托盘图标、任务栏状态条）统一读取解析后的 `ThemeVisual` 快照。主题来源包括 8 套内置主题、`%APPDATA%\x-todo\themes\` 下的自定义 `.xtheme` 文件、以及系统明暗模式与高对比跟随。

## 实现范围（7 个阶段）

1. 主题数据模型 + 内置主题 catalog：重写 `Theme.h`，新增 `Theme.cpp` / `ThemeCatalog.{h,cpp}`，vendoring `nlohmann/json` v3.11.3 单头。
2. `Theme::ResolveTheme` + `MainWindow` 持有 `theme_` 快照，调整 `Create()` 初始化顺序使快照在托盘图标生成与首帧渲染前就绪。
3. 渲染迁移：`MainWindowView` 主窗口与胶囊、确认弹窗、popup 菜单、`WM_CTLCOLOREDIT`、托盘图标、任务栏状态条全部切到主题快照。
4. 持久化：`UiState` 增加四个主题字段，`Store::Load` 的 `ui` 分支从子串匹配重写为精确 key/value 分发。
5. 系统跟随：注册表 `AppsUseLightTheme` 与 `SPI_GETHIGHCONTRAST` 读取，`WM_SETTINGCHANGE` / `WM_SYSCOLORCHANGE` 接入主题解析。
6. 自定义主题 loader、主题管理窗口、菜单主题组、i18n 约 21 条双语文案。
7. README 双语 + nlohmann/json MIT 许可说明 + 机械核查 + codex 审查迭代。

### 文件清单

新增（src）：`Theme.cpp`、`ThemeCatalog.{h,cpp}`、`ThemeLoader.{h,cpp}`、`ThemeManagerWindow.{h,cpp}`；`third_party/nlohmann/json.hpp`（vendored，v3.11.3，MIT）。

重写或修改：`Theme.h`、`MainWindow.{h,cpp}`、`MainWindowView.cpp`、`MainWindowTaskbar.cpp`、`Store.{h,cpp}`、`I18n.{h,cpp}`、`CMakeLists.txt`、`README.md`、`README.zh-CN.md`。

规模：20 个文件，约 3462 行（不含 vendored `json.hpp` 的 24765 行）。

## 关键决策

- **彻底重构颜色消费路径**：`Theme.h` 删除 `kPaper` / `kText` / `kDanger` 等渲染消费的全局颜色常量，仅保留尺寸与字体常量。`Theme::Color` 改名 `D2DColor`（保留 inline 别名过渡）。
- **vendoring nlohmann/json**：规格将其列为 locked decision，且自写 JSON 解析器会给安全校验引入高风险攻击面，因此选择 vendoring 固定版而非手写。
- **8 套内置主题写死最终消费色**：`paper`、`mint`、`sky`、`rose`、`sand`、`graphite`、`ink`、`contrast`，每套完整定义 `ColorSet` / `CapsuleSet` / `TraySet`，不在渲染处混 alpha。配色由 WCAG 对比度脚本验证：正文/背景 ≥ 7:1、弱文本 ≥ 4.5:1、危险色 ≥ 4.5:1、对勾/填充 ≥ 3:1，硬性项全部达标。
- **精确 key/value 解析**：`ui` 行解析升级为 `ParseExactUiKeyValue` 分发，消除 `theme_id` 被 `light_theme_id` / `dark_theme_id` 误命中，已有字段一并纳入。
- **托盘图标主题化 + 生命周期**：`CreateTrayIconHandle` 按主题 GDI 绘制，`RefreshTrayIcon` 处理 `NIM_MODIFY`、旧 `HICON` 销毁、新 `HICON` 失败回滚，生成失败回退 `app.ico`。
- **自定义主题是数据不是插件**：严格 JSON schema 校验，颜色 `^#[0-9A-Fa-f]{6}$`、id 字符集与长度、reserved 字段递归大小写不敏感、UTF-8 校验、无网络、不读外部路径、不加载图片。

## 与规格的偏差及处理

- **base commit 不匹配**：规格写于 `3a86764`，实际仓库已到 `72d5773`（确认弹窗已紧凑化、新增 `MeasureConfirm` 等）。规格中所有 `文件:行号` 锚点不可信，实现以实际源码为准重新核对。
- **T-A3 任务栏状态条已落地**：`MainWindowTaskbar.cpp` 在规格正文未提及但已存在，按规格 §3.10 将其 `PaintTaskbarBand` 的硬编码 RGB 映射到 `paper` / `paperElevated` / `buttonHover` / `buttonPressed` / `paperEdge` / `text`。
- **执行顺序重排**：`ApplyResolvedTheme` 对 `ui_` 字段与 i18n 枚举有编译期硬依赖，故先实现 Store 字段与 i18n 文案，再实现 MainWindow 主题方法，避免悬空引用。

## Codex 审查迭代（4 轮对抗式，--model gpt-5.5）

逐轮独立复审（不喂上轮意见，仅标版本指纹防 stale-cache），findings 从 high 收敛到设计权衡：

- **第 1 轮**（1 high + 2 medium）：导出路径越界（空 `destDir` / 跟随 reparse）；128 上限在循环内只数成功加载，坏文件仍被全量解析；系统读取失败被静默当浅色/非高对比。全部修复。
- **第 2 轮**（1 high + 2 medium）：reserved 递归与 `json::parse` 对深嵌套可栈溢出；候选文件无界收集后才截断；reparse 检查 path-based，枚举与打开之间可被换成 symlink。修复为 parse 前字符级深度闸门（`MaxBracketDepth` ≤ 16）、枚举硬上限（`kScanLimit` = 512）、`FILE_FLAG_OPEN_REPARSE_POINT` + handle 属性校验。
- **第 3 轮**（0 high + 2 medium）：导出仍 path-raceable；高对比首启未覆盖默认用户。修复为 handle 绑定导出（打开目录 handle 校验 + `CREATE_NEW` 后 `GetFinalPathNameByHandleW` 确认父目录一致）、高对比覆盖扩展到默认 paper。
- **第 4 轮**（0 high + 1 medium）：指出第 3 轮的高对比修复过宽，会误覆盖用户显式选择的 Paper。已回退到规格 §11 第二条的安全实现。

`git diff --cached --check` 干净。

## 待定产品决策（已选择保持当前）

系统已开高对比、用户首次启动（默认 `builtin/paper`）时是否自动切 contrast，存在规格内在张力：§11 要求区分「默认 Paper」与「显式选 Paper」，但 §6.2 的字段集没有「是否显式选择过」的持久化状态，无法在不加字段的前提下精确区分。

当前实现（保持）：`follow_system` 模式高对比优先 contrast；`builtin` / `custom` 模式视为用户已选择，高对比下保持选择并由 `MainWindow` 记录可见 notice 提示当前主题可能不可访问。首启默认 paper 在高对比下保留 paper 加 notice，不自动切 contrast。

后续可选增强（如需精确实现 §11 第一条）：增加一个持久化「已显式选择主题」标记，或把默认 `themeMode` 改为 `follow_system`。两者都会引入字段或默认行为变化，留作产品决策。

## 机械核查（规格 §22）

逐条通过：AC-2（`Theme.h` 无渲染消费颜色常量）、AC-3（四个主题结构存在）、AC-4（8 套内置主题）、AC-6（渲染层无颜色常量残留）、AC-7（`WM_CTLCOLOREDIT` 无硬编码 RGB）、AC-10A（`RefreshTrayIcon` + `NIM_MODIFY` + 回滚）、AC-11/12（四主题字段读写对称）、AC-14A（精确解析，无 `line.find` theme_id）、AC-20（无网络）、AC-23（`WM_SYSCOLORCHANGE` + `SPI_GETHIGHCONTRAST`）、AC-24（`custom.exported-*` 导出）。

## Windows 实机待验证（规格 §20）

Linux/WSL 无法编译，以下需在 Windows MSVC 实机核验：

- 编译：`cmake -B build -A x64` + `cmake --build build --config MinSizeRel`。
- 基础切换、重启恢复主题。
- 深色主题下行内编辑框的文字、插入光标、选区、中文 IME 候选窗口可见性（规格 §13.4 列为阻断项，不可见时需选补救路线）。
- 托盘图标主题化、`NIM_MODIFY` 更新、Explorer 重启重建、动态生成失败回退。
- follow-system 明暗切换、高对比开关、`WM_SETTINGCHANGE` / `WM_SYSCOLORCHANGE` 响应。
- 自定义主题加载、非法文件拒绝、reparse / junction 跳过、导出后重新加载。
- 导出的 handle 绑定校验（`GetFinalPathNameByHandleW`、`FILE_FLAG_BACKUP_SEMANTICS`）行为。
- 100% / 125% / 150% / 200% DPI 与多显示器。

## 合并到 main（2026-06-19）

主题系统从隔离分支 `worktree-t-a4-themes` 移植到 `main`。该分支基于旧 base（`72d5773`），`main` 此后已演进 18 个提交，移植时按 `main` 当前架构做了如下适配：

- **丢弃任务栏嵌入**：`main` 已删除 taskbar embed（无 `MainWindowTaskbar.cpp`、无 `MountMode::Taskbar`、无 taskbar 持久化字段与文案）。主题系统原本携带的 taskbar 相关改动（`PaintTaskbarBand` 配色映射、taskbar `UiState` 字段、taskbar i18n、`mount=taskbar` 解析）全部不带入；`mount=taskbar` 旧配置在 `Store::Load` 中回退为 `normal`。
- **弹窗 / 确认框改 Direct2D**：`main` 已把 popup 菜单与确认弹窗从 GDI 重写为 Direct2D。主题不再用本分支的 GDI 绘制路径，而是把 `ThemeVisual` 快照穿进 `main` 的 Direct2D `ConfirmState` / `PopupMenuState`，颜色由删除的全局常量改读 `theme_.colors.*`。
- **渲染迁移直接采用本分支的 `MainWindowView.cpp`**（已完成颜色迁移，且已含 `main` 的 `OnNcHitTest` 标题栏按钮优先改动），仅去掉一行 taskbar 状态条刷新调用。

主窗口、胶囊、菜单、确认弹窗、行内编辑框、托盘图标的主题化与系统明暗 / 高对比跟随均已落地。仍需 Windows 实机视觉核验（见上文「Windows 实机待验证」）。
