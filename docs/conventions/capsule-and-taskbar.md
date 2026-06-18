# Capsule & Taskbar Gotchas

Hard constraints in X-TODO's side-capsule and taskbar mount modes,
discovered through specific bugs. Each rule below regressed at least once.

The note has four mount modes (`MountMode` in `src/MainWindow.h`): Normal,
Desktop, Capsule, and Taskbar. Capsule docks a collapsed pill to a screen
edge and slides the full note out on click. Taskbar hides the main window
and shows a compact band embedded in the Explorer taskbar. The two modes
share almost no code, so a fix in one rarely covers the other, and both
break in ways the default Win32 behavior would not warn about.

## Side Capsule

### The collapsed and expanded capsule are two different windows in one HWND

Symptom: resizing the collapsed pill (the slim bar or the dot) was either
impossible or produced a deformed shape, and clicks on the pill behaved
inconsistently with clicks on the expanded note.

Root cause: the same `hwnd_` serves both states, distinguished only by the
`capsuleShrunk()` predicate (`src/MainWindow.h`), which is
`mountMode_ == MountMode::Capsule && !capsuleExpanded_ && !animActive_`.
The two states need opposite hit behavior. The collapsed pill is a fixed
shape that must not be resized, and during the slide animation the in-flight
window must also keep its shape. `OnNcHitTest` in `src/MainWindowView.cpp`
returns `HTCLIENT` for `capsuleShrunk() || animActive_`, short-circuiting
before any resize-edge math, so the system never enters a sizing loop on the
pill. The expanded note falls through to the normal resize-edge and
`HTCAPTION` logic and is fully resizable. The collapsed Dot style is given an
elliptical window region by `UpdateCapsuleRegion` (`src/MainWindow.cpp`,
`CreateEllipticRgn` when `dotShrunk`); the Slim style keeps a rectangular
window and relies on DWM rounded corners instead of a region. Both collapsed
styles suppress the DWM border line, and `UpdateCapsuleRegion` clears the
region back to rectangular for every other state.

Rule: collapsed or animating means fixed shape and `HTCLIENT`; expanded
means resizable. Anything that reads or writes capsule shape, hit-testing,
or window region must branch on `capsuleShrunk()` (and treat `animActive_`
like collapsed), not on `mountMode_ == Capsule` alone. `35cf976`

### The expanded capsule must not collapse the instant the mouse leaves

Symptom: the expanded note snapped back to a pill the moment the cursor
approached its border, so the thin resize edges were nearly ungrabbable and
the note felt over-eager. Reaching for an edge to resize triggered a
collapse instead.

Root cause: `WM_MOUSELEAVE` in `MainWindow::WndProc` (`src/MainWindow.cpp`)
originally called `StartCapsuleAnim(false)` immediately. Because the resize
hit edges sit at the very border of the window, moving the cursor toward an
edge crosses out of the client area and fires `WM_MOUSELEAVE` before the
user can grab the edge.

Fix (`2aafdbd`): `WM_MOUSELEAVE` now arms a grace timer instead of
collapsing, `SetTimer(hwnd_, kCollapseTimerId, kCollapseDelayMs, nullptr)`
where `kCollapseTimerId` and `kCollapseDelayMs` (500) are defined in
`src/MainWindow.h`. The `WM_TIMER` handler for `kCollapseTimerId` re-checks
state at fire time: it reads the live cursor position with `GetCursorPos`
and the window rect with `GetWindowRect`, reschedules itself while the left
button is held (an active drag or resize), and only calls
`StartCapsuleAnim(false)` if the cursor is genuinely outside the window
rect. The same widening of the resize hit edge from 6 to 8
(`Theme::kResizeEdge` in `src/Theme.h`) and giving the title-bar buttons hit
priority in `OnNcHitTest` shipped in `2aafdbd` so the expanded note can
actually be resized.

This rule has two sub-gotchas that must hold together.

Sub-gotcha (a): the `WM_MOUSEMOVE` cancel of the grace timer must be guarded
by a client-rect `PtInRect`. A captured drag still delivers `WM_MOUSEMOVE`
with coordinates outside the window, so an unguarded `KillTimer` on every
move would cancel the collapse even while the cursor has actually left. The
`WM_MOUSEMOVE` case calls `GetClientRect` and only kills `kCollapseTimerId`
when the point is inside it (`src/MainWindow.cpp`).

Sub-gotcha (b): the collapse decision is made at timer fire, not at
mouse-leave. The handler must re-read cursor position and button state when
the timer fires rather than trusting the state captured when the timer was
armed, because the cursor can move back in, or a drag can start, during the
500ms window.

Rule: collapse on a grace timer, never inline on `WM_MOUSELEAVE`. The timer
handler re-checks live cursor position and button state, defers while the
left button is down, and the `WM_MOUSEMOVE` cancel is gated on the point
being inside the client rect. Do not collapse based on state sampled at
arm time. `2aafdbd`

Verification: dock the note as a side capsule, expand it, and slowly move
the cursor to a resize edge; the note must stay expanded and the edge must
be grabbable. Move the cursor fully off the window and leave it there; the
note collapses after about half a second. Start a resize that drags briefly
outside the window; the note must not collapse mid-resize.

### Pressing the body of the expanded note is not a capsule drag

Symptom: a regression here would either treat clicks inside the expanded
note as attempts to drag-dock the pill, or fail to dock the pill when it was
actually dragged.

Root cause: the press-versus-drag machinery
(`BeginCapsulePress` / `UpdateCapsulePress` / `FinishCapsulePress` /
`SnapCapsuleToNearestEdge` in `src/MainWindow.cpp`) is for the collapsed pill
only. It distinguishes a click (expand) from a drag (re-dock to the nearest
edge of any monitor). `OnLButtonDown` in `src/MainWindowView.cpp` enters
`BeginCapsulePress` only when `capsuleShrunk()`; otherwise it runs the normal
note hit-testing path. So the expanded note's body behaves like an ordinary
note (row edit, checkbox, drag-to-reorder), and the drag-to-move plus
`SnapCapsuleToNearestEdge` logic never runs on it.

Resizing the expanded note persists its size through the normal geometry
path, not a capsule-specific one. `WM_EXITSIZEMOVE` calls
`CaptureVisibleGeometry`, which for an expanded capsule records width and
height into `geom_` (and re-derives the dock edge and position via
`CaptureCapsuleDockFromRect`). `ExpandedTargetRect` then reuses `geom_.w` /
`geom_.h` and re-anchors the note to the current dock edge, so the next
expand keeps the user's chosen size. `WM_EXITSIZEMOVE` re-applies
`ExpandedTargetRect` immediately so the resized note snaps back flush
against the dock edge.

Rule: capsule press, drag, and edge-snap are collapsed-pill behavior, gated
on `capsuleShrunk()` in `OnLButtonDown`. The expanded note uses the normal
input path and persists its size through `CaptureVisibleGeometry` into
`geom_`, consumed by `ExpandedTargetRect`. Do not route expanded-note clicks
through `BeginCapsulePress`. `63e1937`

## Taskbar Band

### The taskbar band is a separate overlay, not a child of Explorer

Symptom: a cross-process `WS_CHILD` under `Shell_TrayWnd` can be created
successfully but still not appear on modern Windows taskbars. If the main
window is then hidden, taskbar mode looks like it lost every visible entry.

Root cause: the band is its own window and should not depend on Explorer's
child z-order. `EnsureTaskbarBand` (`src/MainWindowTaskbar.cpp`) creates
`taskbarHwnd_` as a no-activate topmost tool `WS_POPUP`; `taskbarParent_`
keeps tracking `Shell_TrayWnd` or `Shell_SecondaryTrayWnd` only for monitor
selection, blocker discovery, and coordinate conversion. `LayoutTaskbarBand`
still stores `taskbarBandRect_` in taskbar client coordinates, then converts
the origin with `ClientToScreen` before `SetWindowPos`. The main window
`hwnd_` remains an independent top-level `WS_POPUP` and is hidden only after
the band layout result is `TaskbarLayoutResult::Ok`.

On an Explorer restart the band is recreated as a fresh overlay. The host
window is recomputed each time `EnsureTaskbarBand` runs, and if
`taskbarHwnd_` already exists for a different host
(`taskbarParent_ != host`), `DestroyTaskbarBand` runs before a fresh
`CreateWindowExW`. The same destroy-and-recreate path is driven by the
`TaskbarCreated` message and by `WM_DISPLAYCHANGE` in `MainWindow::WndProc`.

Rule: the band is a standalone no-activate topmost tool popup with its own
class, proc, and `IDC_ARROW` cursor. It is positioned from Explorer taskbar
geometry but is not parented to Explorer. Do not give the band `OnNcHitTest`,
resize edges, or a resize cursor. Do not hide the main window unless
`EnsureTaskbarBand` returns `TaskbarLayoutResult::Ok`.

### `PaintTaskbarBand` must call the global `::FillRect`

Symptom: the band's background fill in `PaintTaskbarBand` did not compile, or
resolved to the wrong function, because the intended Win32 background fill
was shadowed by a same-named member.

Root cause: `MainWindow` declares its own member `void FillRect(const
D2D1_RECT_F&, uint32_t, float)` (a Direct2D helper, defined in
`src/MainWindowView.cpp`). `PaintTaskbarBand` is a `MainWindow` member, so
inside it the unqualified name `FillRect` resolves to that member, not to the
Win32 `FillRect(HDC, const RECT*, HBRUSH)`. The member has a completely
different signature (Direct2D rect and color, no HDC), so the GDI call
`FillRect(mem, &rc, bg)` did not match the intended global function.

Fix (`16436d5`): qualify the call as `::FillRect(mem, &rc, bg)` in
`PaintTaskbarBand` (`src/MainWindowTaskbar.cpp`) so it binds to the global
Win32 GDI function. The one-line change is exactly `FillRect` to
`::FillRect`.

Rule: inside `MainWindow` member functions, any Win32 API that collides with
a member name (`FillRect` is the known case) must be called with the `::`
global qualifier. The band paints with GDI on an HDC, so it needs the global
`::FillRect`, not the Direct2D member. `16436d5`
