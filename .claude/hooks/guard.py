#!/usr/bin/env python3
"""Nimecraft edit guard.

Enforces three Global Constraints from docs/plan/README.md before an edit lands:
  1. Determinism lint: generation code avoids toolchain-dependent math and containers.
  2. Platform isolation: OS-specific code only in src/platform.* and tests/test_platform.cpp.
  3. Logic/raylib isolation: window, GPU, audio, input, and clock calls only in device modules.

Modes:
  Hook (default): reads a PreToolUse JSON payload on stdin for Write/Edit/MultiEdit,
    computes the file content the edit would produce, and exits 2 with the violations
    on stderr to block the edit. Exits 0 when the edit is clean or out of scope.
  Scan: `guard.py --scan` checks every file under src/ and tests/ on disk and exits 1
    on any violation. Use it to catch edits made through the shell.

Only the Python 3 standard library is used.
"""

from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2]).resolve()

CPP_SUFFIXES = {".cpp", ".hpp", ".h", ".cc", ".inl"}

# Files that hold world generation (plan: "generation code"). Extend when a new module generates terrain.
GENERATION_STEMS = {"terrain"}

# Files allowed to contain OS-specific code and environment access.
PLATFORM_ALLOWED = {"src/platform.cpp", "src/platform.hpp", "tests/test_platform.cpp"}

# src/ modules allowed to call raylib window, GPU, audio, input, and clock functions.
DEVICE_STEMS = {"render", "audio", "ui_draw", "screens", "game", "main"}

DETERMINISM_RULES = [
    (re.compile(r"\b(?:std::)?(?:sin|cos|tan|asin|acos|atan2?|sinh|cosh|tanh|exp2?|expm1|"
                r"log(?:2|10|1p)?|pow|cbrt|hypot|fma)f?\s*\("),
     "transcendental math (only + - * /, std::sqrt, std::floor are allowed)"),
    (re.compile(r"\bstd::hash\b"), "std::hash (differs between standard libraries)"),
    (re.compile(r"\bunordered_(?:multi)?(?:map|set)\b"), "unordered container (iteration order differs)"),
    (re.compile(r"\b(?:std::)?(?:mt19937(?:_64)?|default_random_engine|random_device|minstd_rand0?|"
                r"ranlux\w*|knuth_b|\w+_distribution)\b"), "<random> engine or distribution"),
    (re.compile(r"\b(?:std::)?s?rand\s*\("), "rand()/srand()"),
]
DETERMINISM_INCLUDES = re.compile(r"^\s*#\s*include\s*<(?:random|unordered_map|unordered_set)>")

PLATFORM_RULES = [
    (re.compile(r"^\s*#\s*(?:if|ifdef|ifndef|elif|elifdef|elifndef)\b.*\b(?:_WIN32|_WIN64|WIN32|__linux__|"
                r"__unix__|__unix|__APPLE__|__MINGW32__|__MINGW64__|__CYGWIN__)\b", re.M),
     "OS #if (OS-specific code belongs in src/platform.*)"),
    (re.compile(r"\b(?:std::)?(?:getenv|_wgetenv|secure_getenv|setenv|unsetenv|putenv|_putenv|_putenv_s|"
                r"_wputenv|_wputenv_s|_dupenv_s|_wdupenv_s)\s*\("),
     "environment variable access (belongs in src/platform.*)"),
]
PLATFORM_INCLUDES = re.compile(r"^\s*#\s*include\s*[<\"](?:windows\.h|unistd\.h|pwd\.h|shlobj\.h|direct\.h|"
                               r"io\.h|sys/stat\.h|sys/types\.h)[>\"]", re.I)

RAYLIB_DEVICE = re.compile(
    r"\b(?:InitWindow|CloseWindow|WindowShouldClose|IsWindow\w*|SetWindow\w*|ToggleFullscreen|"
    r"ToggleBorderlessWindowed|MaximizeWindow|MinimizeWindow|RestoreWindow|GetScreen(?:Width|Height)|"
    r"GetRender(?:Width|Height)|GetMonitor\w*|GetCurrentMonitor|SetConfigFlags|SetTargetFPS|GetFPS|"
    r"GetFrameTime|GetTime|WaitTime|SwapScreenBuffer|PollInputEvents|BeginDrawing|EndDrawing|ClearBackground|"
    r"(?:Begin|End)(?:Mode2D|Mode3D|TextureMode|ShaderMode|BlendMode|ScissorMode|VrStereoMode)|"
    r"Draw[A-Z]\w*|"
    r"(?:Load|Unload)(?:Texture|RenderTexture|Shader|Model|Mesh|Material|Font|Sound|Music|AudioStream)\w*|"
    r"Upload\w*|Update(?:Texture|Mesh|Sound|MusicStream|AudioStream|Camera|Model)\w*|"
    r"GenTexture\w*|SetShaderValue\w*|GetShaderLocation\w*|"
    r"IsKey\w+|GetKeyPressed|GetCharPressed|IsMouse\w+|GetMouse\w+|SetMouse\w+|IsGamepad\w+|GetGamepad\w+|"
    r"IsGesture\w+|GetTouch\w+|ShowCursor|HideCursor|IsCursor\w+|EnableCursor|DisableCursor|"
    r"InitAudioDevice|CloseAudioDevice|IsAudioDeviceReady|SetMasterVolume|PlaySound|StopSound|PauseSound|"
    r"ResumeSound|PlayMusicStream|StopMusicStream|PauseMusicStream|ResumeMusicStream|SetSound\w+|"
    r"SetMusic\w+|PlayAudioStream|TakeScreenshot|rl[A-Z]\w*)\s*\("
)
RAYLIB_INCLUDES = re.compile(r"^\s*#\s*include\s*[<\"](?:rlgl\.h|GLFW/[^>\"]+|GL/[^>\"]+|glad\.h)[>\"]")


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and string/char literals, keeping newlines so line numbers stay valid."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "R" and nxt == '"' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^()\\ ]{0,16})\(', text[i:])
            if m:
                end = text.find(")" + m.group(1) + '"', i)
                j = n if end < 0 else end + len(m.group(1)) + 2
                out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
                i = j
            else:
                out.append(c)
                i += 1
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * max(j - i - 2, 0) + (c if j - i >= 2 else ""))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def find(pattern: re.Pattern, text: str, message: str, raw_lines: list[str]) -> list[str]:
    hits = []
    for m in pattern.finditer(text):
        line_no = text.count("\n", 0, m.start()) + 1
        hits.append(f"  line {line_no}: {message}\n    {raw_lines[line_no - 1].strip()}")
    return hits


def check(rel: str, content: str) -> list[str]:
    path = Path(rel)
    if path.suffix not in CPP_SUFFIXES or path.parts[0] not in {"src", "tests"}:
        return []
    raw_lines = content.split("\n")
    # Keep preprocessor directives intact (they hold #include "..." and #if lines), strip the rest.
    stripped = strip_comments_and_strings(content)
    code = "\n".join(raw if raw.lstrip().startswith("#") else s
                     for raw, s in zip(raw_lines, stripped.split("\n")))
    code_no_pp = "\n".join("" if raw.lstrip().startswith("#") else s
                           for raw, s in zip(raw_lines, stripped.split("\n")))
    hits: list[str] = []

    if path.parts[0] == "src" and path.stem in GENERATION_STEMS:
        for pat, msg in DETERMINISM_RULES:
            hits += find(pat, code_no_pp, "[determinism] " + msg, raw_lines)
        hits += find(DETERMINISM_INCLUDES, code, "[determinism] forbidden include", raw_lines)

    if rel not in PLATFORM_ALLOWED:
        hits += find(PLATFORM_RULES[0][0], code, "[platform] " + PLATFORM_RULES[0][1], raw_lines)
        hits += find(PLATFORM_RULES[1][0], code_no_pp, "[platform] " + PLATFORM_RULES[1][1], raw_lines)
        hits += find(PLATFORM_INCLUDES, code, "[platform] OS header outside src/platform.*", raw_lines)

    if path.parts[0] == "src" and path.stem not in DEVICE_STEMS:
        hits += find(RAYLIB_DEVICE, code_no_pp,
                     "[raylib] window/GPU/audio/input/clock call in a logic module "
                     f"(allowed only in {', '.join(sorted(DEVICE_STEMS))})", raw_lines)
        hits += find(RAYLIB_INCLUDES, code, "[raylib] GPU/window header in a logic module", raw_lines)
    return hits


def relative(file_path: str) -> str | None:
    try:
        return Path(file_path).resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return None


def resulting_content(tool: str, inp: dict) -> str | None:
    path = Path(inp.get("file_path", ""))
    if tool == "Write":
        return inp.get("content", "")
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    edits = inp.get("edits") if tool == "MultiEdit" else [inp]
    for e in edits or []:
        old, new = e.get("old_string", ""), e.get("new_string", "")
        if old not in text:
            return None  # the edit tool will reject this itself
        text = text.replace(old, new) if e.get("replace_all") else text.replace(old, new, 1)
    return text


def hook() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0
    tool = payload.get("tool_name", "")
    inp = payload.get("tool_input") or {}
    if tool not in {"Write", "Edit", "MultiEdit"}:
        return 0
    rel = relative(inp.get("file_path", ""))
    if rel is None:
        return 0
    content = resulting_content(tool, inp)
    if content is None:
        return 0
    hits = check(rel, content)
    if not hits:
        return 0
    print(f"Nimecraft guard blocked this edit to {rel} (Global Constraints, docs/plan/README.md):", file=sys.stderr)
    print("\n".join(hits), file=sys.stderr)
    print("Rewrite the change to follow the constraint. If you believe this is a real exception, "
          "stop and ask the user; do not work around the guard.", file=sys.stderr)
    return 2


def scan() -> int:
    total = 0
    for top in ("src", "tests"):
        base = ROOT / top
        if not base.is_dir():
            continue
        for f in sorted(base.rglob("*")):
            if f.is_file() and f.suffix in CPP_SUFFIXES:
                rel = f.relative_to(ROOT).as_posix()
                hits = check(rel, f.read_text(encoding="utf-8", errors="replace"))
                if hits:
                    total += len(hits)
                    print(f"{rel}:\n" + "\n".join(hits))
    print(f"guard scan: {total} violation(s)")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(scan() if "--scan" in sys.argv[1:] else hook())
