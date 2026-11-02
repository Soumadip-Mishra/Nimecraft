# Nimecraft

Single-player survival voxel sandbox in C++20 on raylib 6.0, for Linux and Windows (x86-64).
The design docs are the source of truth; this file only points at them and lists what is costly to get wrong.

- What the game contains: `docs/spec/design.md`
- How it is built, shared rules, and the task index: `docs/plan/README.md` (authoritative)
- One file per task: `docs/plan/m<N>-<name>/<NN>-<task>.md`
- How the code is organized: `docs/ARCHITECTURE.md`

`docs/` is gitignored on purpose (local design notes), so it exists only on this machine.
If it is missing, stop and ask; never recreate the plan from memory.

## Commands

Build only through the Makefile wrappers around the CMake presets; never call `g++` directly or invent build folders.

| Command | Use |
|---|---|
| `make test FILTER=<name>` | One test or a substring (dev preset, ASan + UBSan) |
| `make test` | All tests |
| `make tsan` | All tests under ThreadSanitizer |
| `make win` | MinGW cross-build, tests under Wine |
| `make coverage` | Coverage Gate (95% lines, 80% branches per logic file) |
| `make run` / `make` | Run or build the release game |
| `python3 .claude/hooks/guard.py --scan` | Constraint lint over `src/` and `tests/` |

## Workflow

- Plan tasks: use `/next-task`.
  It follows the plan's Standard Task Cycle; where generic skills (executing-plans, test-driven-development) differ, the plan wins.
- Work inline in the main session; do not delegate implementation to subagents in this repo.
  Before proposing a commit, run the `nimecraft-reviewer` agent on the diff.
- Full verification: `/gate`.
  Bugs found later: `/regress` (failing `test_regress_<name>` first, then the fix).
  Tasks marked **Manual**: `/manual-check`.
- Stop before every commit: show the task report and the proposed message, and commit only after the user approves.
  Stage explicit paths, never `git add -A`.
- One task at a time; never start the next task without approval.
- If a test contradicts the spec, fix the test and say why; if the spec itself looks wrong, stop and ask.
- At the start of each milestone, re-read its tasks against the code and report names that drifted before starting.

## Constraints that bite (full list: plan "Global Constraints")

- Same seed, same terrain on GCC, MinGW, and MSVC: generation uses only integer ops, `+ - * /`, `std::sqrt`, `std::floor`; no `sin`/`cos`/`exp`/`pow`/`log`, `std::hash`, `<random>`, or unordered containers.
  Any generation change updates `TERRAIN_GOLDEN` exactly as M1 Task 3 describes.
- OS-specific code (`#ifdef _WIN32`, environment variables, OS paths) only in `src/platform.*` and `tests/test_platform.cpp`.
- Logic modules use raylib only for math types; window, GPU, audio, input, and clock calls belong in `render`, `audio`, `ui_draw`, `screens`, `game`, `main`.
- Only the main thread touches `World`, entities, and block entities; worker jobs get copies and return values.
- Block, item, and mob ids are persisted: append only, never renumber or reuse.
  The region loader of version N reads every older version; new `world.meta` keys are optional with a stated default.
- Paths are `std::filesystem::path`, files open in binary mode, numbers go through `std::to_chars`/`std::from_chars`.
- Warning-free on GCC/MinGW (`-Wall -Wextra -Wpedantic -Wshadow`) and MSVC (`/W4 /permissive-`); watch implicit narrowing that only MSVC flags.
- raylib 6.0 is the only third-party code; ask before adding any dependency.
- Original content only: no names, recipes, textures, or sounds copied from an existing game.
- Tests: expected values are literals from the spec or plan, never computed by the code under test; never weaken, skip, or delete a test to get green.

A PreToolUse hook (`.claude/hooks/guard.py`) blocks edits that break the first three rules.
It cannot see shell edits (`sed`, heredocs), so prefer Edit/Write for source files; `/gate` re-runs it as `--scan`.

## Facts to check, not recall

- raylib: load the `raylib` skill before touching raylib code; it lists the 5.x habits that break on 6.0 and the renderer limits.
  Check every call against `build/dev/_deps/raylib-src/src/raylib.h` instead of memory.
- Compiler and tool versions: run them (`gcc --version`, `cmake --version`) rather than assuming.

## Code intelligence

The `clangd-lsp` plugin (enabled in `.claude/settings.json`) gives diagnostics and go-to-definition after every edit.
It needs `clangd` on the machine (`sudo apt install clangd`) and `compile_commands.json` from the dev preset, so `CMakePresets.json` must set `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON`; `.clangd` points it at `build/dev`.
Treat clangd diagnostics as hints; the build and `/gate` decide.

## Commits

Conventional Commits, using the message the task file gives; no AI co-author trailers.
