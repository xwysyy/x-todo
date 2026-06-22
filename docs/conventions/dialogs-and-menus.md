# Dialog & Menu Gotchas

Hard constraints in X-TODO's custom-drawn popups, discovered through
specific bugs. Each rule below regressed at least once.

The two popups are both owner-drawn Win32 `WS_POPUP` windows with their
own window class, message loop, and Direct2D/DirectWrite paint code, living in the
anonymous namespace at the top of `src/MainWindow.cpp`. There is no
common dialog or `TrackPopupMenu`; everything (sizing, hit-testing,
painting, cursor) is hand-rolled, so the usual framework guarantees do
not apply.

## Themed Confirm Dialog

### Size the dialog to its measured content, never to a hardcoded height

Symptom: the confirm popup showed a large band of dead whitespace below
the message, and the longer "clear all completed" message risked being
clipped with an ellipsis while the short "delete this item?" message
floated in an oversized box.

Root cause: the original `ShowThemedConfirm` set `state.h =
DpiPx(owner, 168)` as a fixed height, painted a "X-TODO" title plus the
icon at a fixed `DpiPx(owner, 38)` top, and drew the message into a
fixed `msgRect` ending around `DpiPx(owner, 112)` with `DT_END_ELLIPSIS`.
The buttons were bottom-anchored at a fixed `pad`. A fixed message
rectangle cannot fit both `Str::DeleteItemMsg` and the much longer
`Str::ClearAllMsg` ("Clear all completed items? This cannot be undone.",
and its Chinese form), so the long one truncated while the short one
left a tall gap above the bottom-anchored buttons.

Fix: `MeasureConfirm` (in `src/MainWindow.cpp`) measures the message
with `DrawTextW(..., DT_CALCRECT | DT_WORDBREAK)` against the available
inner width, storing `msgW` and `msgH` on `ConfirmState`. `ConfirmHeight`
then derives the window height from `padTop + rowH + msgBtnGap + btnH +
padBottom` where `rowH = max(msgH, icon)`. The title was dropped and the
icon plus message are centered as one horizontal block in `ConfirmProc`'s
`WM_PAINT`. The message is drawn with `DT_CENTER | DT_TOP | DT_WORDBREAK`
and no ellipsis flag, so wrapped text is shown in full. Introduced in
`72d5773`, then scaled up (paddings, icon, fonts) in `2aafdbd`.

Rule: a content-driven popup measures its variable content first
(`MeasureConfirm`) and computes its own height (`ConfirmHeight`) before
`CreateWindowExW`. Do not reintroduce a fixed `state.h`, a fixed message
rectangle, or `DT_END_ELLIPSIS` on the message. The same dialog must
fit both the short delete message and the long clear-all message in both
languages.

Verification: open both confirm paths (delete a single item, and Clear
on the completed section) in English and in Chinese; the dialog should
hug its text with even top and bottom padding and no truncated message
in any of the four combinations.

### Measure button width per label so it fits both languages

Symptom: with a fixed button width, the localized button labels did not
fit equally. The Chinese labels (`确认` / `取消`) and the English labels
(`OK` / `Cancel`) have different pixel widths, so one language clipped or
looked cramped.

Root cause: the original buttons used a fixed `DpiPx(owner, 82)` width
regardless of label text.

Fix: `MeasureConfirm` selects the button font, calls
`GetTextExtentPoint32W` on both `Str::ConfirmOk` and `Str::ConfirmCancel`,
and takes the maximum text width plus horizontal padding, floored at a
minimum (`DpiPx(s.owner, 54)` in the current code). The result is stored
in `ConfirmState::btnW` and used by both `ConfirmOkRect` and
`ConfirmCancelRect`, so both buttons share one width that fits the wider
of the two labels. The two-button group is then centered as a unit.
Introduced in `72d5773`, padding and floor enlarged in `2aafdbd`.

Rule: button width is `max(width(OK), width(Cancel)) + padding`, floored
to a minimum, measured at the active font and DPI. Both buttons use the
same measured `btnW`. Do not hardcode a button width that only happens
to fit one locale.

## Popup Menu

### The two menus share one code path

The title-bar hamburger and the tray icon open the same menu through one
function. `ShowTitleMenu` (reached from
`HitKind::Menu` in `src/MainWindowView.cpp`) opens the title-bar menu and
calls `ShowPopupMenu(hwnd_, pt, items, true)` with `alignRight = true`,
so the menu's right edge sits against the window's right side (`pt.x -=
state.w` inside `ShowPopupMenu`). `ShowTrayMenu` calls
`ShowPopupMenu(hwnd_, pt, items, false)` at the cursor for the tray icon
(`WM_TRAY` with `WM_RBUTTONUP` in `MainWindow::WndProc`).

Rule: any change to sizing, hit-testing, painting, capture, or cursor in
`ShowPopupMenu` / `PopupMenuProc` affects both entry points. When changing
menu behavior, check it from the hamburger and the tray icon, not just one.
Per-menu differences (alignment, item list) belong in `ShowTitleMenu` /
`ShowTrayMenu`, not in the shared path.

The title-bar menu and tray menu do not carry list or tab management.
Switching lists and creating a list belong to the main window's tab strip;
the menu surfaces are for immediate window mode, capsule style, settings,
theme, and exit commands only. Long-lived settings such as language,
autostart, and automatic backup belong in the settings window, not as direct
menu rows.

List-tab management stays on the tab strip itself. Double-clicking a list tab
renames it; right-clicking a list tab opens that tab's context menu for rename
and delete. This tab context menu does not add list commands back to the
title-bar or tray menus.

### Keep menu hit-testing in lockstep with painting

Symptom: clicking a menu row could select a neighboring row or no row at
all, because the clickable bands did not line up with the painted rows.

Root cause: row geometry is computed in two places that must agree.
`MenuItemAt` walks items from a starting `top` offset, adding
`s.rowH` or `s.sepH` per item to hit-test a `y` coordinate.
`PopupMenuProc`'s `WM_PAINT` loop walks the same items from a starting
`y` offset, adding `s->rowH` or `s->sepH` per item to paint them. If the
two starting offsets differ, or if the per-row and per-separator heights
differ between the two loops, the painted rows and the clickable bands
drift apart and clicks land on the wrong item.

Three values must stay equal:

- the starting offset: `MenuItemAt`'s initial `top` and the paint loop's
  initial `y` (both `DpiPx(s->owner, 5)` in the current code);
- the row height: `state.rowH` set in `ShowPopupMenu`, the `rowH` summed
  in `MeasurePopupMenuHeight`, and the `s->rowH` advanced in the paint
  loop;
- the separator height: `state.sepH`, the `sepH` in
  `MeasurePopupMenuHeight`, and `s->sepH` in the paint loop.

These shifted together when the menu was compacted (start offset `6`
to `4`, rows `26` to `20`) in `72d5773`, and again when it was enlarged
(start offset `4` to `5`, rows `20` to `22`, separators `5` to `6`) in
`2aafdbd`. The window height in `MeasurePopupMenuHeight` uses the same
`rowH` / `sepH`, so a mismatch there also clips or pads the window on
top of misaligning clicks. Every shipped commit moved the paint start
and `MenuItemAt` start together; the real hazard is editing one site
and forgetting the others, so this is recorded as a standing constraint
rather than tied to one regressing commit.

Rule: the menu's starting offset, `rowH`, and `sepH` are one set of
numbers consumed by `MeasurePopupMenuHeight`, `ShowPopupMenu`
(`state.rowH` / `state.sepH`), `MenuItemAt`, and the `WM_PAINT` loop.
Change them in all four places together, or the hit-test and the paint
disagree.

### A window holding mouse capture must set its own cursor

Symptom: when the menu was opened from a window edge (where the cursor
is a resize arrow), the resize cursor stuck on the open menu instead of
reverting to a normal arrow.

Root cause: `ShowPopupMenu` calls `SetCapture(hwnd)` so the menu can
detect clicks outside itself. While a window holds the mouse capture,
Windows does not send `WM_SETCURSOR`, so the class cursor is never
applied and the cursor keeps whatever shape it had at the moment capture
was taken. Setting `wc.hCursor = LoadCursorW(nullptr, IDC_ARROW)` in
`RegisterPopupClass` is not enough under capture, because the class
cursor is only consulted through `WM_SETCURSOR`, which is suppressed.

Fix: call `SetCursor(LoadCursorW(nullptr, IDC_ARROW))` explicitly twice
in `src/MainWindow.cpp`: once in `ShowPopupMenu` right after
`SetCapture`, and once in `PopupMenuProc` on every `WM_MOUSEMOVE` (so a
cursor changed by another window while the menu is open is corrected on
the next move). Fixed in `8b63ae2`.

Rule: any window that holds mouse capture and wants a specific cursor
must call `SetCursor` itself, on show and on `WM_MOUSEMOVE`. Do not rely
on the window class cursor under capture. This applies to the confirm
dialog too: `ConfirmProc` takes capture in `WM_LBUTTONDOWN`, but only
briefly during a button press, so a stale cursor has no window to persist
in; the menu holds capture for its entire lifetime, which is why it needs
the explicit cursor calls.

Verification: dock the note as a side capsule, expand it, move the cursor
to a resize edge so it becomes a resize arrow, then open the menu; the
cursor over the menu must be a normal arrow, including after moving the
mouse within the menu.
