<div align="center">

<img src=".assets/logo.svg" width="96" alt="X-TODO">

# X-TODO

**一张停在桌面上的待办便签。**

[![Build](https://github.com/xwysyy/X-TODO/actions/workflows/build.yml/badge.svg)](https://github.com/xwysyy/X-TODO/actions)
![Platform](https://img.shields.io/badge/Windows-10%20|%2011-0078D4?logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B17-Win32%20+%20Direct2D-00599C?logo=cplusplus&logoColor=white)
[![Size](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fxwysyy%2FX-TODO%2Fbadges%2Fsize-badge.json&cacheSeconds=3600)](https://github.com/xwysyy/X-TODO/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English](README.md) | **简体中文**

</div>

---

X-TODO 是一个常驻桌面的待办便签。一张无边框纸条停在桌面上，一行一条待办，点方框勾选，内容自动保存、重启后照常显示，无需登录或联网。整个程序是单个 exe，常驻内存保持在个位数 MB。

<div align="center">
<table>
<tr>
<td align="center"><img src=".assets/screenshot.png" width="320" alt="X-TODO 截图"></td>
<td align="center"><img src=".assets/demo.gif" width="320" alt="X-TODO 演示"></td>
</tr>
</table>
</div>

## 特性

- **一行一条待办**，点前面的方框勾掉，回车接着写下一条
- **多级待办**，编辑时按 Tab 缩进，按 Shift+Tab 退级，父项可折叠子项
- **拖动排序**，拖动条目时带着子项一起移动，完成的归进可折叠的「已完成」区，每次删除都会先确认
- **三种摆放**：普通窗口、沉进桌面、折叠成侧边胶囊（鼠标碰一下滑出）
- **自动保存**，重启后原样还在，数据就是一个文本文件
- **托盘常驻**，关窗收起、双击叫回，可设开机自启
- **多套主题**，内置浅色 / 深色 / 高对比，可跟随系统明暗，也能加自定义主题文件
- **纯 Win32 + Direct2D**，不带 .NET、不带浏览器内核，单文件免安装

## 下载

打开 [Releases](https://github.com/xwysyy/X-TODO/releases)，下载最新版本里的 `x-todo.exe`，双击运行。绿色单文件，免安装。开发构建仍可在 [Actions](https://github.com/xwysyy/X-TODO/actions) 的 Artifacts 里下载。

> 待办数据存在 `%APPDATA%\x-todo\`。卸载程序不影响它；换电脑时把这个文件夹拷过去，待办就跟着走。

## 用法

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

## 主题

内置八套主题：暖纸、薄荷、天空、玫瑰、沙色、石墨、墨色、高对比。右键托盘图标或点标题栏的菜单按钮，在「皮肤」组里直接切换。选「跟随系统」时，按 Windows 的应用明暗模式自动套浅色或深色主题；系统开了高对比，会切到高对比主题。换主题时主窗口、侧边胶囊、菜单、确认框和行内编辑框会跟着变，托盘保持固定应用图标。

### 自定义主题

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

颜色都写成 `#rrggbb`。完整字段（`colors` 21 项、`capsule` 10 项、`tray` 4 项）较多，在主题管理窗口点「导出当前主题」会生成一份可直接改的样板，比手写省事。

## 构建

推到 GitHub 后，Actions 会用 MSVC 自动编译，产物在对应运行的 Artifacts 里。推送 `v0.1.0` 这类版本 tag 时，还会创建 GitHub Release 并附上 exe。

要自己编译，在装了 Visual Studio（带「使用 C++ 的桌面开发」）的 Windows 上：

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

技术栈：C++17、Win32、Direct2D / DirectWrite，CMake 构建，静态链接 CRT，产出单个 exe。

## License

[MIT](LICENSE)

JSON 解析使用 [nlohmann/json](https://github.com/nlohmann/json) v3.11.3（MIT 许可），单头文件 vendored 在 `third_party/nlohmann/`。
