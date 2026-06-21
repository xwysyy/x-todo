# Window Hit-Testing & Geometry Gotchas

Hard constraints in X-TODO's window hit-testing and geometry,
discovered through specific bugs. Each rule below regressed at least
once.

## Non-Client Hit-Testing

The main window is a borderless `WS_POPUP` (created in
`src/MainWindow.cpp` `MainWindow::Create`). It has no system frame, so
every edge grab, caption drag, and corner resize is synthesized through
`src/MainWindowView.cpp` `OnNcHitTest`, wired through the `WM_NCHITTEST`
case in `src/MainWindow.cpp` `MainWindow::WndProc`. The pure hit-test
policy lives in `src/WindowHitTest.cpp`, so the behavior is also covered
by `xtodo_gui_contract_tests`.

### Synthesize borderless resize from `kResizeEdge`; capsule stays fixed

Symptom: without explicit hit-testing a borderless window has no resize
affordance at all; conversely, the side capsule must not be resizable by
the system or its docked shape breaks.

Root cause: `WS_POPUP` exposes no frame, so Windows never returns
`HTLEFT`/`HTTOP`/etc. on its own. `OnNcHitTest` converts the screen point
to client coordinates and passes the window size, title controls,
`Theme::kResizeEdge`, and capsule flags into
`GuiHit::HitTestNonClient`. That policy function maps the edge bands to
`TopLeft`/`TopRight`/`BottomLeft`/`BottomRight`/`Left`/`Right`/`Top`/`Bottom`.
For the side capsule, `OnNcHitTest` sets `forceClient` when the capsule is
collapsed or animating, so the policy returns `Client` before any resize
band is considered.

Fix: keep all resize-edge synthesis inside `GuiHit::HitTestNonClient`,
with `OnNcHitTest` only adapting Win32 coordinates and converting the
policy result to `HT*` constants. Do not duplicate the edge math elsewhere.

Rule: borderless resize lives in exactly one hit-test policy; the
collapsed/animating capsule returns `HTCLIENT` to opt out of system
resize.

### Hit-test interactive title controls before the resize band

Symptom: clicking the very top of the menu, pin, or close button started
a window resize instead of activating the button. The top few pixels of
the buttons were dead to clicks.

Root cause: the title controls sit near the top of the window. In
`src/MainWindowView.cpp` `RebuildLayout`, `menuRect_`, `pinRect_`, and
`closeRect_` are laid out at `ty = (S(Theme::kTitleH) - btn) / 2`, which
is `(34 - 24) / 2 = 5` logical pixels from the top with a 24 px button
height. When `kResizeEdge` was widened from 6 to 8 (`src/Theme.h`), the
top resize band (`T = p.y < 8`) started overlapping the button band that
begins at y = 5, so `OnNcHitTest` returned `HTTOP` for a click on the
button's top pixels and the system began a resize.

Fix: `GuiHit::HitTestNonClient` tests `menu`, `theme`, `pin`, and `close`
rects before evaluating resize edges, then returns `Client` for those
points. `OnNcHitTest` must pass the title control rectangles that
`RebuildLayout` computed through `GuiLayout::ComputeTitleButtons`.

Rule: any interactive control that overlaps the resize band must be
hit-tested before the resize edges, not after. Widening `kResizeEdge` is
safe only while this ordering holds.

Verification: with `kResizeEdge = 8` and button top at y = 5, click the
top edge of the menu/pin/close buttons; the action must fire and the
cursor must not switch to a resize arrow.

### `kResizeEdge` is one global knob

Symptom: confusion over what "the resize edge" affects. Tuning it once
silently changes the grab feel of every full-window mode.

Root cause: `kResizeEdge` is consumed by the single `WindowHitTest`
policy used by `OnNcHitTest`, and `OnNcHitTest` is the `WM_NCHITTEST`
handler for the single main window across Normal, Desktop, and expanded
Capsule modes. Those modes share one HWND, so one `kResizeEdge` value
governs the grab feel everywhere at once.

Fix: treat `kResizeEdge` (`src/Theme.h`) as a single global resize-feel
knob for the main-window hit-test path. Do not add a separate per-mode
override.

Rule: `kResizeEdge` changes the grab feel of all main-window modes
simultaneously.

## Geometry Persistence

`WindowGeometry geom_` (`src/MainWindow.h`, struct in `src/Store.h`:
`x, y, w, h, valid`) is the single source of truth for the note's
position and size. It is loaded at startup (`src/MainWindow.cpp`
`MainWindow::Create` via `Store::Load`), validated against monitor
bounds, captured on user moves/resizes, reused when a mode computes its
target rectangle, and saved on a debounce (`src/MainWindow.cpp`
`SaveNow` calls `CaptureGeometry`).

### Preserve note width and height across mode switches via `geom_`

Symptom: switching window modes (Normal, Desktop, Capsule) reset the note
to a default size instead of keeping the size the user had dragged.

Root cause: the pre-fix `SetMountMode` only captured geometry when
leaving Normal mode (`if (mountMode_ == MountMode::Normal)
CaptureGeometry();`), and applying Capsule mode placed the window at the
collapsed `CapsuleTargetRect()`. The user's dragged width and height were
never read back when expanding, so the expanded note fell back to
defaults.

Fix: `src/MainWindow.cpp` `SetMountMode` asks
`GuiGeometry::ShouldCaptureBeforeModeSwitch(capsuleShrunk())` before
calling `CaptureVisibleGeometry`, writing the live window rectangle into
`geom_` when the current visible surface is capturable. Entering Capsule
mode stays collapsed:
`MainWindow::ApplyMountMode` sets `capsuleExpanded_ = false` and places
the window at `CapsuleTargetRect()`. The saved size is consumed later,
when the capsule expands: `StartCapsuleAnim(true)` targets
`src/MainWindow.cpp` `ExpandedTargetRect`, which calls
`GuiGeometry::ExpandedSize(geom_.valid, geom_.w, geom_.h, workW, workH)`.
The dragged size carries into the expanded note rather than at mode entry.
Normal and Desktop modes restore `geom_.x/y/w/h` directly in
`ApplyMountMode`. `WM_EXITSIZEMOVE` calls `CaptureVisibleGeometry`, so a
resize in any mode updates `geom_` before the next switch.

Rule: a mode switch must capture the current visible size into `geom_`
before changing mode, and each mode's target-rect computation must read
width/height back from `geom_`. Never hard-code a fresh default on
switch.

### Enforce minimum size in `WM_GETMINMAXINFO` and ignore sub-minimum captures

Symptom: aggressive resizing or a stale/degenerate window rect could
shrink the note below a usable size or poison the saved geometry with a
tiny size.

Root cause: a borderless `WS_POPUP` has no system-imposed minimum track
size, so without intervention the user can drag the frame arbitrarily
small, and a capture routine that trusted any rect would persist that
tiny size.

Fix: the `WM_GETMINMAXINFO` handler in `src/MainWindow.cpp`
`MainWindow::WndProc` clamps the drag floor with
`GuiGeometry::MinimumTrackSize(dpiScale())`. Both `CaptureVisibleGeometry`
and `CaptureGeometry` pass the live rect into `GuiGeometry::DecideCapture`,
which rejects sizes below `GuiGeometry::kMinWindowW` x
`GuiGeometry::kMinWindowH` after DPI scaling. `MainWindow::Create` applies
the same floor through `GuiGeometry::AcceptsLoadedGeometrySize`, then falls
back to `GuiGeometry::kDefaultWindowW` x `GuiGeometry::kDefaultWindowH` near
the work-area corner when the saved geometry is invalid.

Rule: the resize floor is enforced in `WM_GETMINMAXINFO`, and any
geometry capture must reject sizes below the same minimum so the
persisted `geom_` stays valid.

Verification: drag the window as small as possible; it must stop at
`GuiGeometry::kMinWindowW` x `GuiGeometry::kMinWindowH` after DPI scaling.
After a forced sub-minimum state, the saved geometry must not record a
size below that minimum.
