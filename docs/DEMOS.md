# Slate demo suite

Five technical demos, each focused on one subsystem. Open them from the
editor's **Demos** menu, or run them directly:

```bash
./build/bin/Slate-Runtime.exe Demos/StudioShowcase.json
./build/bin/Slate-Runtime.exe Demos/PhysicsStress.json
./build/bin/Slate-Runtime.exe Demos/RaycastShooter.json
./build/bin/Slate-Runtime.exe Demos/HudToolkit.json
./build/bin/Slate-Runtime.exe Demos/AudioSpatial.json
```

The editor can also load any of them: **Demos → …** in the menu bar.

## Studio Showcase — rendering

`Assets/Demos/StudioShowcase.json` · `Assets/Scripts/Demos/StudioShowcase.lua`

A cinematic orbit around the Damaged Helmet, ringed by sixteen tinted pillars
and lit by a warm key light, a cool fill light and a drifting rim light.

Demonstrates:

- PBR metallic/roughness materials with normal mapping
- Image-based lighting from an HDRI (irradiance, prefiltered specular, BRDF LUT)
- 2048² PCF shadows with a caster-fitted frustum
- SSAO and contact shadows
- HDR pipeline with ACES tonemapping, bloom and FXAA
- HDRI skybox
- HUD text/rect overlay

Controls: **Space** toggles bloom.

## Physics Stress — Box3D

`Assets/Demos/PhysicsStress.json` · `Assets/Scripts/Demos/PhysicsStress.lua`

A 9×8 wall of boxes plus capsules and spheres, with waves of boxes dropped on
top. Shows the multithreaded Box3D solver under load.

Demonstrates:

- Box, sphere and capsule colliders created from Lua at runtime
- Dynamic/static bodies, friction and mass
- 90–260 simultaneous bodies with stable stacking
- Periodic raycasts and a hit counter
- Gravity toggling and entity destruction/rebuild

Controls: **Space** drops a wave, **R** rebuilds, **G** toggles gravity.

## Raycast Shooter — rays and impulses

`Assets/Demos/RaycastShooter.json` · `Assets/Scripts/Demos/RaycastShooter.lua`

Click to fire a ray from the camera; hits take an impulse along the ray, spawn
a shrinking spark and play a spatial sound.

Demonstrates:

- `Raycast` returning hit point and entity
- `ApplyImpulse` and `SetVelocity` on dynamic bodies
- Runtime entity creation/destruction for effects
- Camera aiming from Lua (arrow keys)
- `PlaySoundAt` spatial one-shots
- Crosshair and hit counter via the HUD API

Controls: **click** fires, **arrows** aim.

## HUD Toolkit — screen-space UI

`Assets/Demos/HudToolkit.json` · `Assets/Scripts/Demos/HudToolkit.lua`

A full HUD built only from `DrawRect` and `DrawText`: top bar with elapsed
time, animated status meters, a radar sweep, a scrolling event log and a
blinking cursor.

Demonstrates:

- Rect and text layout with alpha blending
- Animated meters and radar sweep
- Scrolling log with per-entry fading
- Screen-size aware layout (`ScreenWidth` / `ScreenHeight`)

Controls: **L** appends an event to the log.

Best viewed in the runtime; in the editor the panels overlay the top corners.

## Spatial Audio — 3D emitters

`Assets/Demos/AudioSpatial.json` · `Assets/Scripts/Demos/AudioSpatial.lua`

Three looping emitters (220 Hz, 330 Hz, 440 Hz) orbit the listener. The
listener follows the primary camera, so panning and distance falloff change as
the emitters move.

Demonstrates:

- `AudioSourceComponent` with looping, spatial playback
- Listener tracking the camera
- Runtime transform animation of audio sources
- Pulsing emitter visuals

Controls: **Space** pauses the orbit.

## Feature coverage

| Feature | Studio | Physics | Shooter | HUD | Audio |
| --- | --- | --- | --- | --- | --- |
| PBR / IBL / HDRI | ✔ | ✔ | ✔ | ✔ | ✔ |
| Shadows / SSAO / bloom | ✔ | ✔ | ✔ | ✔ | ✔ |
| Box3D bodies + colliders | | ✔ | ✔ | | |
| Raycasts | | ✔ | ✔ | | |
| Impulses / velocity | | | ✔ | | |
| Lua entity lifecycle | ✔ | ✔ | ✔ | ✔ | ✔ |
| HUD text/rect | ✔ | ✔ | ✔ | ✔ | ✔ |
| Audio (spatial) | | ✔ | ✔ | | ✔ |
| Automation-testable | ✔ | ✔ | ✔ | ✔ | ✔ |
