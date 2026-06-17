<div align="center">

<img src=".assets/logo.svg" width="96" alt="X-TODO">

# X-TODO

**A sticky to-do note that lives on your desktop.**

[![Build](https://github.com/xwysyy/X-TODO/actions/workflows/build.yml/badge.svg)](https://github.com/xwysyy/X-TODO/actions)
![Platform](https://img.shields.io/badge/Windows-10%20|%2011-0078D4?logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B17-Win32%20+%20Direct2D-00599C?logo=cplusplus&logoColor=white)
[![Size](https://img.shields.io/endpoint?url=https%3A%2F%2Fraw.githubusercontent.com%2Fxwysyy%2FX-TODO%2Fbadges%2Fsize-badge.json&cacheSeconds=3600)](https://github.com/xwysyy/X-TODO/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

**English** | [简体中文](README.zh-CN.md)

</div>

---

X-TODO is an always-on desktop to-do note. A frameless slip stays on your desktop: one line per task, click the checkbox to mark it done. Edits save on their own and come back after a restart, with no account, no network, and nothing to launch. It ships as a single exe and stays within a few MB of RAM.

<div align="center">
<table>
<tr>
<td align="center"><img src=".assets/screenshot.png" width="320" alt="X-TODO screenshot"></td>
<td align="center"><img src=".assets/demo.gif" width="320" alt="X-TODO demo"></td>
</tr>
</table>
</div>

## Features

- **One line per task.** Tick the box to check it off, press Enter to add the next one.
- **Drag to reorder.** Finished items fold into a collapsible "Completed" section, and every delete asks first.
- **Three layouts:** a normal window, sunk into the desktop, or folded into a side capsule that slides out on hover.
- **Saves itself.** Reopens exactly as you left it; the data is a single text file.
- **Stays in the tray.** Closing hides it, a double-click brings it back, startup launch is optional.
- **Pure Win32 + Direct2D.** No .NET, no browser engine, one portable exe.

## Download

Open [Releases](https://github.com/xwysyy/X-TODO/releases), download `x-todo.exe` from the latest version, and double-click it. Portable, no install. Development builds are still available from the [Actions](https://github.com/xwysyy/X-TODO/actions) artifacts.

> Tasks are stored in `%APPDATA%\x-todo\`. Uninstalling leaves it untouched; copy that folder to another machine and your list comes along.

## Usage

| Action | How |
| :-- | :-- |
| Add | Click "＋ New item" at the bottom, type, press Enter to keep going |
| Done / undo | Click the box in front of an item; click again to bring it back |
| Edit | Click the item text and type |
| Delete | Hover an item, click ×, confirm |
| Reorder | Drag the handle on the right of an item |
| Move / resize | Drag the title bar to move, drag the edges to resize |
| Switch layout | Right-click the tray icon: normal window / desktop / side capsule |

## Build

On push, GitHub Actions builds it with MSVC and uploads the exe to that run's Artifacts. Version tags such as `v0.1.0` also publish a GitHub Release with the exe attached.

To build it yourself on Windows with Visual Studio (Desktop development with C++):

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

Stack: C++17, Win32, Direct2D / DirectWrite, CMake, statically linked CRT, single exe.

## License

[MIT](LICENSE)
