# Testing

X-TODO now has multiple focused unit-test targets instead of one large test file.
The app itself is Win32 + Direct2D, but the tested core layers are kept free of
Win32 dependencies so they run in CI on both Linux and Windows.

## Test targets

| Target | Coverage |
| --- | --- |
| `xtodo_model_core_tests` | default model, active/completed partitioning, text edits, invalid-index no-ops, legacy single-list normalization |
| `xtodo_model_tree_tests` | multi-level trees, subtree completion/restore, subtree deletion, drag reordering, indent/outdent, collapse cleanup |
| `xtodo_model_list_tests` | multi-list isolation, list id generation, current-list selection, rename/delete, per-list completed expansion |
| `xtodo_model_regression_tests` | regressions from recent git history: insert-after-subtree, completed-block ordering, list deletion selection, id collisions, deterministic invariant fuzzing |
| `xtodo_model_property_tests` | broad model invariants after long mutation sequences, subtree contracts, `ReplaceLists` idempotence, invalid-operation reference transparency |
| `xtodo_store_format_tests` | `data.json` round-trip, UTF-8 fidelity, UI-state enum validation and range clamping, malformed/deeply-nested/non-object rejection, calendar normalization, safe defaults on empty input |
| `xtodo_store_format_hardening_tests` | adversarial persistence coverage for invalid input non-mutation, bracket-depth guard behavior, imported data normalization, unsafe UI fields, rich full-state round-trips |
| `xtodo_reminder_service_tests` | reminder candidate generation, due scanning, catch-up rules, fired-key de-duplication, and log pruning |
| `xtodo_reminder_format_tests` | reminder settings and reminder log persistence contracts |
| `xtodo_reminder_text_tests` | single and grouped reminder display text shared by popup and notification surfaces |
| `xtodo_reminder_popup_policy_tests` | Win32-free reminder popup monitor work-area placement policy |
| `xtodo_reminder_visual_policy_tests` | Win32-free capsule reminder visual delivery policy |
| `xtodo_reminder_scheduler_tests` | Task Scheduler reminder-check trigger boundary formatting |
| `xtodo_reminder_scheduler_policy_tests` | Win32-free Task Scheduler fallback enable/delete/register decisions |
| `xtodo_reminder_timer_policy_tests` | Win32-free reminder timer delay policy for capped wakeups and immediate due scans |
| `xtodo_reminder_settings_policy_tests` | Win32-free reminder settings toggle contracts used by the settings window |
| `xtodo_reminder_dispatch_policy_tests` | Win32-free reminder dispatch success contract for fired-log marking |
| `xtodo_windows_notifier_tests` | Win32-free notification contracts for toast activation args and XML escaping |
| `xtodo_theme_tests` | color helpers, built-in theme catalog stability, contrast thresholds, theme resolution and fallback behavior |
| `xtodo_i18n_tests` | all declared UI strings in zh/en, important behavioral strings, default language result validity |
| `xtodo_launch_command_tests` | Win32-free command-line activation contracts for reminder checks and toast target opening |
| `xtodo_gui_contract_tests` | headless GUI contracts for non-client hit-testing, geometry capture policy, title/tab/row layout hit-testing, popup menu item models, and inline edit key intents |
| `xtodo_calendar_date_tests` | focused Gregorian date parsing, leap-year rules, date arithmetic, weekday, week start, and month-grid anchors |
| `xtodo_calendar_date_property_tests` | full-cycle Gregorian property coverage for leap years, month lengths, parse/format round-trips, date reversibility, and strict parsing |
| `xtodo_calendar_model_property_tests` | calendar block ordering, day filtering, range normalization, imported id de-duplication, invalid day rejection, and mutation safety |
| `xtodo_calendar_layout_tests` | focused calendar header, week, timeline text, lane, and month-grid layout contracts |
| `xtodo_calendar_layout_property_tests` | responsive calendar layout and hit-testing invariants across widths, DPI scales, day/week/month views, time parsing, snapping, drag ranges, and lane packing |

## Run with CMake

```bash
cmake -S . -B build-tests -DXTODO_BUILD_APP=OFF -DXTODO_BUILD_TESTS=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests --output-on-failure -C Release
```

On single-config generators such as Makefiles or Ninja, the `--config` / `-C`
arguments are harmless but optional:

```bash
cmake -S . -B build-tests -DXTODO_BUILD_APP=OFF -DXTODO_BUILD_TESTS=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

`XTODO_BUILD_TESTS` defaults to `OFF`, so a normal app build keeps the existing
behavior:

```powershell
cmake -B build -A x64
cmake --build build --config MinSizeRel
```

On non-Windows systems, `XTODO_BUILD_APP` is forced off because the app target
links Win32, Direct2D, and DirectWrite libraries.

## Regression policy

When a bug is fixed, add a test to the smallest matching target. Use
`xtodo_model_regression_tests` for behavior that maps directly to a historical
regression or a git commit, and keep the other model tests focused on stable
feature contracts. The deterministic fuzz test should stay seeded so failures
are reproducible.

GUI regressions should first be pushed into a small Win32-free policy module,
then covered by `xtodo_gui_contract_tests`. Current examples are
`WindowHitTest`, `GeometryPolicy`, `ViewLayout`, `MenuModel`, and `EditIntent`.
The native window code must call those modules instead of duplicating the same
math in `MainWindow.cpp` or `MainWindowView.cpp`; otherwise the headless tests
are only checking a parallel implementation.

Property and hardening targets should assert durable public behavior: invariants,
idempotence, non-mutation on failed parses, and deterministic broad input spaces.
Avoid source-token scanning as a unit-test substitute. Rendering backend changes
need either a shared executable policy module, a GUI contract, or real rendered
artifact evidence.

## CI coverage

The `build` workflow runs the full CMake test suite on `ubuntu-latest` and
`windows-latest`. The release path waits for the full unit-test suite and the
Windows app build before creating tags or publishing release artifacts.

Local Linux environments without CMake can still sanity-check the GUI contract
target directly:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -I src -I tests \
  tests/gui_contract_tests.cpp \
  src/EditIntent.cpp src/GeometryPolicy.cpp src/MenuModel.cpp \
  src/ViewLayout.cpp src/WindowHitTest.cpp src/I18n.cpp \
  -o /tmp/xtodo_gui_contract_tests
/tmp/xtodo_gui_contract_tests
```
