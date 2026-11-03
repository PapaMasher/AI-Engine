# Skill: build and test Slate

Use when you need to build, test or debug the engine.

## Build (Windows reference toolchain)

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=C:/msys64/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/mingw64/bin/g++.exe
cmake --build build -j 12
```

The build copies `Assets/` into `build/Assets` and compiles every GLSL shader
to SPIR-V there. `Slate-Runtime` resolves assets next to its own executable
first, so exported builds are self-contained; development builds fall back to
`build/Assets`.

## Test

```bash
cd build && ctest --output-on-failure
```

- Unit tests: `SlateTests` (core, assets, ECS, scene, physics, scripting).
- Pixel regression: `SlateIntegrationTests` renders `Scenes/OrientationTest.json`
  in a hidden window and asserts red (+Y) appears above green (-Y). If it fails,
  the renderer orientation is broken — do not "fix" the test.

## Smoke runs

```bash
SLATE_MAX_FRAMES=120 ./bin/Slate-Editor.exe
SLATE_MAX_FRAMES=120 SLATE_SCREENSHOT_PATH=shot.png ./bin/Slate-Editor.exe
SLATE_SCENE=Scenes/OrientationTest.json ./bin/Slate-Runtime.exe
```

Check the log for `[ERROR]` / `[CRITICAL]`; a clean run must print zero.

## Rules

- A change is not done until it builds, passes `ctest`, and (for rendering)
  was checked visually.
- Add a unit test in `Tests/` for every new component, field or behaviour.
- Keep warnings at zero; `SLATE_WARNINGS_AS_ERRORS=ON` is the gate.
