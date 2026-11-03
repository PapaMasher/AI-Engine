-- Spins the entity around its Y axis. Demonstrates the Lua scripting API.

local speedDegreesPerSecond = 45.0

function OnCreate(entity)
    Log("Spinner started on " .. entity:Name())
end

function OnUpdate(entity, deltaTime)
    entity:Rotate(0.0, speedDegreesPerSecond * deltaTime, 0.0)
end

function OnDestroy(entity)
    Log("Spinner stopped on " .. entity:Name())
end
