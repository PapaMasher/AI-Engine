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

TEST_CASE("Lua scripts can assign meshes, tints and visibility")
{
    const auto scriptPath = std::filesystem::temp_directory_path() / "slate_mesh_script.lua";
    {
        std::ofstream stream(scriptPath);
        stream << "function OnCreate(entity)\n"
                  "  entity:SetMesh('Models/BoxTextured.glb')\n"
                  "  entity:SetTint(0.1, 0.2, 0.3, 1.0)\n"
                  "  entity:SetVisible(false)\n"
                  "end\n";
    }

    Scene scene("MeshScript");
    Entity entity = scene.CreateEntity("MeshTarget");
    entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath.string();

    PhysicsWorld physics;
    physics.Init();

    ScriptEngine scripts;
    scripts.Init(scene, &physics);
    scripts.Start();

    REQUIRE(entity.HasComponent<MeshRendererComponent>());
    CHECK(entity.GetComponent<MeshRendererComponent>().ModelPath == "Models/BoxTextured.glb");
    CHECK(entity.GetComponent<MeshRendererComponent>().Tint.g == doctest::Approx(0.2f));
    CHECK_FALSE(entity.GetComponent<MeshRendererComponent>().Visible);

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
