# Slate automation protocol

The editor listens on `127.0.0.1:45800` (set `SLATE_AUTOMATION_PORT` to change
it). The protocol is newline-delimited JSON: send one object per line, receive
one object per line. Every response contains `ok` and echoes `cmd`.

Start the editor headless-ish (a window still opens) and keep it running:

```bash
SLATE_AUTOMATION_PORT=45800 ./bin/Slate-Editor.exe Scenes/SlateBlocks.json
```

The scene can also be passed as `--scene <path>`; `SLATE_SCENE` still works as
a fallback but arguments take precedence.

Minimal client:

```python
import json, socket

sock = socket.create_connection(("127.0.0.1", 45800))
file = sock.makefile("rwb", buffering=0)

def call(request):
    file.write((json.dumps(request) + "\n").encode())
    return json.loads(file.readline())

print(call({"cmd": "ping"}))
```

Entities are addressed by `name` or `uuid`. Names are matched case-sensitively
against `TagComponent::Name`; the first match wins.

## Commands

| Command | Fields | Result |
| --- | --- | --- |
| `ping` | – | `engine: "Slate"` |
| `stats` | – | `fps`, `entities`, `drawCalls`, `triangles`, `physicsBodies`, `scriptInstances`, `playing` |
| `list_entities` | – | `entities: [{name, uuid}]` |
| `list_transforms` | – | `entities: [{name, uuid, translation}]` |
| `create_entity` | `name` | `uuid` of the new entity |
| `select` | `name` or `uuid` | selects the entity (shows the translate gizmo) |
| `delete_entity` | `name` or `uuid` | – |
| `set_transform` | `name`/`uuid`, optional `translation`, `rotation` (degrees), `scale` | – |
| `undo` / `redo` | – | `canUndo`, `canRedo` |
| `get_transform` | `name` or `uuid` | `translation`, `rotation`, `scale` |
| `set_mesh` | `name`/`uuid`, `model` (asset-relative path) | – |
| `add_component` | `name`/`uuid`, `component`, component-specific fields | – |
| `load_scene` | `path` | – |
| `save_scene` | `path` | – |
| `screenshot` | `path` | `path` (captured after the next presented frame) |
| `key` | `key` (name), `down` | injects keyboard state for automation and game testing |
| `mouse` | `button` (`Left`/`Right`/`Middle`), `down` | injects mouse button state |
| `clear_input` | – | clears every injected key and button |
| `play` / `stop` | – | toggles physics, scripts and audio |
| `export` | `name`, optional `output`, `scene` | `output` folder |
| `quit` | – | closes the editor |

### Components accepted by `add_component`

- `meshrenderer`
- `camera`
- `directionallight`
- `pointlight`
- `rigidbody` — `type` (0 static, 1 dynamic, 2 kinematic), `mass`
- `boxcollider` — `halfExtents: [x, y, z]`
- `spherecollider`, `capsulecollider` — `radius`
- `script` — `path`
- `audio` — `clip`

## Example: build a small scene and export it

```python
call({"cmd": "create_entity", "name": "Ball"})
call({"cmd": "set_mesh", "name": "Ball", "model": "Models/BoxTextured.glb"})
call({"cmd": "set_transform", "name": "Ball",
      "translation": [0, 3, 0], "scale": [0.4, 0.4, 0.4]})
call({"cmd": "add_component", "name": "Ball", "component": "rigidbody",
      "type": 1, "mass": 1.0})
call({"cmd": "add_component", "name": "Ball", "component": "boxcollider",
      "halfExtents": [0.2, 0.2, 0.2]})
call({"cmd": "play"})
call({"cmd": "screenshot", "path": "screenshots/ball.png"})
call({"cmd": "export", "name": "BallGame", "output": "Export"})
```

## Notes

- `screenshot` is deferred: the editor captures after the next `EndFrame`, so
  the file appears a frame later.
- `export` serializes the live scene (including unsaved edits) and copies
  `Assets`, the runtime executable and a `game.json` manifest into
  `Export/<name>/`. The runtime reads `game.json` from its own directory.
- The server only binds `127.0.0.1`, so it is not reachable from the network.
