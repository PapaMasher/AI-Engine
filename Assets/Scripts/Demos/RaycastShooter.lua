-- Raycast Shooter: click to cast a ray from the camera. Hits receive an
-- impulse from the ray direction, spawn a spark and play a sound. Arrow keys
-- aim the camera.
--
-- Controls: mouse click fires, Left/Right yaw, Up/Down pitch.

local TARGETS = 14
local FIRE_COOLDOWN = 0.14
local RAY_LENGTH = 80.0

local targets = {}
local sparks = {}
local hits = 0
local cooldown = 0.0
local yaw = 0.0
local pitch = -6.0
local previousKeys = {}

local PALETTE = {
    { 0.95, 0.30, 0.28 },
    { 0.98, 0.72, 0.25 },
    { 0.40, 0.85, 0.55 },
    { 0.30, 0.65, 0.98 }
}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function buildTargets()
    for index = 1, TARGETS do
        local column = (index - 1) % 7
        local row = math.floor((index - 1) / 7)
        local x = (column - 3) * 1.6
        local y = 0.5 + row * 1.4
        local z = -3.0 - row * 2.5
        local color = PALETTE[(index % #PALETTE) + 1]

        local target = CreateEntity("Target")
        target:SetMesh("Models/Cube.glb")
        target:SetTint(color[1], color[2], color[3], 1.0)
        target:SetScale(0.9, 0.9, 0.9)
        target:SetPosition(x, y, z)
        target:SetRigidBody(1, 1.5)
        target:SetBoxCollider(0.45, 0.45, 0.45)

        table.insert(targets, { entity = target, home = { x, y, z } })
    end
end

local function spawnSpark(x, y, z)
    local spark = CreateEntity("Spark")
    spark:SetMesh("Models/Cube.glb")
    spark:SetTint(1.0, 0.85, 0.35, 1.0)
    spark:SetScale(0.22, 0.22, 0.22)
    spark:SetPosition(x, y, z)
    spark:SetVisible(true)
    table.insert(sparks, { entity = spark, life = 0.45 })
end

local function cameraForward()
    local yawRad = math.rad(yaw)
    local pitchRad = math.rad(pitch)
    local cosPitch = math.cos(pitchRad)
    return -math.sin(yawRad) * cosPitch, math.sin(pitchRad), -math.cos(yawRad) * cosPitch
end

local function fire()
    local camera = FindEntity("Main Camera")
    if not camera:IsValid() then
        return
    end

    local position = camera:Position()
    local fx, fy, fz = cameraForward()

    local hit = Raycast(position.x, position.y, position.z, fx, fy, fz, RAY_LENGTH)
    if not hit.hit then
        return
    end

    hits = hits + 1
    spawnSpark(hit.x, hit.y, hit.z)
    PlaySoundAt("Audio/move.wav", 0.9, false, hit.x, hit.y, hit.z)

    local target = hit.entity
    if target and target:IsValid() and target:HasRigidBody() then
        ApplyImpulse(target, fx * 22.0, fy * 22.0 + 5.0, fz * 22.0)
    end
end

local function updateSparks(deltaTime)
    for index = #sparks, 1, -1 do
        local spark = sparks[index]
        spark.life = spark.life - deltaTime

        if spark.life <= 0.0 or not spark.entity:IsValid() then
            if spark.entity:IsValid() then
                spark.entity:Destroy()
            end
            table.remove(sparks, index)
        else
            local scale = 0.22 * (spark.life / 0.45)
            spark.entity:SetScale(scale, scale, scale)
        end
    end
end

local function updateTargets()
    for _, target in ipairs(targets) do
        if target.entity:IsValid() then
            local position = target.entity:Position()
            if position.y < -3.0 then
                target.entity:SetPosition(target.home[1], target.home[2] + 2.0, target.home[3])
                SetVelocity(target.entity, 0.0, 0.0, 0.0)
            end
        end
    end
end

function OnCreate(entity)
    buildTargets()
    Log("RaycastShooter ready")
end

function OnUpdate(entity, deltaTime)
    cooldown = math.max(0.0, cooldown - deltaTime)

    if KeyDown("Left") then
        yaw = yaw + deltaTime * 60.0
    end
    if KeyDown("Right") then
        yaw = yaw - deltaTime * 60.0
    end
    if KeyDown("Up") then
        pitch = math.min(60.0, pitch + deltaTime * 45.0)
    end
    if KeyDown("Down") then
        pitch = math.max(-70.0, pitch - deltaTime * 45.0)
    end

    local camera = FindEntity("Main Camera")
    if camera:IsValid() then
        camera:SetRotation(pitch, yaw, 0.0)
    end

    if MouseDown("Left") and cooldown <= 0.0 then
        cooldown = FIRE_COOLDOWN
        fire()
    end

    updateSparks(deltaTime)
    updateTargets()

    -- Crosshair.
    local centreX = ScreenWidth() * 0.5
    local centreY = ScreenHeight() * 0.5
    DrawRect(centreX - 1.0, centreY - 12.0, 2.0, 24.0, 1.0, 1.0, 1.0, 0.9)
    DrawRect(centreX - 12.0, centreY - 1.0, 24.0, 2.0, 1.0, 1.0, 1.0, 0.9)
    DrawRect(centreX - 4.0, centreY - 4.0, 8.0, 8.0, 1.0, 0.4, 0.25, 1.0)

    local hudY = ScreenHeight() - 116.0
    DrawRect(16.0, hudY, 400.0, 92.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(30.0, hudY + 8.0, "RAYCAST SHOOTER", 22.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(30.0, hudY + 40.0, "HITS " .. hits .. "   TARGETS " .. TARGETS, 16.0, 0.72, 0.82, 1.0, 1.0)
    DrawText(30.0, hudY + 66.0, "CLICK FIRE · ARROWS AIM", 14.0, 0.55, 0.60, 0.70, 1.0)
end
