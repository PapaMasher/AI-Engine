# Slate

Slate is a production-grade 3D game engine for Windows, macOS and Linux,
written in C++20. It ships an editor that AI agents can drive end-to-end, a
runtime for exported games, and a test suite that exercises every subsystem.

## Features

- **Vulkan renderer** through NVIDIA NVRHI on all platforms.
- **PBR** metallic/roughness workflow with normal mapping and tangent
  generation.
- **HDR pipeline** — RGBA16F scene targets, ACES tonemapping, exposure.
- **IBL** — equirectangular HDRI → cubemap → irradiance, GGX prefiltered
  specular, BRDF LUT.
- **Shadows** — 2048² PCF soft shadow maps.
- **SSAO** with a separable blur pass.
- **glTF/GLB import** with materials, textures and submeshes.
- **ECS** with generation-checked entity handles, hierarchy, JSON scenes and
  prefabs.
- **Physics** — Jolt rigid bodies, box and sphere colliders, raycasts.
- **Lua scripting** — `OnCreate` / `OnUpdate` / `OnDestroy`, entity creation and
  destruction, physics queries.
- **Audio** — miniaudio with spatial playback.
- **Editor** — ImGui docking UI, hierarchy, inspector, statistics, gizmo-free
  transform editing, free-fly camera.
- **AI automation** — newline-delimited JSON over TCP to build scenes, take
  screenshots, play, export and quit.
- **Export** — one command produces a self-contained game folder.

## Build

Windows (MinGW-w64):

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

You need a Vulkan 1.2+ driver, CMake 3.20+, Ninja, a C++20 compiler and
`glslangValidator` on `PATH` (or `-DSLATE_GLSLANG=/path/to/glslangValidator`).

## Run

```bash
./build/bin/Slate-Editor.exe      # editor
./build/bin/Slate-Runtime.exe     # runtime (loads game.json if present)
```

Frame-limited smoke run:

```bash
SLATE_MAX_FRAMES=120 SLATE_SCREENSHOT_PATH=shot.png ./build/bin/Slate-Editor.exe
```

## Test

```bash
cd build && ctest --output-on-failure
```

`SlateIntegrationTests` renders a hidden-window orientation scene and asserts
from real pixels that world +Y appears above world -Y. This guards the renderer
against NDC and viewport convention regressions.

## AI automation

Start the editor with `SLATE_AUTOMATION_PORT=45800` and send JSON lines:

```json
{"cmd":"create_entity","name":"Cube"}
{"cmd":"set_mesh","name":"Cube","model":"Models/BoxTextured.glb"}
{"cmd":"set_transform","name":"Cube","translation":[0,1,0]}
{"cmd":"screenshot","path":"shot.png"}
{"cmd":"export","name":"MyGame"}
```

See [docs/AUTOMATION_PROTOCOL.md](docs/AUTOMATION_PROTOCOL.md) for the complete
command set and the skills in `skills/` for worked workflows.

## Layout

```
Slate/          engine library
Slate-Editor/   editor executable
Slate-Runtime/  runtime executable
Tests/          unit and integration tests
Assets/         scenes, models, HDRIs, shaders, scripts, audio
cmake/          dependency and shader build logic
third_party/    vendored dependencies
docs/           protocol and design notes
skills/         agent playbooks
```

## License

Vendored dependencies retain their own licenses; see `third_party/`.
