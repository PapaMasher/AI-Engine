# AGENTS.md

Guidance for AI agents and humans working in this repository. Slate is a
production-grade 3D engine; every change must be tested and must follow the
conventions below.

## What this is

Slate is a C++20 3D game engine with:

- **Rendering** — Vulkan through NVIDIA NVRHI (Vulkan backend on every
  platform), PBR metallic/roughness, HDR pipeline with ACES tonemapping, IBL
  from HDRIs, PCF soft shadow maps, SSAO.
- **Scene** — entity-component system with generation-checked handles,
  hierarchy, JSON serialization and prefabs.
- **Assets** — glTF/GLB import (cgltf) with materials and textures, HDR
  environment maps, PNG/JPG textures (stb_image).
- **Physics** — Jolt Physics, rigid bodies and box/sphere colliders driven by
  components.
- **Scripting** — Lua 5.4 through sol2 with per-entity environments and
  `OnCreate` / `OnUpdate` / `OnDestroy` callbacks.
- **Audio** — miniaudio, 2D and spatial playback.
- **Editor** — ImGui docking editor with a screen-space translate gizmo, fully
  drivable by an AI agent through a JSON automation server.
- **Runtime** — separate executable that runs exported games.

## Repository layout

```
CMakeLists.txt        Root build, options
cmake/                Dependency wiring and shader compilation
Slate/                Engine static library
  include/Slate/      Public headers (Core, Platform, Renderer, Scene,
                      Assets, Physics, Scripting, Audio, Editor)
  src/                Implementation
Slate-Editor/         Editor executable (docking UI + automation server)
Slate-Runtime/        Runtime executable (runs exported games)
Tests/                doctest unit tests + Vulkan integration test
Assets/               Content shipped with the build
  Scenes/             JSON scenes (TestScene, OrientationTest)
  Models/             glTF/GLB models
  HDRIs/              HDR environment maps
  Scripts/            Lua scripts
  Audio/              WAV/OGG clips
  Shaders/            GLSL shaders (compiled to SPIR-V at build time)
docs/                 Automation protocol and design notes
skills/               Agent playbooks for common workflows
third_party/          Vendored dependencies (no submodules, no downloads)
```

## Build

Windows (MinGW-w64, the reference toolchain on this machine):

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j 12
```

Linux (Ubuntu 24+):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j $(nproc)
```

Requirements: CMake 3.24+, Ninja, a C++20 compiler, Vulkan 1.2+ drivers, and
`glslangValidator` (set `SLATE_GLSLANG` or put it on `PATH`).

Options:

| Option | Default | Meaning |
| --- | --- | --- |
| `SLATE_BUILD_EDITOR` | ON | Build `Slate-Editor` and `Slate-Runtime` |
| `SLATE_BUILD_TESTS` | ON | Build `SlateTests`, `SlateIntegrationTests` |
| `SLATE_WARNINGS_AS_ERRORS` | OFF | Fail the build on warnings |
| `SLATE_ENABLE_VALIDATION` | ON | Vulkan validation layers |

## Test

```bash
cd build
ctest --output-on-failure
```

- `SlateTests` — unit tests for core, assets, ECS, scene, physics, scripting.
- `SlateIntegrationTests` — renders `Scenes/OrientationTest.json` into a hidden
  window and asserts with real pixels that world +Y appears above world -Y.
  This is the regression test for renderer orientation; never weaken it.

Every new component, serialization field, physics behaviour or scripting API
must come with a unit test in `Tests/`. Rendering changes need either a pixel
test or a screenshot reviewed by a human.

Frame-limited runs are useful for smoke tests:

```bash
SLATE_MAX_FRAMES=120 SLATE_SCREENSHOT_PATH=shot.png ./bin/Slate-Editor.exe
```

A clean run prints zero `[ERROR]` / `[CRITICAL]` lines.

## Code style (Hazel)

Match [Hazel](https://docs.hazelengine.com/HazelForEngineers/DeveloperGuide#naming):

- Types, enums and enum values: `PascalCase` (`MeshRendererComponent`,
  `BodyType::Dynamic`).
- Functions and methods: `PascalCase` (`CreateEntity`, `GetCameraData`).
- Local variables and parameters: `camelCase` (`deltaTime`, `entity`).
- Member variables: `m_PascalCase` (`m_Scene`, `m_CommandList`).
- Static members: `s_PascalCase` (`s_AssetRoot`).
- Namespaces: `PascalCase`; the engine uses the flat `Slate` namespace with
  prefixed class names.
- Headers: `#pragma once`, include what you use, no `using namespace` in
  headers.
- Constants: `kPascalCase` (`kShadowMapSize`).
- Prefer `Ref<T>` / `Scope<T>` (`std::shared_ptr` / `std::unique_ptr`) over raw
  owning pointers; NVRHI handles are already reference counted.
- 4-space indentation, braces on their own line for functions and control flow.
- No tabs, no trailing whitespace, ASCII source unless a file genuinely needs
  UTF-8.

Production rules:

- No hacks, no TODOs that ship, no dead code, no commented-out blocks.
- Every resource has an owner and a shutdown path; no leaks.
- Log errors through `SLATE_ERROR` / `SLATE_WARN`; never swallow failures.
- Public headers stay stable; breaking changes require updating call sites and
  tests in the same commit.
- Do not copy code from other engines. Behaviour and naming may match Hazel,
  the implementation must be ours.

## Editor automation (AI control)

The editor exposes a newline-delimited JSON server on `127.0.0.1:45800`
(override with `SLATE_AUTOMATION_PORT`). One JSON object per line in, one JSON
object per line out. See `docs/AUTOMATION_PROTOCOL.md` for the full command
list.

```python
import json, socket

sock = socket.create_connection(("127.0.0.1", 45800))
file = sock.makefile("rwb", buffering=0)

def call(request):
    file.write((json.dumps(request) + "\n").encode())
    return json.loads(file.readline())

print(call({"cmd": "ping"}))
```

The API can create/delete/select entities, read and write transforms, assign
meshes, add components, load/save scenes, take screenshots, toggle play mode,
export a game and quit. It is polled on the main thread, so it is safe to use
while the editor renders.

## Asset conventions

- Models: glTF 2.0 binary (`.glb`) with tangents; Y-up, metres.
- Scenes: `Assets/Scenes/<Name>.json`, serialized by `Scene::Save`.
- Scripts: `Assets/Scripts/<Name>.lua`; entity scripts implement
  `OnCreate(entity)`, `OnUpdate(entity, deltaTime)`, `OnDestroy(entity)`.
- Audio: 44.1 kHz mono/stereo WAV or OGG in `Assets/Audio/`.
- HDRIs: `.hdr` equirectangular in `Assets/HDRIs/`.
- Textures referenced by models are embedded in the GLB or live beside it.
- Never hand-edit generated SPIR-V; edit the GLSL in `Assets/Shaders/`.

## Sample game

`Assets/Scenes/SlateBlocks.json` plus `Assets/Scripts/SlateBlocks.lua` is a
playable 3D Tetris written entirely in Lua against the public scripting API.
It spawns and destroys every block itself, keeps its own board state, clears
lines and keeps score, so it doubles as an end-to-end scripting test.

```bash
SLATE_SCENE=Scenes/SlateBlocks.json ./bin/Slate-Runtime.exe
```

Controls: Left/Right or A/D move, Down or S soft drop, Up or W rotate, Space
hard drop, Enter restarts after game over.

## Play mode

`Application::SetPlaying(true)` snapshots the scene to JSON before physics and
scripts start. Stopping play restores that snapshot, so entities spawned or
moved during play never leak into the authored scene.

## Definition of done

A change is done when:

1. It builds cleanly with the reference toolchain.
2. `ctest` passes, including any new tests for the change.
3. Rendering changes were verified with a screenshot (and a pixel test where
   possible).
4. The change follows the style rules above.
5. A code review has been performed and the commit message describes the
   behaviour change, not the file list.
