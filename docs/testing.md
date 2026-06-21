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
| `xtodo_store_format_tests` | `data.txt` v1-v4 parsing, latest v4 serialization, escaping, UI-state validation, theme-id key parsing, mount-mode migration |
| `xtodo_theme_tests` | color helpers, built-in theme catalog stability, contrast thresholds, theme resolution and fallback behavior |
| `xtodo_i18n_tests` | all declared UI strings in zh/en, important behavioral strings, default language result validity |
| `xtodo_gui_contract_tests` | headless GUI contracts for non-client hit-testing, geometry capture policy, title/tab/row layout hit-testing, popup menu item models, and inline edit key intents |

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
