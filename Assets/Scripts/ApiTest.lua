-- Exercises the entire Slate Lua API. Logs "ApiTest PASS" when every binding
-- behaves as documented; any failure raises a Lua error that the engine logs.

local finished = false

function OnCreate(entity)
    Log("ApiTest: OnCreate")

    entity:SetName("ApiTester")
    assert(entity:Name() == "ApiTester", "Name/SetName failed")

    entity:SetPosition(1.0, 2.0, 3.0)
    local position = entity:Position()
    assert(math.abs(position.x - 1.0) < 1e-4, "SetPosition x failed")
    assert(math.abs(position.y - 2.0) < 1e-4, "SetPosition y failed")
    assert(math.abs(position.z - 3.0) < 1e-4, "SetPosition z failed")

    entity:SetRotation(10.0, 20.0, 30.0)
    local rotation = entity:Rotation()
    assert(math.abs(rotation.x - 10.0) < 1e-4, "SetRotation x failed")
    assert(math.abs(rotation.y - 20.0) < 1e-4, "SetRotation y failed")

    entity:SetScale(2.0, 2.0, 2.0)
    local scale = entity:Scale()
    assert(math.abs(scale.x - 2.0) < 1e-4, "SetScale failed")

    entity:Translate(1.0, 0.0, 0.0)
    assert(math.abs(entity:Position().x - 2.0) < 1e-4, "Translate failed")

    entity:Rotate(0.0, 5.0, 0.0)
    assert(math.abs(entity:Rotation().y - 25.0) < 1e-4, "Rotate failed")

    local spawned = CreateEntity("ApiSpawn")
    assert(spawned:IsValid(), "CreateEntity failed")
    spawned:SetPosition(0.0, 1.0, 0.0)

    local found = FindEntity("ApiSpawn")
    assert(found:IsValid(), "FindEntity failed")
    assert(found:Name() == "ApiSpawn", "FindEntity returned the wrong entity")

    local missing = FindEntity("DoesNotExist")
    assert(not missing:IsValid(), "FindEntity should fail for unknown names")

    local prefab = SpawnPrefab("Prefabs/TestPrefab.json", 3.0, 0.0, 0.0)
    assert(prefab:IsValid(), "SpawnPrefab failed")
    assert(prefab:Name() == "Turret", "SpawnPrefab name mismatch")

    local doomed = CreateEntity("ToDestroy")
    DestroyEntity(doomed)
    assert(not doomed:IsValid(), "DestroyEntity failed")

    local doomed2 = CreateEntity("ToDestroy2")
    doomed2:Destroy()
    assert(not doomed2:IsValid(), "Entity:Destroy failed")

    local meshTarget = CreateEntity("MeshTarget")
    meshTarget:SetMesh("Models/BoxTextured.glb")
    meshTarget:SetTint(0.2, 0.9, 0.4, 1.0)
    local tint = meshTarget:Tint()
    assert(math.abs(tint.g - 0.9) < 1e-4, "SetTint/Tint failed")
    meshTarget:SetVisible(false)

    assert(type(KeyDown("Space")) == "boolean", "KeyDown failed")
    assert(type(MouseDown("Left")) == "boolean", "MouseDown failed")
end

function OnUpdate(entity, deltaTime)
    if finished then
        return
    end
    finished = true

    -- Physics runs before scripts, so bodies exist by the first update.
    local hit = Raycast(0.0, 5.0, 0.0, 0.0, -1.0, 0.0, 20.0)
    assert(hit.hit == true, "Raycast missed the ground")
    assert(math.abs(hit.y - 0.05) < 0.2, "Raycast hit unexpected height: " .. tostring(hit.y))
    assert(hit.entity:IsValid(), "Raycast returned an invalid entity")

    Log("ApiTest PASS")
end

function OnDestroy(entity)
    Log("ApiTest: OnDestroy")
end
