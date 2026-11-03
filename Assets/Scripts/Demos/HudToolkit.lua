-- HUD Toolkit: a full screen-space UI built only from DrawRect and DrawText.
-- Shows panels, animated meters, a radar sweep, a scrolling event log and a
-- blinking cursor.

local time = 0.0
local cursorBlink = 0.0
local logEntries = {}
local logTimer = 0.0
local previousKeys = {}

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

local function pushLog(message)
    table.insert(logEntries, { text = message, age = 0.0 })
    if #logEntries > 6 then
        table.remove(logEntries, 1)
    end
end

local function drawPanel(x, y, width, height, alpha)
    DrawRect(x, y, width, height, 0.02, 0.03, 0.05, alpha)
    DrawRect(x, y, width, 2.0, 0.25, 0.45, 0.75, alpha)
end

local function drawMeter(x, y, width, label, value, r, g, b)
    DrawText(x, y, label, 13.0, 0.70, 0.75, 0.85, 1.0)
    DrawRect(x, y + 18.0, width, 10.0, 0.10, 0.12, 0.16, 0.9)
    DrawRect(x, y + 18.0, width * value, 10.0, r, g, b, 1.0)
    DrawRect(x + width * value - 2.0, y + 16.0, 4.0, 14.0, 1.0, 1.0, 1.0, 0.8)
end

local function drawRadar(x, y, size)
    DrawRect(x, y, size, size, 0.03, 0.05, 0.08, 0.8)
    DrawRect(x + size * 0.5 - 1.0, y, 2.0, size, 0.18, 0.30, 0.42, 0.8)
    DrawRect(x, y + size * 0.5 - 1.0, size, 2.0, 0.18, 0.30, 0.42, 0.8)

    local sweep = time * 1.8
    for index = 0, 8 do
        local angle = sweep + index * 0.08
        local radius = size * 0.42
        local px = x + size * 0.5 + math.cos(angle) * radius
        local py = y + size * 0.5 + math.sin(angle) * radius
        DrawRect(px - 1.5, py - 1.5, 3.0, 3.0, 0.35, 0.95, 0.65, 1.0 - index * 0.1)
    end

    for index = 1, 5 do
        local angle = index * 1.9
        local radius = size * (0.12 + (index % 3) * 0.1)
        local px = x + size * 0.5 + math.cos(angle) * radius
        local py = y + size * 0.5 + math.sin(angle) * radius
        DrawRect(px - 2.0, py - 2.0, 4.0, 4.0, 0.95, 0.45, 0.30, 0.9)
    end
end

function OnCreate(entity)
    pushLog("HUD toolkit initialised")
    Log("HudToolkit ready")
end

function OnUpdate(entity, deltaTime)
    time = time + deltaTime
    cursorBlink = cursorBlink + deltaTime

    if pressed("L") then
        pushLog("Event logged at t=" .. string.format("%.1f", time))
    end

    logTimer = logTimer + deltaTime
    if logTimer > 2.2 then
        logTimer = 0.0
        pushLog("Telemetry packet " .. math.floor(time * 10))
    end

    for _, entry in ipairs(logEntries) do
        entry.age = entry.age + deltaTime
    end

    local width = ScreenWidth()
    local height = ScreenHeight()

    -- Top bar.
    DrawRect(0.0, 0.0, width, 46.0, 0.02, 0.03, 0.06, 0.85)
    DrawText(20.0, 14.0, "SLATE HUD TOOLKIT", 20.0, 1.0, 1.0, 1.0, 1.0)
    DrawText(width - 150.0, 16.0, string.format("T+%06.1f", time), 15.0, 0.60, 0.85, 1.0, 1.0)

    -- Left stats panel.
    drawPanel(16.0, 62.0, 270.0, 236.0, 0.65)
    DrawText(30.0, 74.0, "SYSTEM STATUS", 15.0, 0.85, 0.90, 1.0, 1.0)

    local health = 0.62 + math.sin(time * 0.9) * 0.28
    local stamina = 0.5 + math.sin(time * 1.7 + 1.0) * 0.4
    local heat = 0.35 + math.sin(time * 2.3 + 2.0) * 0.3

    drawMeter(30.0, 100.0, 230.0, "HEALTH", health, 0.30, 0.90, 0.45)
    drawMeter(30.0, 138.0, 230.0, "STAMINA", stamina, 0.30, 0.65, 0.95)
    drawMeter(30.0, 176.0, 230.0, "HEAT", heat, 0.95, 0.55, 0.25)
    DrawText(30.0, 226.0, "INTEGRITY " .. string.format("%d%%", math.floor(health * 100)), 13.0, 0.60, 0.66, 0.76, 1.0)
    DrawText(30.0, 248.0, "PRESS L TO LOG AN EVENT", 12.0, 0.50, 0.55, 0.65, 1.0)

    -- Radar panel.
    drawPanel(width - 230.0, 62.0, 214.0, 214.0, 0.65)
    DrawText(width - 216.0, 74.0, "PROXIMITY", 15.0, 0.85, 0.90, 1.0, 1.0)
    drawRadar(width - 214.0, 96.0, 182.0)

    -- Event log.
    drawPanel(16.0, height - 178.0, 360.0, 158.0, 0.55)
    DrawText(30.0, height - 166.0, "EVENT LOG", 14.0, 0.85, 0.90, 1.0, 1.0)
    for index, entry in ipairs(logEntries) do
        local alpha = math.max(0.15, 1.0 - entry.age / 8.0)
        DrawText(30.0, height - 142.0 + (index - 1) * 20.0, "> " .. entry.text, 13.0, 0.65, 0.75, 0.88, alpha)
    end

    -- Crosshair and blinking cursor.
    local centreX = width * 0.5
    local centreY = height * 0.5
    DrawRect(centreX - 10.0, centreY - 1.0, 20.0, 2.0, 1.0, 1.0, 1.0, 0.75)
    DrawRect(centreX - 1.0, centreY - 10.0, 2.0, 20.0, 1.0, 1.0, 1.0, 0.75)

    if cursorBlink % 1.0 < 0.5 then
        DrawRect(width * 0.5 - 40.0, height - 40.0, 3.0, 18.0, 0.4, 0.9, 1.0, 0.9)
    end
end
