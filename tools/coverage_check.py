#!/usr/bin/env python3
"""Coverage Gate for Nimecraft (docs/plan/README.md, "Coverage Gate").

Usage: tools/coverage_check.py <build_dir> [--min-line 95] [--min-branch 80]

Runs `gcov --json-format --stdout --branch-probabilities` on every .gcda file under
<build_dir>, merges line and branch counts for each src/*.cpp, prints a table, and applies
the gate to logic files
(every src/*.cpp except the device files below). Branches gcov marks as exception paths
("throw": true) are ignored. A logic file that no test reached has no data and fails.

Exit status: 0 when every logic file passes, 1 when any fails, 2 on a usage or gcov error.
Only the Python 3 standard library and GCC's own gcov are used.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
# Files that need a window, GPU, or audio device; covered by --smoke and the Manual checks instead.
DEVICE_FILES = {"render.cpp", "ui_draw.cpp", "screens.cpp", "game.cpp", "main.cpp", "audio.cpp"}


class FileCoverage:
    """Line and branch results for one source file, merged across gcda files and instantiations."""

    def __init__(self) -> None:
        self.lines: dict[int, bool] = {}
        self.branches: dict[tuple[int, int], bool] = {}

    def add(self, entry: dict) -> None:
        n = entry["line_number"]
        self.lines[n] = self.lines.get(n, False) or entry["count"] > 0
        index = 0
        for branch in entry.get("branches", []):
            if branch.get("throw"):
                continue
            key = (n, index)
            self.branches[key] = self.branches.get(key, False) or branch["count"] > 0
            index += 1

    def uncovered_lines(self) -> list[int]:
        return sorted(n for n, hit in self.lines.items() if not hit)

    def untaken_branch_lines(self) -> list[int]:
        return sorted({n for (n, _), hit in self.branches.items() if not hit})


def percent(covered: int, total: int) -> float:
    return 100.0 if total == 0 else 100.0 * covered / total


def ranges(numbers: list[int]) -> str:
    """[3, 4, 5, 9] -> '3-5, 9'"""
    parts: list[str] = []
    start = prev = None
    for n in numbers + [None]:
        if start is not None and (n is None or n != prev + 1):
            parts.append(str(start) if start == prev else f"{start}-{prev}")
            start = None
        if n is not None and start is None:
            start = n
        prev = n
    return ", ".join(parts)


def collect(build_dir: Path) -> dict[Path, FileCoverage]:
    gcda_files = sorted(str(p.resolve()) for p in build_dir.rglob("*.gcda"))
    results: dict[Path, FileCoverage] = {}
    if not gcda_files:
        return results
    # --branch-probabilities is what makes gcov put branch records into the JSON at all.
    proc = subprocess.run(["gcov", "--json-format", "--stdout", "--branch-probabilities", *gcda_files],
                          cwd=build_dir, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"gcov exited with status {proc.returncode}")
    for text in proc.stdout.splitlines():
        if not text.strip():
            continue
        doc = json.loads(text)
        cwd = Path(doc.get("current_working_directory", build_dir))
        for record in doc.get("files", []):
            path = (cwd / record["file"]).resolve()
            if path.parent != SRC or path.suffix != ".cpp":
                continue
            cov = results.setdefault(path, FileCoverage())
            for entry in record.get("lines", []):
                cov.add(entry)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description="Per-file coverage report and gate.")
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--min-line", type=float, default=95.0)
    parser.add_argument("--min-branch", type=float, default=80.0)
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        print(f"coverage_check: no such build folder: {args.build_dir}", file=sys.stderr)
        return 2
    try:
        results = collect(build_dir)
    except (OSError, RuntimeError, json.JSONDecodeError) as err:
        print(f"coverage_check: {err}", file=sys.stderr)
        return 2

    rows: list[tuple[str, str, str, str]] = []
    failures: list[str] = []
    for source in sorted(SRC.glob("*.cpp")):
        name = f"src/{source.name}"
        gated = source.name not in DEVICE_FILES
        cov = results.get(source.resolve())
        if cov is None:
            verdict = "FAIL" if gated else "not gated"
            rows.append((name, "no data", "no data", verdict))
            if gated:
                failures.append(f"{name}: no coverage data (no test reaches this file)")
            continue

        line_hit = sum(cov.lines.values())
        line_total = len(cov.lines)
        branch_hit = sum(cov.branches.values())
        branch_total = len(cov.branches)
        line_pct = percent(line_hit, line_total)
        branch_pct = percent(branch_hit, branch_total)
        ok = line_pct >= args.min_line and branch_pct >= args.min_branch
        verdict = ("pass" if ok else "FAIL") if gated else "not gated"
        rows.append((name,
                     f"{line_hit}/{line_total} {line_pct:5.1f}%",
                     f"{branch_hit}/{branch_total} {branch_pct:5.1f}%",
                     verdict))
        if gated and not ok:
            detail = [f"{name}: lines {line_pct:.1f}% (min {args.min_line:g}), "
                      f"branches {branch_pct:.1f}% (min {args.min_branch:g})"]
            if cov.uncovered_lines():
                detail.append(f"  uncovered lines: {ranges(cov.uncovered_lines())}")
            if cov.untaken_branch_lines():
                detail.append(f"  lines with untaken branches: {ranges(cov.untaken_branch_lines())}")
            failures.append("\n".join(detail))

    header = ("file", "lines", "branches", "gate")
    widths = [max(len(r[i]) for r in rows + [header]) for i in range(4)]
    for row in [header] + rows:
        print("  ".join(cell.ljust(w) for cell, w in zip(row, widths)).rstrip())

    if failures:
        print(f"\nCoverage Gate FAILED (lines >= {args.min_line:g}%, branches >= {args.min_branch:g}%):")
        print("\n".join(failures))
        return 1
    print(f"\nCoverage Gate passed (lines >= {args.min_line:g}%, branches >= {args.min_branch:g}%).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
