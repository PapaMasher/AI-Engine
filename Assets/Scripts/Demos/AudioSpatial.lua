-- Spatial Audio: three looping emitters orbit the listener. The listener is
-- the primary camera, so panning and distance falloff follow the camera.
--
-- Controls: Space pauses the orbit.

local emitters = {}
local angle = 0.0
local orbiting = true
local previousKeys = {}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

function OnCreate(entity)
    local configs = {
        { name = "Emitter Low", radius = 3.2, height = 1.0, phase = 0.0, speed = 0.55, color = { 0.72, 0.45, 0.95 } },
        { name = "Emitter Mid", radius = 4.4, height = 1.6, phase = 2.1, speed = -0.4, color = { 0.30, 0.75, 0.95 } },
        { name = "Emitter High", radius = 2.6, height = 2.2, phase = 4.2, speed = 0.75, color = { 0.95, 0.72, 0.30 } }
    }

    for _, config in ipairs(configs) do
        local emitter = FindEntity(config.name)
        if emitter:IsValid() then
            table.insert(emitters, {
                entity = emitter,
                radius = config.radius,
                height = config.height,
                phase = config.phase,
                speed = config.speed,
                color = config.color
            })
        end
    end

    Log("AudioSpatial ready: " .. #emitters .. " emitters")
end

function OnUpdate(entity, deltaTime)
    if pressed("Space") then
        orbiting = not orbiting
        Log("Orbit " .. (orbiting and "on" or "off"))
    end

    if orbiting then
        angle = angle + deltaTime
    end

    for index, emitter in ipairs(emitters) do
        if emitter.entity:IsValid() then
            local a = emitter.phase + angle * emitter.speed
            local x = math.cos(a) * emitter.radius
            local z = math.sin(a) * emitter.radius
            local y = emitter.height + math.sin(angle * 1.7 + index) * 0.25
            emitter.entity:SetPosition(x, y, z)

            local pulse = 0.55 + math.sin(angle * 2.0 + index * 1.3) * 0.08
            emitter.entity:SetScale(pulse, pulse, pulse)
        end
    end

    local hudY = ScreenHeight() - 140.0
    DrawRect(16.0, hudY, 430.0, 116.0, 0.02, 0.03, 0.05, 0.55)
    DrawText(30.0, hudY + 8.0, "SPATIAL AUDIO", 22.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(30.0, hudY + 40.0, "3 ORBITING EMITTERS · LISTENER = MAIN CAMERA", 15.0, 0.68, 0.80, 1.0, 1.0)
    DrawText(30.0, hudY + 64.0, "LOW 220Hz · MID 330Hz · HIGH 440Hz", 14.0, 0.55, 0.62, 0.72, 1.0)
    DrawText(30.0, hudY + 88.0, "SPACE: PAUSE ORBIT", 13.0, 0.50, 0.55, 0.65, 1.0)
end
