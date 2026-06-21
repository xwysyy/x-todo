# Theme System

Long-lived rules for X-TODO's theme pipeline. This file records current
runtime contracts, not the implementation plan that created them.

## Runtime Boundary

`Theme::ThemeVisual` is the rendering color contract. Main window paint,
capsule paint, dialogs, prompts, popup menus, the theme manager, and native
child-control coloring must read the active `ThemeVisual` or an explicit
snapshot captured when the popup opens.

Direct2D code uses `Theme::D2DColor`. GDI code uses `Theme::GdiColor`.
Do not add new render-facing global color constants such as `Theme::kPaper`,
`Theme::kText`, or `Theme::kDanger`.

Theme files do not own layout. Window geometry, hit-test widths, row heights,
font family, font sizes, capsule dimensions, animation timing, keyboard
behavior, and item structure stay in program constants and code.

## Built-In Themes

Built-in themes live in `ThemeCatalog` and must define every field in
`ColorSet`, `CapsuleSet`, and `TraySet`. A built-in theme must not inherit
missing colors from another theme at runtime.

The expected built-in ids are:

- `paper`
- `mint`
- `sky`
- `rose`
- `sand`
- `graphite`
- `ink`
- `contrast`

Hover, pressed, danger, row, menu, and capsule colors are final consumer
colors. Rendering code should not reintroduce ad hoc alpha blends for theme
roles that already exist in `ThemeVisual`.

## Resolution

Theme selection is stored as four exact UI fields:

- `theme_mode`, one of `builtin`, `custom`, or `follow_system`
- `theme_id`, used by `builtin` and `custom`
- `light_theme_id`, used by `follow_system`
- `dark_theme_id`, used by `follow_system`

`StoreFormat` must parse these as exact keys. Do not parse theme fields with
substring searches, because `theme_id` is contained inside `light_theme_id`
and `dark_theme_id`.

`follow_system` reads Windows app light or dark mode from
`AppsUseLightTheme`. When Windows high contrast is active, `follow_system`
resolves to the built-in `contrast` theme. In `builtin` and `custom` modes,
the selected theme is preserved and the app records a visible notice if high
contrast may make that theme inaccessible.

Failed theme resolution falls back to built-in `paper` and records a notice.
Invalid theme fields must not trigger the corrupt-data backup path and must
not clear todo items.

## Custom Themes

Custom themes are `.xtheme` files under `%APPDATA%\x-todo\themes\`. They are
strict JSON data. They cannot execute code, load images, fetch URLs, read
paths declared in JSON, import CSS, set fonts, change layout, change
animation, add hotkeys, or alter window shape.

Custom theme ids must start with `custom.` and cannot shadow built-in ids.
Issue UI must show only the file name, not the full local path.

The loader rejects malformed JSON, oversized files, excessive nesting, invalid
ids, missing fields, unknown inner fields, invalid colors, invalid alpha
values, reserved fields, duplicate ids, and contrast failures. Reparse-point
directory entries are skipped. Directory and export operations must stay bound
to verified handles instead of trusting path strings after enumeration.

Exporting a built-in theme writes a reusable custom id such as
`custom.exported-paper`. Exporting must not overwrite an existing theme file
without an explicit user decision.

## Rendering Surfaces

The active main window reads `MainWindow::theme_`. Confirm dialogs, text
prompts, and popup menus keep the theme snapshot passed at open time. They do
not have to live-refresh while they are already open; a later open should use
the current resolved theme.

The native `EDIT` child control can reliably set background and text colors
through `WM_CTLCOLOREDIT`. Caret color, selection color, and IME candidate UI
are controlled by Windows. Dark-theme work is incomplete until those states
are checked on Windows with real input.

Side-capsule rendering consumes `theme_.capsule`. Slim folded opacity comes
from `theme_.capsule.slimAlpha`; Dot folded state uses Dot capsule colors and
the existing per-pixel alpha path.

## Tray Boundary

The shell tray icon is app identity. `CreateTrayIconHandle()` loads the fixed
`resources/app.ico`; theme switching must not recolor or redraw the shell tray
icon unless the product decision changes and the README is updated.

`RefreshTrayIcon()` owns replacement semantics: create a new owned `HICON`,
call `Shell_NotifyIconW(NIM_MODIFY, ...)`, then destroy the previous owned
icon only after the replacement succeeds. Explorer restart handling must
re-add the fixed app icon.

`ThemeVisual::tray` currently remains part of the theme data shape and export
format. Do not claim that shell tray rendering consumes it unless a current
code path actually does.

## Review Checks

- Check `src/Theme.h` before adding a new color role. Add roles only when a
  real surface needs them.
- Check `src/StoreFormat.cpp` when adding persisted UI fields. Exact key
  parsing is required.
- Check `src/ThemeLoader.cpp` for file-size, nesting, reparse, reserved-field,
  contrast, and handle-bound export behavior before loosening validation.
- Check `src/MainWindow.cpp` popup and prompt structs when changing theme
  refresh behavior. Popup snapshots are intentional.
- Check Windows behavior for dark native edit state, IME, high contrast,
  Explorer restart tray recovery, DPI scaling, and multi-monitor capsule
  placement before calling a visual theme change complete.

Useful static probes:

```sh
rg -n "Theme::k(Paper|PaperEdge|Text|TextWeak|TextDone|CheckBorder|CheckFill|CheckFillHover|CheckMark|Danger|Divider|Hover|Handle|HandleHover|DragGhost)\b" src
rg -n "line\.find\(L\".*theme_id" src
rg -n "WM_SETTINGCHANGE|AppsUseLightTheme|WM_SYSCOLORCHANGE|SPI_GETHIGHCONTRAST" src
rg -n "FILE_FLAG_OPEN_REPARSE_POINT|GetFinalPathNameByHandleW|custom\.exported" src/ThemeLoader.cpp
```
