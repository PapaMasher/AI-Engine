-- Prism Garden: a rotating spiral of glossy blocks lit by coloured point
-- lights. Built entirely from Lua at runtime.
--
-- Controls: Space toggles rotation.

local COUNT = 90
local cubes = {}
local spinning = true
local previousKeys = {}

local PALETTE = {
    { 0.95, 0.30, 0.45 },
    { 0.30, 0.75, 0.95 },
    { 0.55, 0.95, 0.45 },
    { 0.98, 0.75, 0.30 },
    { 0.72, 0.45, 0.98 }
}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function buildGarden()
    for index = 1, COUNT do
        local phase = index * 0.42
        local radius = 1.8 + index * 0.13
        local height = 0.5 + (index % 9) * 0.55
        local color = PALETTE[(index % #PALETTE) + 1]

        local cube = CreateEntity("Prism")
        cube:SetMesh("Models/Cube.glb")
        cube:SetTint(color[1], color[2], color[3], 1.0)
        cube:SetScale(0.55, height, 0.55)
        cube:SetPosition(math.cos(phase) * radius, height * 0.5, math.sin(phase) * radius)

        table.insert(cubes, {
            entity = cube,
            phase = phase,
            radius = radius,
            height = height
        })
    end
end

function OnCreate(entity)
    buildGarden()
    Log("PrismGarden ready: " .. COUNT .. " prisms")
end

function OnUpdate(entity, deltaTime)
    if pressed("Space") then
        spinning = not spinning
        Log("Spin " .. (spinning and "on" or "off"))
    end

    if spinning then
        local step = deltaTime * 0.35
        for _, cube in ipairs(cubes) do
            cube.phase = cube.phase + step
            local x = math.cos(cube.phase) * cube.radius
            local z = math.sin(cube.phase) * cube.radius
            if cube.entity:IsValid() then
                cube.entity:SetPosition(x, cube.height * 0.5, z)
                cube.entity:SetRotation(0.0, math.deg(cube.phase), 0.0)
            end
        end
    end

    local hudY = ScreenHeight() - 84.0
    DrawRect(12.0, hudY, 300.0, 72.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(26.0, hudY + 8.0, "PRISM GARDEN", 24.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(26.0, hudY + 42.0, "SPACE TOGGLE SPIN", 16.0, 0.62, 0.68, 0.78, 1.0)
end
