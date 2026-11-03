-- Physics Stress Test: builds a 9x8 wall of boxes plus capsules and spheres,
-- then lets you drop waves of boxes on top. Shows Box3D's multithreaded
-- solver, box/sphere/capsule colliders, raycasts and per-body impulses.
--
-- Controls: Space drops a wave, R rebuilds, G toggles gravity.

local WALL_COLUMNS = 9
local WALL_ROWS = 8
local MAX_BODIES = 260

local wall = {}
local props = {}
local spawned = {}
local raycastHits = 0
local raycastTimer = 0.0
local gravityOn = true
local previousKeys = {}

local PALETTE = {
    { 0.95, 0.35, 0.28 },
    { 0.98, 0.72, 0.25 },
    { 0.40, 0.85, 0.50 },
    { 0.28, 0.62, 0.98 },
    { 0.72, 0.45, 0.95 }
}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function makeBox(name, x, y, z, size, color)
    local box = CreateEntity(name)
    box:SetMesh("Models/Cube.glb")
    box:SetTint(color[1], color[2], color[3], 1.0)
    box:SetScale(size, size, size)
    box:SetPosition(x, y, z)
    box:SetRigidBody(1, size * size * size * 2.0)
    box:SetBoxCollider(size * 0.5, size * 0.5, size * 0.5)
    return box
end

local function buildWall()
    for row = 0, WALL_ROWS - 1 do
        for column = 0, WALL_COLUMNS - 1 do
            local x = (column - (WALL_COLUMNS - 1) * 0.5) * 1.08
            local y = 0.5 + row * 1.03
            local color = PALETTE[(row % #PALETTE) + 1]
            table.insert(wall, makeBox("Wall Block", x, y, 0.0, 1.0, color))
        end
    end
end

local function buildProps()
    for index = 1, 10 do
        local capsule = CreateEntity("Capsule")
        capsule:SetMesh("Models/Cube.glb")
        capsule:SetTint(0.6, 0.35, 0.95, 1.0)
        capsule:SetScale(0.8, 2.4, 0.8)
        capsule:SetPosition(-7.0 + index * 1.1, 9.0 + index * 0.6, 3.0)
        capsule:SetRigidBody(1, 2.0)
        capsule:SetCapsuleCollider(0.4, 0.8)
        table.insert(props, capsule)
    end

    for index = 1, 10 do
        local sphere = CreateEntity("Sphere")
        sphere:SetMesh("Models/Cube.glb")
        sphere:SetTint(0.95, 0.55, 0.2, 1.0)
        sphere:SetScale(0.7, 0.7, 0.7)
        sphere:SetPosition(7.0 - index * 1.0, 10.0 + index * 0.5, 3.0)
        sphere:SetRigidBody(1, 1.2)
        sphere:SetSphereCollider(0.35)
        table.insert(props, sphere)
    end
end

local function destroyAll(list)
    for _, entity in ipairs(list) do
        if entity:IsValid() then
            entity:Destroy()
        end
    end
    return {}
end

local function rebuild()
    wall = destroyAll(wall)
    props = destroyAll(props)
    spawned = destroyAll(spawned)
    buildWall()
    buildProps()
    Log("PhysicsStress rebuilt")
end

local function dropWave()
    if #spawned >= MAX_BODIES then
        local oldest = table.remove(spawned, 1)
        if oldest:IsValid() then
            oldest:Destroy()
        end
    end

    for index = 1, 24 do
        local x = math.random() * 12.0 - 6.0
        local z = math.random() * 6.0 - 3.0
        local size = 0.5 + math.random() * 0.5
        local color = PALETTE[math.random(#PALETTE)]
        table.insert(spawned, makeBox("Dropped Box", x, 15.0 + index * 0.4, z, size, color))
    end

    PlaySound("Audio/lock.wav", 0.6, false, false)
    Log("Dropped a wave, bodies=" .. PhysicsBodyCount())
end

local function updateRaycast(deltaTime)
    raycastTimer = raycastTimer + deltaTime
    if raycastTimer < 0.6 then
        return
    end

    raycastTimer = 0.0
    local hit = Raycast(0.0, 14.0, 0.0, 0.0, -1.0, 0.0, 20.0)
    if hit.hit then
        raycastHits = raycastHits + 1
    end
end

function OnCreate(entity)
    buildWall()
    buildProps()
    Log("PhysicsStress ready")
end

function OnUpdate(entity, deltaTime)
    if pressed("Space") then
        dropWave()
    end

    if pressed("R") then
        rebuild()
    end

    if pressed("G") then
        gravityOn = not gravityOn
        SetGravity(0.0, gravityOn and -9.81 or 0.0, 0.0)
        Log("Gravity " .. (gravityOn and "on" or "off"))
    end

    updateRaycast(deltaTime)

    local hudY = ScreenHeight() - 140.0
    DrawRect(16.0, hudY, 420.0, 116.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(30.0, hudY + 8.0, "PHYSICS STRESS TEST", 22.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(30.0, hudY + 40.0, "BODIES " .. PhysicsBodyCount() .. "   RAYCAST HITS " .. raycastHits, 16.0, 0.68, 0.80, 1.0, 1.0)
    DrawText(30.0, hudY + 64.0, "SPACE DROP WAVE · R REBUILD · G GRAVITY " .. (gravityOn and "ON" or "OFF"), 14.0, 0.55, 0.60, 0.70, 1.0)
    DrawText(30.0, hudY + 88.0, "BOX3D MULTITHREADED SOLVER · BOX / SPHERE / CAPSULE", 13.0, 0.45, 0.50, 0.60, 1.0)
end
