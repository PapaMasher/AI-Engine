#include <doctest/doctest.h>

#include "Slate/Scripting/ScriptEngine.h"
#include "Slate/Physics/PhysicsWorld.h"
#include "Slate/Scene/Scene.h"
#include "Slate/Core/FileSystem.h"

#include <filesystem>
#include <fstream>

using namespace Slate;

TEST_CASE("Lua scripts run OnCreate and OnUpdate")
{
    const auto scriptPath = std::filesystem::temp_directory_path() / "slate_test_script.lua";
    {
        std::ofstream stream(scriptPath);
        stream << "created = false\n"
                  "function OnCreate(entity)\n"
                  "  created = true\n"
                  "  entity:SetPosition(1.0, 2.0, 3.0)\n"
                  "end\n"
                  "function OnUpdate(entity, dt)\n"
                  "  entity:Rotate(0.0, 90.0 * dt, 0.0)\n"
                  "end\n";
    }

    Scene scene("Scripting");
    Entity entity = scene.CreateEntity("Scripted");
    entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath.string();

    PhysicsWorld physics;
    physics.Init();

    ScriptEngine scripts;
    scripts.Init(scene, &physics);
    scripts.Start();

    CHECK(scripts.GetInstanceCount() == 1);
    CHECK(entity.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));

    scripts.Update(1.0);
    CHECK(entity.GetComponent<TransformComponent>().Rotation.y == doctest::Approx(90.0f));

    scripts.Stop();
    scripts.Shutdown();
    physics.Shutdown();

    std::filesystem::remove(scriptPath);
}

TEST_CASE("Lua scripts can create entities")
{
    const auto scriptPath = std::filesystem::temp_directory_path() / "slate_spawn_script.lua";
    {
        std::ofstream stream(scriptPath);
        stream << "function OnCreate(entity)\n"
                  "  local spawned = CreateEntity('Spawned')\n"
                  "  spawned:SetPosition(0.0, 1.0, 0.0)\n"
                  "end\n";
    }

    Scene scene("Spawning");
    Entity entity = scene.CreateEntity("Spawner");
    entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath.string();

    PhysicsWorld physics;
    physics.Init();

    ScriptEngine scripts;
    scripts.Init(scene, &physics);
    scripts.Start();

    CHECK(scene.GetEntityCount() == 2);

    scripts.Stop();
    scripts.Shutdown();
    physics.Shutdown();

    std::filesystem::remove(scriptPath);
}
