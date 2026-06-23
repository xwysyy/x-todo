# Capsule Gotchas

Hard constraints in X-TODO's side-capsule mount mode, discovered through
specific bugs. Each rule below regressed at least once.

The note has three mount modes (`MountMode` in `src/MainWindow.h`): Normal,
Desktop, and Capsule. Capsule docks a collapsed pill to a screen edge and
slides the full note out on click. It has separate shape, hit-test, animation,
and persistence behavior from the normal window.

## Side Capsule

### The collapsed and expanded capsule are two different windows in one HWND

Symptom: resizing the collapsed entry (the sleeping cube or the puzzle orb) was either
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
`HTCAPTION` logic and is fully resizable. The collapsed entry is drawn with
per-pixel alpha and fixed product colors, so `UpdateCapsuleRegion`
(`src/MainWindow.cpp`) suppresses DWM border and shadow while folded and clears
back to a rectangular ordinary window for every other state.

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
