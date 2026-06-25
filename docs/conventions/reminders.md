# Calendar Reminders

This document is the implementation blueprint and maintenance contract for
calendar block reminders. The reminder feature is implemented as a complete
system, not as a staged MVP. Work follows TDD throughout: write one failing
behavior test, confirm the failure, implement the smallest production change,
then refactor only while the test suite is green.

## Goal

Calendar blocks support global reminders at two configurable points:

- five minutes before the block starts
- halfway through the block

Reminder settings are global. Individual calendar blocks do not store reminder
configuration. A reminder can be delivered through the app's own non-modal
popup, side capsule visual feedback, Windows system notification, and an
optional scheduler fallback for cases where the app process is not running.

## Architecture

Keep calendar data pure. `CalendarBlock` remains `{ id, day, startMinute,
endMinute, title }`. Reminder behavior is derived from those fields and the
global reminder settings.

Implement reminders through these boundaries:

- `ReminderTypes`: shared reminder settings, reminder kinds, candidates, log
  entries, and stable keys.
- `ReminderService`: Win32-free logic for candidate generation, due scanning,
  catch-up behavior, de-duplication, and log pruning.
- `StoreFormat`: persistence for `ui.reminders` and the global reminder log.
- `MainWindow`: timer ownership, calendar mutation hooks, sleep/time-change
  handling, dispatch orchestration, and target navigation.
- `ReminderPopup`: app-owned non-modal popup rendered in the existing window
  style.
- `ReminderTimerPolicy`: Win32-free timer delay policy for capped wakeups and
  immediate due scans.
- `WindowsNotifier`: Windows notification backends, starting with tray balloon
  notifications and supporting modern toast notifications when available.
- `ReminderDispatchPolicy`: Win32-free decision logic for marking reminders
  fired only after an attempted channel accepts delivery.
- `ReminderSchedulerPolicy`: Win32-free decision logic for deleting or
  registering the scheduler fallback task.
- `TaskSchedulerReminder`: one scheduled reminder-check task for the reliability
  path when the app process is not running.

`CalendarModel` does not know about reminders. The model owns block data only.

## Settings

Store reminder settings in `UiState`:

```cpp
struct ReminderSettings {
    bool enabled = true;
    bool beforeStart5 = true;
    bool halfway = true;
    bool inAppPopup = true;
    bool capsulePulse = true;
    bool systemNotification = false;
    bool taskSchedulerFallback = false;
    bool catchUpAfterResume = true;
    int catchUpGraceMinutes = 10;
};
```

Persist them under `ui.reminders`:

```json
{
  "ui": {
    "reminders": {
      "enabled": true,
      "beforeStart5": true,
      "halfway": true,
      "inAppPopup": true,
      "capsulePulse": true,
      "systemNotification": false,
      "taskSchedulerFallback": false,
      "catchUpAfterResume": true,
      "catchUpGraceMinutes": 10
    }
  }
}
```

The settings window owns these rows in a dedicated reminder section:

- enable calendar reminders
- five minutes before start
- halfway through
- app popup
- side capsule feedback
- Windows notification
- Task Scheduler fallback
- catch up still-valid missed reminders

Every setting change schedules a save and refreshes the reminder schedule.

## Reminder Log

Reminder firing state is global runtime state, not block configuration. Persist a
small `reminderLog` array beside the existing top-level data:

```json
{
  "reminderLog": [
    {
      "key": "block:42|before5|2026-06-25|840|900",
      "firedAt": 1782396900
    }
  ]
}
```

The key format is:

```text
block:{id}|{kind}|{day}|{startMinute}|{endMinute}
```

The title is not part of the key. Retitling a block changes what future popups
display but does not retrigger an already fired reminder. Moving or resizing a
block changes the key and lets the new schedule fire normally.

Prune fired entries older than 14 days. Drop malformed log entries during parse.

## Time Semantics

All reminder scheduling uses local wall-clock time. Calendar storage remains
date plus minute-of-day. Core reminder logic compares `ReminderMinute { day,
minute }` values so the behavior stays Win32-free and directly testable. Epoch
seconds are used only for fired-log retention timestamps and short-lived visual
expiry.

Candidate generation:

```text
beforeStart5 = start - 5 minutes
halfway = start + floor((end - start) / 2)
```

Rules:

- A block starting at `00:03` has a before-start reminder at the previous day
  `23:58`.
- A one-minute block does not produce a halfway reminder.
- A block with a day that is not a real Gregorian date is ignored by reminder
  candidate generation.
- A candidate with `end <= start` is ignored, although normalized calendar data
  should already prevent that state.
- Multiple due reminders in the same timer tick are dispatched as a group.
- Deleted blocks disappear from future candidate scans. Old fired entries are
  harmless and are removed by pruning.

## Due And Catch-Up Rules

The service distinguishes normal due reminders from catch-up reminders.

Normal due:

```text
now >= due && !fired
```

Validity windows:

- before-start reminders are valid while `due <= now < start`
- halfway reminders are valid while `due <= now < end`

Catch-up after sleep, process delay, or restart:

- disabled when `catchUpAfterResume` is false
- before-start reminders can be caught up only before the block starts
- halfway reminders can be caught up only before the block ends and within
  `catchUpGraceMinutes` after their due time

System time changes trigger a schedule rebuild. A time rollback must not clear
the fired log, because clearing it would duplicate reminders.

## Main Window Scheduling

`MainWindow` owns the reminder timer. The timer id only needs to be unique
within `MainWindow`; the behavioral contract is that the wake interval is capped
to one minute.

Each tick runs a due scan; it does not trust timer precision. `WM_TIMER` is low
priority, so all correctness comes from scanning the interval and applying
catch-up rules. When an unfired reminder is already due but still inside its
validity window, the next schedule is `now`, so the main window runs the scan
immediately instead of waiting for the one-minute cap.

Refresh the schedule after:

- app startup and data load
- calendar block creation
- calendar block deletion
- title edit
- start or end time edit
- drag-create, drag-move, and drag-resize completion
- reminder setting changes
- `WM_TIMECHANGE`
- resume from sleep
- Explorer restart when tray state may need repair
- reminder log pruning or scheduler fallback update

The dispatch path:

```text
OnReminderTimer
  -> ReminderService::DueNow
  -> DispatchReminders
  -> mark fired only after at least one selected channel accepts the reminder
  -> save reminder log
  -> refresh next schedule
```

If every selected channel fails, keep the reminder unfired while it remains
inside its validity window.

## App Popup

The app popup is non-modal. It must not disable the main window, enter a nested
modal loop, or steal focus from an active edit control.

Use a popup window with:

```cpp
WS_POPUP
WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE
```

Behavior:

- show one card for a single reminder
- aggregate multiple due reminders into one card
- auto-dismiss after a short timeout
- allow explicit dismiss
- clicking Open shows the main window, switches to calendar day view, navigates
  to the block day, and highlights the target block
- clicking Dismiss only closes the popup

Example single reminder text:

```text
Starts in 5 minutes
14:00-15:00 Meeting
```

Example grouped reminder text:

```text
3 calendar reminders
14:00 Meeting starts soon
13:30-14:30 Writing is halfway through
```

When all grouped reminders share one kind, use a specific grouped title such as
`2 个安排将在 5 分钟后开始` or `2 个安排已进行到一半`. Mixed groups use the
generic calendar-reminders title.

Use the same Direct2D, DirectWrite, rounded popup, theme, and DPI conventions as
the existing settings and menu surfaces.

Place the popup against the bottom-right of the owner window's monitor work
area, not the primary monitor work area. Clamp the placement when a small work
area cannot fit the preferred popup size.

## Capsule Feedback

Capsule feedback is part of the in-app reminder experience. Add a short-lived
visual state:

```cpp
struct ReminderVisualState {
    bool active = false;
    int blockId = -1;
    long long untilEpoch = 0;
    double pulseT = 0.0;
    int pendingCount = 0;
};
```

Expected behavior:

- collapsed side capsule shows a subtle pulse
- pending reminders can show a small badge count
- opening the reminder target clears the pending visual state
- the target calendar block flashes or highlights briefly in day view
- capsule feedback counts as a delivered reminder only when the main window is
  visible; a hidden-to-tray window cannot mark a reminder fired through an
  invisible capsule pulse

The effect must not move the docked capsule, alter stored geometry, or force the
window into a different mount mode.

## Windows Notifications

Use Windows notification delivery as a real system channel, not as the only
reminder mechanism.

System notification bodies use the same grouped reminder text as the in-app
popup, but they are joined into a bounded plain-text summary. Keep the body at
or below 200 characters and end truncated summaries with `...`; the in-app popup
can still render the full grouped line list.

### Tray Balloon

The first Windows backend uses the existing tray icon and `Shell_NotifyIconW`
with `NIF_INFO`. This keeps the default implementation compatible with the
current single-executable Win32 app shape.

On balloon click, open the latest reminder target when Windows sends the click
callback. If Windows drops the callback, the notification remains a valid
one-way alert.

### Modern Toast

Modern toast support is part of the complete implementation when the environment
supports it. It requires an AppUserModelID and a Start Menu shortcut pointing to
the executable. The app creates or repairs that shortcut when Windows
notifications are enabled.

Toast activation routes back to a calendar target:

```text
x-todo.exe --open-calendar --day 2026-06-25 --block 42
```

When a primary instance is already running, the new process sends the target to
the running instance and exits. If no instance is running, the app starts,
loads data, opens the target, and refreshes the reminder schedule.

## Scheduler Fallback

The reminder system includes a Task Scheduler reliability path for cases where
the user exits the app or Windows restarts before the next reminder.

Maintain one scheduled task:

```text
X-TODO Reminder Check
```

The task action:

```text
x-todo.exe --reminder-check
```

The trigger is the next due reminder time. Do not create one task per block.

The reminder-check command:

- loads `data.json`
- computes due reminders from global settings and the reminder log using the
  normal due scan, not the catch-up scan
- sends the reminders to a running primary instance when present
- otherwise sends a durable Toast notification; a tray balloon alone is not a
  successful background delivery because the scheduled helper process exits
- updates the fired log when a channel succeeds
- computes and registers the next scheduled check

The fallback requires Windows notification delivery when no primary process is
running. If `taskSchedulerFallback` is enabled while `systemNotification` is
disabled, delete the scheduled task and surface a settings status instead of
registering a task that cannot notify the user.

Task Scheduler failures are surfaced in settings as status text. They do not
silently disable in-app reminders.

`MainWindow` tracks whether the scheduler state is known and whether the app
believes a reminder-check task is registered. Startup begins in an unknown
state, so disabled fallback still deletes once to clear any stale task from an
older run. After a successful delete, later timer refreshes do nothing until a
setting or next-due state requires registration again. After a successful
registration, disabling fallback or losing the next due reminder deletes the
known task.

## Target Navigation

Opening a reminder target performs a view navigation, not an edit operation:

- show the main window
- expand capsule mode if needed
- switch to calendar view
- switch to day mode
- set `calendarDay` to the target block day
- scroll the day timeline to the block
- highlight the block briefly

The action does not change the block title, start, end, or calendar data. It
does not enter text edit mode by default.

## Implementation Files

Add:

```text
src/ReminderTypes.h
src/ReminderService.h
src/ReminderService.cpp
src/ReminderText.h
src/ReminderText.cpp
src/ReminderPopupPolicy.h
src/ReminderPopup.h
src/ReminderPopup.cpp
src/ReminderTimerPolicy.h
src/WindowsNotifier.h
src/WindowsNotifier.cpp
src/ReminderDispatchPolicy.h
src/ReminderSchedulerPolicy.h
src/ReminderSettingsPolicy.h
src/ReminderVisualPolicy.h
src/TaskSchedulerReminder.h
src/TaskSchedulerReminder.cpp
src/LaunchCommand.h
src/LaunchCommand.cpp
```

Modify:

```text
CMakeLists.txt
src/Store.h
src/StoreFormat.cpp
src/MainWindow.h
src/MainWindow.cpp
src/MainWindowView.cpp
src/SettingsWindow.h
src/SettingsWindow.cpp
src/I18n.h
src/I18n.cpp
docs/testing.md
tests/CMakeLists.txt
```

Add focused tests:

```text
tests/reminder_service_tests.cpp
tests/reminder_format_tests.cpp
tests/reminder_text_tests.cpp
tests/reminder_popup_policy_tests.cpp
tests/reminder_scheduler_tests.cpp
tests/reminder_scheduler_policy_tests.cpp
tests/reminder_timer_policy_tests.cpp
tests/reminder_settings_policy_tests.cpp
tests/reminder_visual_policy_tests.cpp
tests/reminder_dispatch_policy_tests.cpp
tests/windows_notifier_tests.cpp
tests/launch_command_tests.cpp
```

## TDD Execution Plan

Do not write all tests first. Use vertical RED-GREEN-REFACTOR slices.

1. Write a failing test that disabled reminders produce no candidates.
2. Implement the smallest `ReminderSettings`, `ReminderService`, and candidate
   call needed to pass.
3. Write a failing test that `09:00-10:00` produces a before-start candidate at
   `08:55`.
4. Implement before-start generation.
5. Write a failing test that `00:03-00:30` produces a before-start candidate on
   the previous day at `23:58`.
6. Implement local minute arithmetic across day boundaries.
7. Write a failing test that `14:00-15:00` produces a halfway candidate at
   `14:30`.
8. Implement halfway generation.
9. Write a failing test that a one-minute block produces no halfway candidate.
10. Implement the minimum-duration rule.
11. Write a failing test that a due reminder is returned only when `now >= due`.
12. Implement due scanning.
13. Write a failing test that a fired key is not returned again.
14. Implement fired log lookup and marking.
15. Write a failing test that changing block start or end creates a new key.
16. Implement the stable key format.
17. Write a failing test for before-start catch-up before block start.
18. Implement before-start validity.
19. Write a failing test that missed before-start reminders are dropped after
    block start.
20. Implement expired before-start filtering.
21. Write a failing test for halfway catch-up within the block and grace window.
22. Implement halfway catch-up.
23. Write a failing test that halfway catch-up is dropped after block end or
    outside grace.
24. Implement the expiry rules.
25. Write a failing persistence test for `ui.reminders` round-trip.
26. Implement store serialization and parsing.
27. Write a failing persistence test that missing reminders use defaults.
28. Implement default parsing.
29. Write a failing persistence test that malformed reminder fields are ignored.
30. Implement safe parse validation.
31. Write a failing test for reminder log round-trip and pruning.
32. Implement log serialization, parsing, and pruning.
33. Write a failing text-format test for one reminder and grouped reminders.
34. Implement reminder text formatting.
35. Write a failing settings contract test for toggling reminder settings.
36. Implement settings rows, host callbacks, and save/schedule refresh.
37. Write a failing main-window contract test or extracted policy test for
    target navigation state.
38. Implement target navigation and block highlight state.
39. Write a failing extracted policy test for channel dispatch marking fired
    only after channel success.
40. Implement dispatch success accounting.
41. Implement the app popup and verify manually on Windows.
42. Implement capsule pulse and verify manually on Windows.
43. Implement tray balloon notifications and verify manually on Windows.
44. Implement toast notification support and verify shortcut, activation, and
    fallback behavior on Windows.
45. Implement the Task Scheduler reminder-check path and verify task creation,
    trigger update, command execution, and failure status on Windows.

Each step must keep previously passing tests green before moving to the next
behavior.

## Verification

Headless validation:

```sh
cmake -S . -B build-tests -DXTODO_BUILD_APP=OFF -DXTODO_BUILD_TESTS=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests --output-on-failure -C Release
```

Windows app build:

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

Manual Windows verification must cover:

- normal window mode
- desktop mode
- side capsule mode expanded and collapsed
- app hidden to tray
- app not running with Task Scheduler fallback enabled
- startup after Windows login
- sleep and resume
- system time change forward and backward
- Explorer restart and tray icon repair
- multiple reminders in one minute
- toast activation into an existing instance
- toast activation from a cold process
- notification failure status in settings
- high DPI and multi-monitor placement

Linux or WSL validation can prove reminder logic, persistence, text formatting,
and headless contracts. Native Windows notification, tray, focus, toast,
scheduler, capsule, and rendered behavior require Windows evidence.

## Non-Goals

- No per-block reminder settings.
- No conversion between calendar blocks and todo items.
- No separate Windows scheduled task per block.
- No modal reminder dialog.
- No silent fallback between notification channels. If a requested Windows
  channel fails, report status and keep other explicitly enabled channels.
