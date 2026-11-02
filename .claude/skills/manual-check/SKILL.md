---
name: manual-check
description: Run the Manual verification for a Nimecraft plan task marked (Manual) - the release --smoke run, then hand the user the task's in-game and Windows checklist and record the results. Use when /next-task reaches a Manual task or the user asks to smoke test or play-check the game.
argument-hint: "[task, e.g. m3/05] (default: the task in progress)"
---

# Manual check

1. **Smoke.** Run `make && timeout 120 ./build/release/nimecraft --smoke`.
   It must exit 0; quote the exit code and the last lines of output.
   If there is no display, say so instead of reporting a pass.
2. **Checklist.** Copy the task file's Manual / in-game checks into a numbered checklist for the user.
   Keep each item to one observable result ("break a block at a chunk border: both chunks redraw in the same frame").
3. **Windows.** Add the Windows steps from the plan: `make win`, copy `build/win-cross/nimecraft.exe` to a Windows 10/11 PC, run `nimecraft.exe --smoke`, and repeat the checks that involve input, files, or rendering.
4. **Wait.** Stop and wait for the user's results; you cannot play the game yourself.
5. **Record.** Tick the Manual boxes only for checks the user confirmed.
   Every failed check becomes a `/regress` item; list them for the user rather than fixing them silently.
