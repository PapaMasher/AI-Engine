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

TEST_CASE("Lua can attach physics bodies and colliders")
{
    const auto scriptPath = std::filesystem::temp_directory_path() / "slate_physics_script.lua";
    {
        std::ofstream stream(scriptPath);
        stream << "function OnCreate(entity)\n"
                  "  local crate = CreateEntity('Crate')\n"
                  "  crate:SetMesh('Models/Cube.glb')\n"
                  "  crate:SetPosition(0.0, 4.0, 0.0)\n"
                  "  crate:SetRigidBody(1, 2.0)\n"
                  "  crate:SetBoxCollider(0.5, 0.5, 0.5)\n"
                  "  Log('bodies=' .. PhysicsBodyCount())\n"
                  "end\n";
    }

    Scene scene("LuaPhysics");
    Entity entity = scene.CreateEntity("Spawner");
    entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath.string();

    PhysicsWorld physics;
    physics.Init();

    ScriptEngine scripts;
    scripts.Init(scene, &physics);
    scripts.Start();

    REQUIRE(scene.GetEntityCount() == 2);

    physics.Step(scene, 1.0 / 60.0);
    CHECK(physics.GetBodyCount() == 1);

    Entity crate;
    scene.GetRegistry().Each<TagComponent>([&](Entity candidate, TagComponent& tag)
    {
        if (tag.Name == "Crate")
            crate = candidate;
    });
    REQUIRE(crate.IsValid());
    CHECK(crate.HasComponent<RigidBodyComponent>());
    CHECK(crate.GetComponent<RigidBodyComponent>().Type == BodyType::Dynamic);
    REQUIRE(crate.HasComponent<BoxColliderComponent>());
    CHECK(crate.GetComponent<BoxColliderComponent>().HalfExtents.x == doctest::Approx(0.5f));

    scripts.Stop();
    scripts.Shutdown();
    physics.Shutdown();

    std::filesystem::remove(scriptPath);
}

TEST_CASE("PlaySound is safe without an audio device")
{
    const auto scriptPath = std::filesystem::temp_directory_path() / "slate_audio_script.lua";
    {
        std::ofstream stream(scriptPath);
        stream << "function OnCreate(entity)\n"
                  "  PlaySound('Audio/beep.wav', 0.5, false, false)\n"
                  "end\n";
    }

    Scene scene("LuaAudio");
    Entity entity = scene.CreateEntity("Speaker");
    entity.AddComponent<ScriptComponent>().ScriptPath = scriptPath.string();

    PhysicsWorld physics;
    physics.Init();

    ScriptEngine scripts;
    scripts.Init(scene, &physics, nullptr, nullptr);
    scripts.Start();
    CHECK(scripts.GetInstanceCount() == 1);

    scripts.Stop();
    scripts.Shutdown();
    physics.Shutdown();

    std::filesystem::remove(scriptPath);
}
