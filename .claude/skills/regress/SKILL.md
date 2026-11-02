---
name: regress
description: Fix a Nimecraft bug the plan's way (Test-First Rule 5) - reproduce it, write a failing test_regress_<short_name> in the owning module's test file, fix it, run the gate, then stop for approval of a fix commit. Use when a bug turns up while playing, in CI, in review, or in a sanitizer report.
argument-hint: "<bug description or failing output>"
---

# Regression fix

1. **Reproduce.** Use the user's description or output to reproduce the bug with a command or test.
   Find the root cause with the `systematic-debugging` skill before changing code.
2. **Owner.** Name the module that owns the faulty behavior (see the file map in `docs/plan/README.md`).
   The test goes in `tests/test_<module>.cpp`.
   If the bug lives in a device-only file (`render`, `ui_draw`, `screens`, `game`, `main`, device half of `audio`), move the faulty decision into a logic module so it can be tested (Test-First Rule 8).
3. **Failing test.** Add `test_regress_<short_name>` with literal expected values.
   Run `make test FILTER=test_regress_<short_name>`; it must fail for the bug's reason.
   Keep the last lines of the output.
4. **Fix.** Make the smallest change that makes the test pass.
5. **Gate.** Run `/gate`.
6. **Review.** Run the `nimecraft-reviewer` agent on the diff.
7. **Report and stop.** Root cause in one or two sentences, the failing and passing output tails, the gate table, and a proposed `fix: <summary>` message.
   Commit only after the user approves, staging explicit paths.
