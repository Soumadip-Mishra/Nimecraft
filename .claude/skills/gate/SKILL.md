---
name: gate
description: Run the full Nimecraft verification gate (constraint guard scan, ASan/UBSan tests, TSan tests, Windows cross-build under Wine, Coverage Gate) and report one pass/fail table with evidence. Use before claiming a task or fix is done, or when the user asks to verify, check, or gate the build.
---

# Nimecraft gate

Run every step, even after a failure, so the user sees the whole picture.
Run them one at a time; each build can take minutes.

| # | Step | Command | Pass means |
|---|---|---|---|
| 1 | Guard | `python3 .claude/hooks/guard.py --scan` | exit 0, `0 violation(s)` |
| 2 | Tests | `make test` | exit 0, summary shows `0 failures`, no ASan/UBSan report |
| 3 | TSan | `make tsan` | exit 0, `0 failures`, no `WARNING: ThreadSanitizer` |
| 4 | Windows | `make win` | exit 0, `0 failures` printed through Wine |
| 5 | Coverage | `make coverage` | exit 0; every logic file >= 95% lines and >= 80% branches |

Also grep the build output of steps 2-4 for `warning:`; the project must be warning-free, so any warning counts as a failure.

If the `Makefile` does not exist yet (before M1 Task 1), say the gate is not available yet and run only step 1.
If a tool is missing (for example Wine), mark that step SKIPPED with the reason; never mark it PASS.

## Report

One table: step, PASS / FAIL / SKIPPED, the key line of evidence (the test summary line, the coverage numbers).
For each failure, quote only what matters:

- the failing test names and their `CHECK` lines,
- the first frame block of a sanitizer report,
- each compiler warning with file:line,
- the files under the Coverage Gate, with their uncovered line numbers.

Never say the gate passed unless every step shows its pass evidence in this run's output.
