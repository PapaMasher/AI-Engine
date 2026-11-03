# Skill: author a Slate scene

Use when creating or editing `Assets/Scenes/*.json` or prefabs.

## Scene schema

```json
{
  "name": "MyScene",
  "environment": {
    "ambientColor": [1, 1, 1],
    "ambientIntensity": 1.0,
    "exposure": 1.0,
    "hdriPath": "HDRIs/studio_small_03_1k.hdr",
    "showSkybox": true
  },
  "entities": [
    {
      "uuid": "1000000000000001",
      "name": "Main Camera",
      "transform": {
        "translation": [0, 1.6, 4],
        "rotation": [-5, 0, 0],
        "scale": [1, 1, 1]
      },
      "camera": {
        "fieldOfView": 60,
        "nearClip": 0.1,
        "farClip": 500,
        "primary": true
      }
    }
  ]
}
```

Component keys: `camera`, `directionalLight`, `pointLight`, `meshRenderer`,
`rigidBody` (`type` 0 static / 1 dynamic / 2 kinematic), `boxCollider`,
`sphereCollider`, `script`, `audioSource`.

- Rotations are Euler degrees, applied YXZ.
- Model paths are relative to `Assets/` (`Models/DamagedHelmet.glb`).
- `uuid` is a 16-hex-digit string; keep it unique and stable.
- `meshRenderer.tint` multiplies the material base colour.

## Prefabs

`Scene::SavePrefab(entity, path)` writes a single entity plus its children.
`Scene::InstantiatePrefab(path, position)` spawns it with fresh UUIDs.

## Lighting

- Directional light `direction` is the direction light travels; the shader
  lights surfaces facing `-direction`.
- With an HDRI, ambient comes from the irradiance cubemap; without one the
  ambient term is black, so add a directional light or HDRI for test scenes.
- SSAO and shadows are on by default; disable `castShadows` on small props if
  the shadow map resolution is tight.

## Verify

Run the editor, take a screenshot through automation, and confirm the scene
reads correctly: sky above, ground below, objects upright.
