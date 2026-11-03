# Skill: drive the Slate editor from an AI agent

Use when you must author scenes, take screenshots, play or export without a
human touching the UI.

## Start

```bash
SLATE_AUTOMATION_PORT=45800 ./bin/Slate-Editor.exe
```

Wait until the log prints `Automation server listening on 127.0.0.1:45800`.
The editor must keep running; do not use `SLATE_MAX_FRAMES` unless you want it
to exit automatically.

## Talk to it

```python
import json, socket

sock = socket.create_connection(("127.0.0.1", 45800))
file = sock.makefile("rwb", buffering=0)

def call(request):
    file.write((json.dumps(request) + "\n").encode())
    return json.loads(file.readline())

print(call({"cmd": "ping"}))
```

Full command reference: `docs/AUTOMATION_PROTOCOL.md`.

## Typical loop

1. `list_entities` to learn the scene.
2. `create_entity` → `set_mesh` → `set_transform` → `add_component` to build.
3. `save_scene` so the work survives.
4. `play` to run physics/scripts/audio; `stats` to confirm bodies and scripts.
5. `screenshot` (deferred one frame) and inspect the PNG.
6. `export` to package a runnable build.

## Testing a game

Inject input instead of asking a human to press keys:

```python
call({"cmd": "key", "key": "Left", "down": True})
time.sleep(0.2)
call({"cmd": "key", "key": "Left", "down": False})
call({"cmd": "list_transforms"})   # read positions back
call({"cmd": "clear_input"})
```

`list_transforms` returns every entity's translation, which is enough to
reconstruct board state and drive an automated playthrough. `key` accepts
letters, `Space`, `Enter`, `Escape`, `Up`/`Down`/`Left`/`Right`, and modifier
names.

## Gotchas

- Names must match exactly; use `uuid` from `list_entities` when unsure.
- `screenshot` returns before the file exists — wait a frame (or poll the path).
- `quit` closes the editor; only send it when you are done.
- The server is single-threaded and polled per frame: one request per line,
  no pipelining of partial JSON.
