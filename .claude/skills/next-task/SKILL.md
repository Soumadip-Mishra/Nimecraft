---
name: next-task
description: Implement the next unchecked Nimecraft plan task, or a named one such as "m1/03", with the plan's Standard Task Cycle - failing tests first, minimal implementation, full gate, coverage, mutation spot check, reviewer pass - then stop for commit approval. Use when the user says next task, continue the plan, or names a plan task.
argument-hint: "[task, e.g. m2/04] (default: first unchecked task)"
---

# Next Nimecraft plan task

Follow `docs/plan/README.md` (Test-First Rules, Coverage Checklist, Coverage Gate, Standard Task Cycle).
This skill is the checklist for running it; the plan wins on any conflict.

## 0. Preflight

1. `git status --short`: the tree must be clean apart from files this task will create.
   If not, list the stray files and ask before continuing.
2. Check tools: `cmake --version` (>= 3.25), `x86_64-w64-mingw32-g++-posix --version`, `wine --version`.
   If any is missing, stop and tell the user to run `! sudo apt install cmake g++-mingw-w64-x86-64-posix wine` (M1 Task 1 Step 1), unless that step is the task at hand.

## 1. Pick the task

- With an argument (`m2/04`, `M2 Task 4`), open that task file.
- Otherwise walk the milestone index in `docs/plan/README.md` in order and take the first task file that still has a `- [ ]` box.
- Read the shared rules in `docs/plan/README.md` (once per session), the milestone `README.md`, and the whole task file.
- If this is the first task of a milestone, compare the milestone's task files with the code as it exists and report any names that drifted before writing anything.

Tell the user in one line which task you are starting.

## 2. Tests first

1. Write every test the task lists, in the listed test file, using the exact names.
2. Walk the 14-row Coverage Checklist against each public function or class the task adds or changes.
   Add a test for every row that applies and is not covered.
   Keep a short table: row number, applies (yes/no), covering test.
3. Expected values are literals from the spec or plan; never call the code under test to compute them.

## 3. Failing run

Run `make test FILTER=<filter>`.
It must fail for the right reason: a missing symbol, or a failing `CHECK` about the behavior under test.
A new test that already passes tests nothing; fix it before going on.
Keep the last ~15 lines of this output for the report.

## 4. Implement

Write only enough code in the listed files to pass the tests.
Use Edit/Write for source files so the guard hook sees every change.
If the guard blocks an edit, change the design; never route around it with shell edits.

## 5. Gate

Run `/gate` (guard scan, `make test`, `make tsan`, `make win`, `make coverage`).
Every touched logic file must meet the Coverage Gate.
A line that truly cannot run in tests is listed in the report with its reason; it is never hidden.

## 6. Mutation spot check

For new code, one at a time: flip one comparison, change one constant, delete one statement.

1. Baseline: `make test FILTER=<filter>` must be green and run more than 0 tests; otherwise stop, the result would mean nothing.
2. Back up the file: `mkdir -p build/mutation && cp src/<file>.cpp build/mutation/`.
3. Apply the mutation with Edit, choosing an `old_string` that occurs exactly once, then run `make test FILTER=<filter>`; it must fail.
4. Restore with `cp build/mutation/<file>.cpp src/<file>.cpp` and confirm with `cmp` before the next mutation.
   Never use `git checkout` or `git stash` to revert; the task's work is still uncommitted.

If a mutation survives, write down a concrete input that gives a wrong result under the mutant, add a test with that input, see it fail against the mutated code, restore, and see it pass.
If no such input exists (an equivalent mutant), say so in the report and pick a different mutation.

## 7. Manual tasks

If the task title says **(Manual)**, run `/manual-check` and wait for the user's in-game results.

## 8. Review

Launch the `nimecraft-reviewer` agent with the task file path.
Fix every CONFIRMED BLOCKER or MAJOR finding, then repeat step 5.
Report PLAUSIBLE findings to the user without acting on them silently.

## 9. Report and stop

Tick the task file's completed `- [ ]` boxes (`docs/` is local, so this is safe).
Then report, with each sentence on its own line:

- Task and files changed.
- Coverage Checklist table from step 2.
- Last lines of the failing run and of the passing run.
- Gate table from `/gate`, with coverage for each touched file.
- Mutation table: mutation, file:line, caught by which test.
- Reviewer verdict and what you fixed.
- Any deviation from the task file, and why.
- The commit message from the task file.

Stop here.
Commit only after the user approves: `git add <explicit paths>` then `git commit -m "<message>"`.
Never start the next task in the same turn.
