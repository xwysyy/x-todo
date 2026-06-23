<div align="center">

<img src=".assets/logo.svg" width="96" alt="X-TODO">

# X-TODO

**把今天钉在桌面上。**

[![Build](https://github.com/xwysyy/X-TODO/actions/workflows/build.yml/badge.svg)](https://github.com/xwysyy/X-TODO/actions)
![Platform](https://img.shields.io/badge/Windows-10%20|%2011-0078D4?logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B17-Win32%20+%20Direct2D-00599C?logo=cplusplus&logoColor=white)
[![Size](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fxwysyy%2FX-TODO%2Fbadges%2Fsize-badge.json&cacheSeconds=3600)](https://github.com/xwysyy/X-TODO/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English](README.md) | **简体中文**

</div>

---

<div align="center">
<table>
<tr>
<td align="center"><img src=".assets/tasks.png" width="250" alt="待办"><br><sub><b>待办</b></sub></td>
<td align="center"><img src=".assets/calendar.png" width="250" alt="日历"><br><sub><b>日历</b></sub></td>
<td align="center"><img src=".assets/capsule.gif" width="250" alt="侧边胶囊"><br><sub><b>侧边胶囊</b></sub></td>
</tr>
</table>
</div>

## 📌 X-TODO 是什么

**X-TODO** 是一个常驻桌面的 Windows 轻量计划工具，把你的待办清单和一天的时间安排放在同一张便签里。用**多级大纲**写下任务，再切到**日历**，把它们排进真实的日 / 周 / 月时间轴。需要屏幕时，把整个便签折成屏幕边缘的**侧边胶囊**。它是单个原生 exe，所有数据存进一个本地文件，重启后停在你离开的地方。

## ✨ 特性

- **多级大纲待办。** 一行一条，最多三级缩进。父项可折叠，拖动手柄带子项一起排序，完成的归进可折叠的「已完成」区。
- **日 / 周 / 月日历。** 在日视图时间轴上拖一段就挡出时间，拖块身移动、拖边缘缩放，都吸附到 15 分钟。周和月是总览，点任意一天就钻进去编辑。
- **三种桌面形态。** 普通窗口、沉进桌面，或折成侧边胶囊，鼠标碰一下滑出。
- **本地优先存储。** 所有数据是 `%APPDATA%` 下的一个 JSON 文件，可在「设置」里选目录约每小时自动备份。
- **多套主题。** 内置五套浅色主题，支持自定义 `.xtheme` 文件，也能跟随系统明暗。
- **托盘常驻。** 关窗收起，双击叫回，可设开机自启。
- **原生轻量。** 纯 Win32 + Direct2D，单个 exe，约 1 MB。

## 📥 下载

打开 [Releases](https://github.com/xwysyy/X-TODO/releases)，下载最新版本里的 `x-todo.exe`，双击运行。绿色单文件，免安装。开发构建仍可在 [Actions](https://github.com/xwysyy/X-TODO/actions) 的 Artifacts 里下载。

> 待办数据存在 `%APPDATA%\x-todo\data.json`。「设置」里可以把这个文件镜像到 `<备份目录>\data.json`。卸载程序不影响应用数据目录；换电脑时把这个目录拷过去，待办就跟着走。

## ⌨️ 用法

| 操作 | 怎么做 |
| :-- | :-- |
| 新增 | 点底部「＋ 新增一条」，或编辑时按回车在当前条目及其子项后插入 |
| 完成 / 还原 | 点条目前的方框；有子项时整棵子树一起移动 |
| 编辑 | 点条目文字直接改 |
| 缩进 / 退级 | 编辑未完成条目时按 Tab 或 Shift+Tab |
| 折叠子项 | 点父项前面的展开箭头 |
| 重命名 / 删除标签页 | 双击标签页重命名，右键标签页打开重命名 / 删除菜单 |
| 删除 | 悬停条目点 ×，确认后删除；子项会一起删掉 |
| 排序 | 按住条目右侧的手柄上下拖；子项会跟着移动 |
| 移动 / 缩放 | 拖标题栏移动，拖窗口边缘缩放 |
| 换摆放方式 | 右键托盘图标，选普通窗口 / 挂到桌面 / 侧边胶囊 |
| 打开日历 | 点标题栏的日历按钮，在清单和日历之间切换 |
| 安排时间 | 在日视图空白处拖出一段创建时间块，拖块身移动、拖边缘缩放 |
| 编辑时间块 | 点时间块改标题和起止时间，右键删除 |
| 切换日历视图 | 用日历顶部的「日 / 周 / 月」切换，在周或月里点某一天进入当天 |
| 设置 | 从标题栏菜单或托盘菜单打开「设置」，修改语言、开机自启和备份目录 |
| 备份 | 在「设置」里选择目录；程序会约每小时覆盖该目录下的 `data.json` |

## 🎨 主题

内置五套主题：暖纸、薄荷、天空、玫瑰、沙色。右键托盘图标或点标题栏的菜单按钮，在「皮肤」组里直接切换。选「跟随系统」时，按 Windows 的应用明暗模式自动切换主题。换主题时主窗口、菜单、确认框、设置窗口、主题管理窗口和行内编辑框会跟着变。折叠侧边入口和托盘保持固定应用颜色。

### 🖌️ 自定义主题

把 `.xtheme` 文件放进 `%APPDATA%\x-todo\themes\`，在主题管理窗口点「重新加载」即可使用。文件是严格 JSON，只描述颜色和显示名，不能引用图片、脚本、网址或外部路径。结构示意：

```json
{
  "schema": 1,
  "id": "custom.morning-paper",
  "name": { "zh": "晨纸", "en": "Morning Paper" },
  "dark": false,
  "colors": { "paper": "#fbf7ec", "text": "#33312c", "checkFill": "#7c9a6b", "danger": "#c8755e" },
  "capsule": { "slimPaper": "#fbf7ec", "slimAlpha": 0.6 },
  "tray": { "background": "#fbf7ec", "mark": "#7c9a6b" }
}
```

颜色都写成 `#rrggbb`。完整字段（`colors` 21 项、`capsule` 10 项、`tray` 4 项）较多，在主题管理窗口点「导出当前主题」会生成一份可直接改的样板，比手写省事。折叠侧边入口使用固定应用颜色，`capsule` 仍保留在主题格式里。

## 🛠️ 构建

推到 GitHub 后，Actions 会用 MSVC 自动编译，产物在对应运行的 Artifacts 里。推送 `v0.1.0` 这类版本 tag 时，还会创建 GitHub Release 并附上 exe。

要自己编译，在装了 Visual Studio（带「使用 C++ 的桌面开发」）的 Windows 上：

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

技术栈：C++17、Win32、Direct2D / DirectWrite，CMake 构建，静态链接 CRT，产出单个 exe。

## 📄 License

[MIT](LICENSE)

JSON 解析使用 [nlohmann/json](https://github.com/nlohmann/json) v3.11.3（MIT 许可），单头文件 vendored 在 `third_party/nlohmann/`。
