#include <doctest/doctest.h>

#include "Slate/Scene/Scene.h"
#include "Slate/Core/FileSystem.h"

#include <filesystem>

using namespace Slate;

TEST_CASE("Scene creates entities with ids and transforms")
{
    Scene scene("Test");

    Entity entity = scene.CreateEntity("Player");
    REQUIRE(entity.IsValid());
    CHECK(entity.HasComponent<IDComponent>());
    CHECK(entity.HasComponent<TagComponent>());
    CHECK(entity.HasComponent<TransformComponent>());
    CHECK(entity.GetComponent<TagComponent>().Name == "Player");
    CHECK(scene.GetEntityCount() == 1);
}

TEST_CASE("Scene serialization round-trips entities and components")
{
    const auto path = std::filesystem::temp_directory_path() / "slate_scene_roundtrip.json";

    Scene scene("RoundTrip");
    scene.GetEnvironment().HDRIPath = "HDRIs/studio_small_03_1k.hdr";

    Entity camera = scene.CreateEntity("Camera");
    camera.AddComponent<CameraComponent>();

    Entity mesh = scene.CreateEntity("Mesh");
    auto& renderer = mesh.AddComponent<MeshRendererComponent>();
    renderer.ModelPath = "Models/DamagedHelmet.glb";
    mesh.GetComponent<TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);

    Entity physics = scene.CreateEntity("Physics");
    physics.AddComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
    physics.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f, 1.0f, 2.0f);

    const UUID meshId = scene.GetEntityUUID(mesh);
    REQUIRE(scene.Save(path.string()));

    Scene loaded;
    REQUIRE(loaded.Load(path.string()));

    CHECK(loaded.GetName() == "RoundTrip");
    CHECK(loaded.GetEntityCount() == 3);
    CHECK(loaded.GetEnvironment().HDRIPath == "HDRIs/studio_small_03_1k.hdr");

    Entity loadedMesh = loaded.FindEntityByUUID(meshId);
    REQUIRE(loadedMesh.IsValid());
    CHECK(loadedMesh.GetComponent<TagComponent>().Name == "Mesh");
    CHECK(loadedMesh.GetComponent<MeshRendererComponent>().ModelPath == "Models/DamagedHelmet.glb");
    CHECK(loadedMesh.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));

    std::filesystem::remove(path);
}

TEST_CASE("Scene serializes to memory and restores")
{
    Scene scene("Memory");
    Entity entity = scene.CreateEntity("Thing");
    entity.GetComponent<TransformComponent>().Translation = glm::vec3(4.0f, 5.0f, 6.0f);

    const std::string snapshot = scene.Serialize();
    REQUIRE_FALSE(snapshot.empty());

    scene.CreateEntity("Spam");
    REQUIRE(scene.GetEntityCount() == 2);

    REQUIRE(scene.Deserialize(snapshot, "snapshot"));
    CHECK(scene.GetEntityCount() == 1);
    CHECK(scene.GetName() == "Memory");
}

TEST_CASE("Scene hierarchy propagates world transforms")
{
    Scene scene("Hierarchy");

    Entity parent = scene.CreateEntity("Parent");
    parent.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 2.0f, 0.0f);

    Entity child = scene.CreateEntity("Child");
    child.GetComponent<TransformComponent>().Translation = glm::vec3(1.0f, 0.0f, 0.0f);

    scene.SetParent(child, parent);
    scene.UpdateTransforms();

    CHECK(child.GetComponent<TransformComponent>().WorldMatrix[3] == glm::vec4(1.0f, 2.0f, 0.0f, 1.0f));
}

TEST_CASE("Scene rejects cyclic parenting")
{
    Scene scene("Cycles");

    Entity a = scene.CreateEntity("A");
    Entity b = scene.CreateEntity("B");

    scene.SetParent(b, a);
    scene.SetParent(a, b); // must be ignored

    CHECK_FALSE(a.HasComponent<ParentComponent>());
    CHECK(b.HasComponent<ParentComponent>());
}

TEST_CASE("Prefabs save and instantiate with children")
{
    const auto path = std::filesystem::temp_directory_path() / "slate_prefab.json";

    Scene scene("Prefabs");
    Entity root = scene.CreateEntity("Turret");
    auto& renderer = root.AddComponent<MeshRendererComponent>();
    renderer.ModelPath = "Models/BoxTextured.glb";

    Entity child = scene.CreateEntity("Barrel");
    scene.SetParent(child, root);

    REQUIRE(scene.SavePrefab(root, path.string()));

    Entity instance = scene.InstantiatePrefab(path.string(), glm::vec3(3.0f, 0.0f, 0.0f));
    REQUIRE(instance.IsValid());
    CHECK(instance.GetComponent<TransformComponent>().Translation == glm::vec3(3.0f, 0.0f, 0.0f));

    std::filesystem::remove(path);
}

TEST_CASE("Scene camera data uses the primary camera")
{
    Scene scene("Camera");

    Entity camera = scene.CreateEntity("Camera");
    auto& component = camera.AddComponent<CameraComponent>();
    component.FieldOfView = 75.0f;
    camera.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 1.0f, 5.0f);
    scene.UpdateTransforms();

    const CameraData data = scene.GetCameraData(16.0f / 9.0f);
    CHECK(data.Position == glm::vec3(0.0f, 1.0f, 5.0f));
    CHECK(data.ViewProjection != glm::mat4(0.0f));
}
