# Surface Rendering

Framed UI surfaces must use one consistent drawing contract. The rule applies to tabs, buttons, inputs, dialog fields, popup rows, and any future component with a visible fill plus border.

## Rounded Direct2D Surfaces

Use one rounded-rectangle primitive for the border. Do not compose a normal rounded surface from partial `DrawLine` calls.

Required pattern:

1. Compute one stable outer rect.
2. Fill the rounded rect.
3. Stroke the same rounded rect.
4. Draw text and marks inside that rect.

If a component intentionally omits one side of a border, the call site must state that intent. Otherwise a missing side is a bug.

Settings-like popup windows must draw with Direct2D and DirectWrite. Do not use
GDI double-buffering, `RoundRect`, GDI brush/pen drawing, or GDI text measurement
for these surfaces. `ThemedWindowControls` owns the shared Direct2D helpers for
rounded fills, strokes, text, divider lines, and middle elision.

## Child HWND Frames

When a parent window paints a frame around a native child control, the child HWND must not occupy the same rect as the frame.

Required pattern:

1. Compute a frame rect for the parent-painted fill and border.
2. Compute a separate child rect inset from the frame rect.
3. Use the child rect for `CreateWindowExW` and `MoveWindow`.
4. Use the frame rect for Direct2D or GDI frame painting.

A native `EDIT` or other child HWND that shares the frame rect will cover the parent-painted top and left border on Windows.

## Review Checks

- Search for framed components built from several border `DrawLine` calls.
- Search for child controls created with the same rect later used by parent frame painting.
- Run `xtodo_rendering_policy_tests` when a settings-like popup or
  `ThemedWindowControls` changes; it blocks GDI drawing tokens in those surfaces.
- Check Windows screenshots at normal DPI and scaled DPI when a new framed component is added.
