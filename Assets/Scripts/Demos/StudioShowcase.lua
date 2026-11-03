-- Studio Showcase: a slow cinematic orbit around the helmet, surrounded by a
-- ring of tinted pillars lit by warm, cool and rim point lights. Demonstrates
-- PBR + IBL, soft shadows, SSAO, bloom and the HUD renderer.
--
-- Controls: Space toggles bloom.

local RADIUS = 8.0
local HEIGHT = 2.9
local TARGET_HEIGHT = 1.1
local ORBIT_SPEED = 0.18

local PALETTE = {
    { 0.95, 0.35, 0.30 },
    { 0.98, 0.72, 0.28 },
    { 0.45, 0.85, 0.55 },
    { 0.30, 0.65, 0.95 },
    { 0.72, 0.45, 0.95 },
    { 0.95, 0.55, 0.75 }
}

local angle = 0.0
local bloomOn = true
local previousKeys = {}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function buildPillars()
    for index = 1, 16 do
        local a = (index - 1) / 16.0 * math.pi * 2.0
        local radius = 4.6
        local height = 0.5 + (index % 6) * 0.38
        local color = PALETTE[(index % #PALETTE) + 1]

        local pillar = CreateEntity("Pillar")
        pillar:SetMesh("Models/Cube.glb")
        pillar:SetTint(color[1], color[2], color[3], 1.0)
        pillar:SetScale(0.5, height, 0.5)
        pillar:SetPosition(math.cos(a) * radius, height * 0.5, math.sin(a) * radius)
    end
end

function OnCreate(entity)
    buildPillars()
    Log("StudioShowcase ready")
end

function OnUpdate(entity, deltaTime)
    angle = angle + deltaTime * ORBIT_SPEED

    local camera = FindEntity("Main Camera")
    if camera:IsValid() then
        camera:SetPosition(math.sin(angle) * RADIUS, HEIGHT, math.cos(angle) * RADIUS)

        local pitch = -math.deg(math.atan(HEIGHT - TARGET_HEIGHT, RADIUS))
        camera:SetRotation(pitch, math.deg(angle), 0.0)
    end

    -- The rim light drifts behind the scene to keep the silhouette alive.
    local rim = FindEntity("Rim Light")
    if rim:IsValid() then
        rim:SetPosition(math.sin(angle * 1.7) * 2.4, 3.4 + math.sin(angle * 1.3) * 0.5, -4.2)
    end

    if pressed("Space") then
        bloomOn = not bloomOn
        SetBloom(bloomOn, 1.6, 0.06)
        Log("Bloom " .. (bloomOn and "on" or "off"))
    end

    local hudY = ScreenHeight() - 128.0
    DrawRect(16.0, hudY, 470.0, 104.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(30.0, hudY + 8.0, "SLATE — STUDIO SHOWCASE", 22.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(30.0, hudY + 40.0, "PBR · IBL · SOFT SHADOWS · SSAO · BLOOM · FXAA", 15.0, 0.68, 0.78, 0.92, 1.0)
    DrawText(30.0, hudY + 68.0, "SPACE: BLOOM " .. (bloomOn and "ON" or "OFF"), 14.0, 0.55, 0.60, 0.70, 1.0)
end
