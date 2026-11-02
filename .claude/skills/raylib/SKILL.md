---
name: raylib
description: raylib 6.0 rules and version pitfalls for Nimecraft. Load before writing or changing code that calls raylib, rlgl, or raymath (render, audio, ui_draw, screens, game, main, embedded GLSL shaders, mob models, icons), and whenever a raylib call fails to compile or behaves unexpectedly.
---

# raylib in Nimecraft

## Version

- Pinned: tag `6.0` (commit `dbc56a87`), fetched by CMake `FetchContent` (M1 Task 1).
- Tag `6.2` also exists upstream.
  Moving to it is a plan and dependency change: ask the user first, never bump it as a side effect.
- Most training data and online examples target raylib 5.x, and the online cheatsheet may show a newer version than 6.0.
  Neither is the source of truth.

## Source of truth

Before using any raylib function, type, or constant, grep the pinned headers in the build tree:

```bash
grep -n "RLAPI.*<Name>" build/dev/_deps/raylib-src/src/raylib.h
grep -n "<Name>" build/dev/_deps/raylib-src/src/rlgl.h build/dev/_deps/raylib-src/src/raymath.h
```

To see what a function frees or allocates, read `build/dev/_deps/raylib-src/src/r*.c`.
If the build tree does not exist yet, read `https://raw.githubusercontent.com/raysan5/raylib/6.0/src/raylib.h`.

## 5.x habits that break on 6.0 (verified by diffing the 5.5 and 6.0 headers)

| Area | 5.5 | 6.0 |
|---|---|---|
| `DrawCircleGradient` | `(int centerX, int centerY, float radius, Color inner, Color outer)` | `(Vector2 center, float radius, Color inner, Color outer)` |
| `DrawRectangleGradientEx` | `(rec, topLeft, bottomLeft, topRight, bottomRight)` | `(rec, topLeft, bottomLeft, bottomRight, topRight)`: the last two corners swapped |
| `UpdateModelAnimation` | `int frame` | `float frame`; `UpdateModelAnimationEx` blends two animations |
| Removed | `UpdateModelAnimationBones`, `UnloadModelAnimation`, `DrawModelPoints`, `DrawModelPointsEx` | gone |
| `Mesh` skinning fields | `boneIds`, `boneMatrices` | `boneIndices`; `boneMatrices` removed |
| `SetSoundPan` / `SetMusicPan` | `0.5` is center | `-1.0` left, `0.0` center, `1.0` right |
| `LoadFontData` | ends `int codepointCount, int type` | adds `int *glyphCount` out-param; `codepoints` is `const int *` |
| Text helpers | `TextToUpper` and friends return `const char *` | return `char *`; `TextReplace(text, search, replacement)` |
| rlgl shaders | `rlCompileShader`, `rlLoadShaderCode`, `rlLoadShaderProgram(vsId, fsId)` | `rlLoadShader`, `rlLoadShaderProgram(const char *vsCode, const char *fsCode)`, `rlLoadShaderProgramEx` |

New in 6.0, so do not hand-roll them in device code: `FileRename`, `FileCopy`, `FileMove`, `FileRemove`, `GetKeyName` (layout-aware key names for the settings screen), `ComputeSHA256`.
Saves still go through `storage` with `std::filesystem` (Global Constraints), not raylib file functions.

## Facts that shape the voxel renderer (raylib 6.0 source)

- `Mesh.indices` is `unsigned short *`, so an indexed mesh holds at most 65,535 vertices.
  Size chunk meshes to stay under it, split them, or draw them non-indexed; add a test for the worst case in the mesher.
- `UnloadMesh` calls `RL_FREE` on `vertices`, `texcoords`, `normals`, `colors`, `tangents`, `texcoords2`, and `indices`.
  CPU arrays handed to a `Mesh` must come from `RL_MALLOC`/`MemAlloc`, never from a `std::vector`'s storage.
- `UploadMesh(Mesh *mesh, bool dynamic)` creates the VAO and VBOs; `UpdateMeshBuffer` rewrites one buffer in place.
- Default shader bindings: attributes `vertexPosition` (location 0), `vertexTexCoord` (1), `vertexNormal` (2), `vertexColor` (3); uniforms `mvp`, `colDiffuse`; sampler `texture0`.
  Embedded GLSL 330 shaders that use these names get them bound by `LoadShaderFromMemory(vsCode, fsCode)`; check the result with `IsShaderValid`.

## Project rules that involve raylib

- Every raylib, rlgl, and OpenGL call runs on the main thread, which owns the GL context.
  Workers return plain `MeshData`; the main thread copies it into a `Mesh` and uploads it.
- Window, GPU, audio, input, and clock calls (`GetTime`, `GetFrameTime`) live only in `render`, `audio`, `ui_draw`, `screens`, `game`, and `main`; the guard hook enforces this.
  Logic modules use `raylib.h`/`raymath.h` for math types only.
- Never include `raylib.h` in `src/platform.cpp`: `<windows.h>` declares names that clash with raylib's (`Rectangle`, `CloseWindow`, `ShowCursor`, `DrawText`, ...).
- Textures, icons, and sounds are generated in code.
  Produce pixels and samples as plain arrays in a logic module (`synth`, icon generation) so they can be tested; the device module only wraps them in `Image`, `Texture2D`, `Wave`, or `Sound`.
- Graphics stay within OpenGL 3.3 core / GLSL 330.
