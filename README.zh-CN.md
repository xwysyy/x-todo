<div align="center">

# X-TODO

**一张停在桌面上的待办便签。**

[![Build](https://github.com/xwysyy/X-TODO/actions/workflows/build.yml/badge.svg)](https://github.com/xwysyy/X-TODO/actions)
![Platform](https://img.shields.io/badge/Windows-10%20|%2011-0078D4?logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B17-Win32%20+%20Direct2D-00599C?logo=cplusplus&logoColor=white)
![Size](https://img.shields.io/badge/exe-~240_KB-2EA043)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English](README.md) | **简体中文**

</div>

---

X-TODO 是一个常驻桌面的待办便签。一张无边框纸条停在桌面上，一行一条待办，点方框勾选，内容自动保存、重启后照常显示，无需登录或联网。整个程序是单个 exe，常驻内存保持在个位数 MB。

<!-- 跑起来后截一张图，存成 docs/screenshot.png，再放开下面这行 -->
<!-- <div align="center"><img src="docs/screenshot.png" width="320"></div> -->

## 特性

- **一行一条待办**，点前面的方框勾掉，回车接着写下一条
- **拖动排序**，完成的归进可折叠的「已完成」区，每次删除都会先确认
- **三种摆放**：普通窗口、沉进桌面、折叠成侧边胶囊（鼠标碰一下滑出）
- **自动保存**，重启后原样还在，数据就是一个文本文件
- **托盘常驻**，关窗收起、双击叫回，可设开机自启
- **纯 Win32 + Direct2D**，不带 .NET、不带浏览器内核，单文件免安装

## 下载

打开 [Actions](https://github.com/xwysyy/X-TODO/actions)，点最近一次绿色构建，在页面底部 **Artifacts** 里下载 `x-todo`，解压后双击 `x-todo.exe`。绿色单文件，免安装。

> 待办数据存在 `%APPDATA%\x-todo\`。卸载程序不影响它；换电脑时把这个文件夹拷过去，待办就跟着走。

## 用法

| 操作 | 怎么做 |
| :-- | :-- |
| 新增 | 点底部「＋ 新增一条」，输入后回车继续下一条 |
| 完成 / 还原 | 点条目前的方框；已完成项再点一次还原回来 |
| 编辑 | 点条目文字直接改 |
| 删除 | 悬停条目点 ×，确认后删除 |
| 排序 | 按住条目右侧的手柄上下拖 |
| 移动 / 缩放 | 拖标题栏移动，拖窗口边缘缩放 |
| 换摆放方式 | 右键托盘图标，选普通窗口 / 挂到桌面 / 侧边胶囊 |

## 构建

推到 GitHub 后，Actions 会用 MSVC 自动编译，产物在对应运行的 Artifacts 里，本地什么都不用装。

要自己编译，在装了 Visual Studio（带「使用 C++ 的桌面开发」）的 Windows 上：

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

技术栈：C++17、Win32、Direct2D / DirectWrite，CMake 构建，静态链接 CRT，产出单个 exe。

## License

[MIT](LICENSE)
