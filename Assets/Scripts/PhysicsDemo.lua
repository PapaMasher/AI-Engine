-- Physics Playground: builds a pyramid, a capsule and spheres at startup,
-- then lets you spawn boxes on demand. Demonstrates Box3D through the Lua API.
--
-- Controls: Space spawns a box, R clears spawned boxes.

local SPAWN_LIMIT = 60

local spawned = {}
local spawnCount = 0
local previousKeys = {}

local function makeBox(name, x, y, z, size, r, g, b)
    local entity = CreateEntity(name)
    entity:SetMesh("Models/Cube.glb")
    entity:SetTint(r, g, b, 1.0)
    entity:SetScale(size, size, size)
    entity:SetPosition(x, y, z)
    entity:SetRigidBody(1, size * size * size)
    entity:SetBoxCollider(size * 0.5, size * 0.5, size * 0.5)
    return entity
end

local function buildPyramid()
    local colors = {
        { 0.95, 0.35, 0.25 },
        { 0.98, 0.75, 0.20 },
        { 0.35, 0.85, 0.45 },
        { 0.25, 0.60, 0.95 },
        { 0.70, 0.40, 0.95 }
    }

    for row = 0, 4 do
        local count = 5 - row
        local startX = -(count - 1) * 0.5
        local color = colors[(row % #colors) + 1]

        for index = 0, count - 1 do
            makeBox("Pyramid Block", startX + index, 0.5 + row, 0.0, 1.0,
                color[1], color[2], color[3])
        end
    end
end

local function buildProps()
    -- Capsule: the mesh approximates the capsule shape.
    local capsule = CreateEntity("Capsule")
    capsule:SetMesh("Models/Cube.glb")
    capsule:SetTint(0.55, 0.35, 0.95, 1.0)
    capsule:SetScale(0.8, 2.4, 0.8)
    capsule:SetPosition(4.5, 3.0, 0.0)
    capsule:SetRigidBody(1, 2.0)
    capsule:SetCapsuleCollider(0.4, 0.8)

    -- Spheres (cube meshes, sphere colliders).
    local spherePositions = {
        { -4.5, 2.5, 1.0 },
        { -4.5, 4.5, -1.0 },
        { -4.5, 6.5, 2.0 }
    }

    for index, position in ipairs(spherePositions) do
        local sphere = CreateEntity("Sphere")
        sphere:SetMesh("Models/Cube.glb")
        sphere:SetTint(0.95, 0.55, 0.20, 1.0)
        sphere:SetScale(0.6, 0.6, 0.6)
        sphere:SetPosition(position[1], position[2], position[3])
        sphere:SetRigidBody(1, 0.8)
        sphere:SetSphereCollider(0.3)
    end
end

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function spawnBox()
    if #spawned >= SPAWN_LIMIT then
        local oldest = table.remove(spawned, 1)
        if oldest:IsValid() then
            oldest:Destroy()
        end
    end

    local x = math.random() * 4.0 - 2.0
    local z = math.random() * 4.0 - 2.0
    local size = 0.5 + math.random() * 0.6

    local box = makeBox("Spawned Box", x, 9.0, z, size,
        0.9, 0.3 + math.random() * 0.5, 0.3 + math.random() * 0.4)
    table.insert(spawned, box)
    spawnCount = spawnCount + 1
    Log("Spawned box " .. spawnCount)
end

local function clearSpawned()
    for _, entity in ipairs(spawned) do
        if entity:IsValid() then
            entity:Destroy()
        end
    end
    spawned = {}
    Log("Cleared spawned boxes")
end

function OnCreate(entity)
    buildPyramid()
    buildProps()
    Log("PhysicsPlayground ready")
end

function OnUpdate(entity, deltaTime)
    if pressed("Space") then
        spawnBox()
    end

    if pressed("R") then
        clearSpawned()
    end

    local hudY = ScreenHeight() - 110.0
    DrawRect(12.0, hudY, 360.0, 96.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(26.0, hudY + 8.0, "PHYSICS PLAYGROUND", 24.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(26.0, hudY + 42.0, "BODIES " .. PhysicsBodyCount(), 20.0, 0.72, 0.82, 1.0, 1.0)
    DrawText(26.0, hudY + 70.0, "SPACE SPAWN  R CLEAR", 16.0, 0.55, 0.60, 0.70, 1.0)
end
