# Skill: export and ship a Slate game

Use when packaging a playable build.

## From the editor

```python
call({"cmd": "export", "name": "MyGame", "output": "Export"})
```

This writes:

```
Export/MyGame/
  Slate-Runtime.exe
  game.json
  Assets/            (full copy of the project content)
    Scenes/MyGame.json   (serialized live scene)
```

`game.json` contains `{"name", "scene", "engine", "version"}`. The runtime reads
it from its own directory and loads the scene, then enters play mode
automatically.

## Verify the export

```bash
cd Export/MyGame
SLATE_MAX_FRAMES=90 SLATE_SCREENSHOT_PATH=shot.png ./Slate-Runtime.exe
```

Check:

- log says `Loaded scene '...Export/MyGame/Assets/Scenes/MyGame.json'`
- entity count matches the editor
- screenshot shows the game, not a black frame
- no `[ERROR]` / `[CRITICAL]` lines

## Distribution

Ship the whole `Export/MyGame` folder. It has no external dependencies beyond
the Vulkan runtime and the platform C++ runtime already present on the target
machine. Assets resolve next to the executable, so the folder can be moved or
zipped freely.
