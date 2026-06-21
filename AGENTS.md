# X-TODO Agent Instructions

## Project Shape

- X-TODO is a Windows-only Win32 desktop app using Direct2D, DirectWrite, C++17, and CMake.
- The app target links Win32, Direct2D, and DirectWrite libraries. Linux and WSL checks can cover source structure and headless tests, not real Windows UI behavior.
- Do not mix this repository with other todo app checkouts. Anchor commands at this repository root.
- README files are user-facing. Long-lived implementation rules belong in `docs/conventions/`.

## Before Editing

- Run `git status --short --branch` before staging or committing.
- Treat existing unrelated changes as user-owned. Do not revert or include them unless the user explicitly asks.
- Do not modify, stage, commit, or delete `VERSION` unless the user explicitly asks for a release or version change.
- Read the relevant convention file before changing a behavior area.
- Keep changes scoped to the requested behavior. Do not refactor nearby code just because it is adjacent.

## Documentation Layers

- `docs/conventions/` stores durable maintenance knowledge and hard-won implementation constraints.
- `docs/tasks/` is not a long-term archive. Do not add completed task specs or enduring rules there.
- `docs/testing.md` is the test entry point.
- Generated or user-facing release details do not belong in conventions unless they affect future implementation decisions.

## Build And Verification

- For tests, follow `docs/testing.md`.
- Preferred local test path when CMake is available:

```sh
cmake -S . -B build-tests -DXTODO_BUILD_APP=OFF -DXTODO_BUILD_TESTS=ON
cmake --build build-tests --config Release --parallel
ctest --test-dir build-tests --output-on-failure -C Release
```

- On non-Windows systems, `XTODO_BUILD_APP` is forced off. Do not claim that the native app was built locally from Linux or WSL.
- For Windows app builds, mirror the CI path: `cmake -B build -A x64` and `cmake --build build --config MinSizeRel`.
- Visual behavior, tray behavior, Explorer restart behavior, high contrast, IME, DPI, and multi-monitor behavior require Windows CI artifacts or real Windows host evidence.
- Markdown-only and `docs/**` changes do not trigger the `build` workflow on push because `.github/workflows/build.yml` ignores those paths.

## Area Pointers

- Theme system: `docs/conventions/theme-system.md`
- Side capsule behavior: `docs/conventions/capsule.md`
- Dialogs and menus: `docs/conventions/dialogs-and-menus.md`
- Surface frames and child HWND frames: `docs/conventions/surface-rendering.md`
- Hit-testing and geometry: `docs/conventions/window-hittest-and-geometry.md`
- Multilevel list storage and behavior: `docs/conventions/multilevel-lists.md`
- Testing layout and regression policy: `docs/testing.md`

## Commit And Push

- Stage only the files that belong to the agreed scope.
- Use `git diff --cached --check` before committing.
- If `VERSION` is dirty and the user did not request a release or version change, leave it unstaged.
- After push, verify the remote branch points at the committed SHA. Do not report CI success unless a workflow actually ran and was checked.
