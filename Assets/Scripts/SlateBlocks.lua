-- Slate Blocks: a playable 3D Tetris written entirely against the Slate Lua
-- API. Attach to a "Game" entity; the script spawns and destroys every block
-- itself, so the scene only needs a camera, lights and a backdrop.
--
-- Controls: Left/Right or A/D move, Down or S soft drop, Up or W rotate,
-- Space hard drop, Enter restarts after game over.

local COLS = 10
local ROWS = 20
local ORIGIN_X = -4.5
local ORIGIN_Y = 0.0

local SHAPES = {
    I = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },
    O = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
    T = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } },
    S = { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } },
    Z = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } },
    J = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } },
    L = { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }
}

local COLORS = {
    I = { 0.20, 0.80, 1.00 },
    O = { 1.00, 0.85, 0.20 },
    T = { 0.70, 0.35, 1.00 },
    S = { 0.30, 1.00, 0.40 },
    Z = { 1.00, 0.30, 0.30 },
    J = { 0.25, 0.45, 1.00 },
    L = { 1.00, 0.55, 0.15 }
}

local KINDS = { "I", "O", "T", "S", "Z", "J", "L" }

local board = {}
local piece = nil
local dropTimer = 0.0
local dropInterval = 0.65
local moveTimer = 0.0
local moveInterval = 0.14
local score = 0
local lines = 0
local gameOver = false
local scoreBar = {}
local previousKeys = {}

local spawnPiece, lockPiece, clearLines, restart

local function cellPosition(col, row)
    return ORIGIN_X + col, ORIGIN_Y + row
end

local function resetBoard()
    board = {}
    for row = 0, ROWS - 1 do
        board[row] = {}
    end
end

local function canPlace(shape, col, row)
    for _, cell in ipairs(shape) do
        local x = col + cell[1]
        local y = row + cell[2]
        if x < 0 or x >= COLS or y < 0 then
            return false
        end
        if y < ROWS and board[y][x] then
            return false
        end
    end
    return true
end

local function rotated(shape)
    local out = {}
    for _, cell in ipairs(shape) do
        table.insert(out, { cell[2], -cell[1] })
    end

    local minX, minY = out[1][1], out[1][2]
    for _, cell in ipairs(out) do
        if cell[1] < minX then minX = cell[1] end
        if cell[2] < minY then minY = cell[2] end
    end
    for _, cell in ipairs(out) do
        cell[1] = cell[1] - minX
        cell[2] = cell[2] - minY
    end
    return out
end

local function syncPiece()
    for index, cell in ipairs(piece.shape) do
        local x, y = cellPosition(piece.col + cell[1], piece.row + cell[2])
        piece.entities[index]:SetPosition(x, y, 0.0)
    end
end

local function tryMove(dx, dy)
    if not piece then
        return false
    end

    local col = piece.col + dx
    local row = piece.row + dy
    if not canPlace(piece.shape, col, row) then
        return false
    end

    piece.col = col
    piece.row = row
    syncPiece()
    return true
end

local function rotatePiece()
    if not piece then
        return
    end

    local shape = rotated(piece.shape)
    if canPlace(shape, piece.col, piece.row) then
        piece.shape = shape
    elseif canPlace(shape, piece.col - 1, piece.row) then
        piece.col = piece.col - 1
        piece.shape = shape
    elseif canPlace(shape, piece.col + 1, piece.row) then
        piece.col = piece.col + 1
        piece.shape = shape
    else
        return
    end

    syncPiece()
end

local function updateScoreBar()
    local lit = math.floor(score / 100)
    for index = 1, #scoreBar do
        local entity = scoreBar[index]
        if entity:IsValid() then
            if index <= lit then
                entity:SetTint(0.3, 1.0, 0.4, 1.0)
            else
                entity:SetTint(0.12, 0.14, 0.18, 1.0)
            end
        end
    end
end

clearLines = function()
    local cleared = 0

    for row = 0, ROWS - 1 do
        local full = true
        for col = 0, COLS - 1 do
            if not board[row][col] then
                full = false
                break
            end
        end

        if full then
            for col = 0, COLS - 1 do
                local entity = board[row][col]
                if entity then
                    entity:Destroy()
                end
                board[row][col] = nil
            end
            cleared = cleared + 1
        end
    end

    if cleared == 0 then
        return 0
    end

    -- Drop everything above the cleared rows, updating both the grid and the
    -- visible entities.
    local write = 0
    for row = 0, ROWS - 1 do
        local hasCell = false
        for col = 0, COLS - 1 do
            if board[row][col] then
                hasCell = true
                break
            end
        end

        if hasCell then
            if write ~= row then
                for col = 0, COLS - 1 do
                    local entity = board[row][col]
                    board[write][col] = entity
                    board[row][col] = nil
                    if entity then
                        local x, y = cellPosition(col, write)
                        entity:SetPosition(x, y, 0.0)
                    end
                end
            end
            write = write + 1
        end
    end

    score = score + cleared * 100
    lines = lines + cleared
    updateScoreBar()
    Log("CLEARED " .. cleared .. " line(s), score=" .. score .. ", lines=" .. lines)
    return cleared
end

lockPiece = function()
    if not piece then
        return
    end

    for index, cell in ipairs(piece.shape) do
        local x = piece.col + cell[1]
        local y = piece.row + cell[2]
        if y >= 0 and y < ROWS and x >= 0 and x < COLS then
            board[y][x] = piece.entities[index]
        end
    end

    piece = nil
    Log("LOCK score=" .. score)
    clearLines()
    spawnPiece()
end

spawnPiece = function()
    local kind = KINDS[math.random(#KINDS)]
    local shape = {}
    for _, cell in ipairs(SHAPES[kind]) do
        table.insert(shape, { cell[1], cell[2] })
    end

    local col = math.floor(COLS / 2) - 2
    local row = ROWS - 2

    if not canPlace(shape, col, row) then
        gameOver = true
        Log("GAME OVER score=" .. score .. ", lines=" .. lines)
        return
    end

    local entities = {}
    for _, cell in ipairs(shape) do
        local entity = CreateEntity("Block")
        entity:SetMesh("Models/Cube.glb")
        entity:SetTint(COLORS[kind][1], COLORS[kind][2], COLORS[kind][3], 1.0)
        entity:SetScale(0.92, 0.92, 0.92)
        local x, y = cellPosition(col + cell[1], row + cell[2])
        entity:SetPosition(x, y, 0.0)
        table.insert(entities, entity)
    end

    piece = { kind = kind, shape = shape, col = col, row = row, entities = entities }
    dropTimer = 0.0
    Log("SPAWN " .. kind .. " score=" .. score)
end

restart = function()
    for row = 0, ROWS - 1 do
        for col = 0, COLS - 1 do
            local entity = board[row][col]
            if entity then
                entity:Destroy()
            end
            board[row][col] = nil
        end
    end

    if piece then
        for _, entity in ipairs(piece.entities) do
            if entity:IsValid() then
                entity:Destroy()
            end
        end
    end

    piece = nil
    score = 0
    lines = 0
    gameOver = false
    dropTimer = 0.0
    resetBoard()
    updateScoreBar()
    spawnPiece()
    Log("RESTART")
end

local function pressed(key)
    local down = KeyDown(key)
    local was = previousKeys[key] or false
    previousKeys[key] = down
    return down and not was
end

function OnCreate(entity)
    resetBoard()

    -- Ten-block score bar to the right of the well.
    scoreBar = {}
    for index = 1, 10 do
        local bar = CreateEntity("ScoreBar")
        bar:SetMesh("Models/Cube.glb")
        bar:SetTint(0.12, 0.14, 0.18, 1.0)
        bar:SetScale(0.5, 0.5, 0.5)
        bar:SetPosition(6.2, index - 1, 0.0)
        scoreBar[index] = bar
    end

    spawnPiece()
    Log("SlateBlocks ready: arrows/WASD move, Up rotates, Space drops, Enter restarts")
end

function OnUpdate(entity, deltaTime)
    if gameOver then
        if pressed("Enter") then
            restart()
        end
        return
    end

    if not piece then
        spawnPiece()
        return
    end

    if pressed("Left") or pressed("A") then
        tryMove(-1, 0)
        moveTimer = moveInterval
    elseif pressed("Right") or pressed("D") then
        tryMove(1, 0)
        moveTimer = moveInterval
    elseif (KeyDown("Left") or KeyDown("A")) and moveTimer <= 0.0 then
        tryMove(-1, 0)
        moveTimer = moveInterval
    elseif (KeyDown("Right") or KeyDown("D")) and moveTimer <= 0.0 then
        tryMove(1, 0)
        moveTimer = moveInterval
    end

    if pressed("Up") or pressed("W") then
        rotatePiece()
    end

    if pressed("Space") then
        while tryMove(0, -1) do end
        lockPiece()
        return
    end

    if KeyDown("Down") or KeyDown("S") then
        dropTimer = dropTimer + deltaTime * 8.0
    end

    moveTimer = moveTimer - deltaTime
    dropTimer = dropTimer + deltaTime

    if dropTimer >= dropInterval then
        dropTimer = 0.0
        if not tryMove(0, -1) then
            lockPiece()
        end
    end
end

function OnDestroy(entity)
    Log("SlateBlocks stopped, final score=" .. score)
end
