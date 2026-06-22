# Calendar System

Long-lived rules for X-TODO's calendar. This file records current runtime
contracts, not the plan that produced them.

## Module Boundary

Calendar data is independent of `TodoModel`. The calendar is a top-level app view
(`MainView::Calendar`), not a property of any todo list, and calendar blocks are
never converted into todo items.

Geometry, hit-testing, date math, and persistence parsing live in Win32-free
modules so contract tests can cover them without a real window:

- `CalendarModel` owns block data (add, remove, retitle, set range, query by day,
  import from storage).
- `CalendarDate` owns date arithmetic.
- `CalendarLayout` (`GuiCalendar`) owns frames, block rects, lane packing,
  hit-testing, edit geometry, and time parsing and formatting.

`MainWindow` and `MainWindowView` only call these modules. Do not hardcode
calendar geometry or hit-testing in the view layer; extend the policy modules
instead.

## Data And Storage

`CalendarBlock` is `{ int id; std::string day; int startMinute; int endMinute;
std::wstring title; }`. Week and month views derive all grouping from `day`, so
no storage migration is needed to display them.

Persistence stays on `XTODO v4`. The store reads and writes:

- `ui active_view` = `list | calendar`
- `ui calendar_day` = the selected anchor, a real `YYYY-MM-DD`
- `ui calendar_view` = `day | week | month`
- `calendar <id> <day> <start> <end> <title>` block rows

Parsing uses exact keys. Invalid block rows are ignored, duplicate block ids are
re-assigned, and an unknown or missing `calendar_view` loads as `day`. A stored
`calendar_day` that is not a real date falls back to today rather than entering
week or month math as an impossible date.

## CalendarDate

`CalendarDate` is Win32-free and deterministic (proleptic Gregorian day-number
math, no `mktime` or timezone use):

- `Parse` accepts only real Gregorian dates; it rejects impossible values such as
  `2026-02-31` and `2025-02-29`, not only malformed strings.
- `AddDays` and `AddMonths` offset by days or months. `AddMonths` clamps the day
  to the target month length, so `2026-01-31` plus one month is `2026-02-28`.
- Weeks start on Monday: `WeekdayMondayBased`, `StartOfWeek`, and `MonthGridStart`
  (the Monday on or before the first of the month).

Today and the current minute come from a small platform wrapper over
`GetLocalTime`; the date math itself stays in `CalendarDate`.

## View Modes And Navigation

Runtime state uses `enum class CalendarViewMode { Day, Week, Month }`; only the
store boundary uses strings, so there is no invalid intermediate value.

`calendarDay_` is the selected anchor and is always a real local date. Each mode
derives its visible period from the anchor:

- Day: `[anchor, anchor + 1 day)`
- Week: `[StartOfWeek(anchor), StartOfWeek(anchor) + 7 days)`
- Month: a fixed 42-cell grid for the month containing the anchor

Prev and next move by one day, one week, or one month. Month navigation keeps the
day-of-month when possible and clamps to the last day otherwise. Today sets the
anchor to the current local day in every mode. Drilling in from a week day
header, a week block, or a month cell selects that day and switches to day mode.

## Shared Header

All three modes share one `HeaderLayout` (`ComputeHeader`): a grouped prev/next
nav pair on the left, a flexible period title, a connected Day/Week/Month
segmented control (selected segment filled), and a back-to-today icon shown only
when the anchor is not today.

The header degrades by priority on narrow widths: nav, then segmented, then today,
then title. The title shortens and then hides, the today icon hides, and the
segmented control shrinks, but the segmented control never overlaps the nav group
and no rect goes negative-width.

Header hit-testing is a single `HitTestHeader` call against the active frame's
header, run before the body. The per-mode hit-tests (`HitTest`, `HitTestWeek`,
`HitTestMonth`) return body targets only.

## Day View Editing

Day view is the editing surface; week and month are read-only in the current
version.

- A new block is created only after the drag passes the movement threshold; a
  bare click does not create one.
- Block move and resize snap to 15 minutes; manual time entry keeps minute
  precision.
- Editing is note-style: the title writes back as typed, time fields write back
  once they parse as a complete valid time, and there are no save or cancel
  buttons.
- An empty new block is removed only on outside click, Escape, starting another
  action, collapsing, or exit, not when the mouse merely leaves it.
- Blocks render flat: no side bar, drop shadow, or inner highlight.

## Week View

Seven day columns share one vertical time axis at the same scale as day view.
Per-day blocks are packed into deterministic lanes:

- Sort a day's blocks by `startMinute`, then `endMinute`, then `id`.
- Assign each block in an overlapping cluster to the first free lane.
- Block width is the column width divided by the cluster's lane count.
- Hit-testing walks rendered rects in reverse paint order so the top block wins.

## Month View

A fixed 42-cell grid (six weeks of seven days). Leading and trailing out-of-month
days render muted. Each cell shows a deterministic summary: a count when the cell
is short, otherwise as many compact titles as fit plus a `+N` overflow marker.
Summary rendering never resizes a cell.

## Rendering And Verification

Reuse the theme system and the fixed calendar block palette; scale through
`dpiScale()`. Week and month summaries use no child HWND controls.

`CalendarDate`, `CalendarLayout`, store parsing, and i18n labels are covered by
Win32-free unit tests (run via the `g++` fallback in `docs/testing.md` when CMake
is absent). Native visual and interaction behavior, including real rendering,
mouse, IME, DPI, and capsule mode, must be checked on Windows or a CI artifact.
