#include <doctest/doctest.h>

#include "Slate/Physics/PhysicsWorld.h"

#include <cmath>

using namespace Slate;

TEST_CASE("Physics world creates bodies for rigid body entities")
{
    PhysicsWorld physics;
    physics.Init();
    REQUIRE(physics.IsInitialized());

    Scene scene("Physics");

    Entity ground = scene.CreateEntity("Ground");
    ground.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(10.0f, 0.5f, 10.0f);

    Entity box = scene.CreateEntity("Box");
    box.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
    box.AddComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
    box.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);

    physics.Step(scene, 0.0);
    CHECK(physics.GetBodyCount() == 2);

    const float startY = box.GetComponent<TransformComponent>().Translation.y;

    for (int i = 0; i < 120; ++i)
        physics.Step(scene, 1.0 / 60.0);

    const float endY = box.GetComponent<TransformComponent>().Translation.y;
    CHECK(endY < startY);
    CHECK(endY > -1.0f); // should rest on the ground rather than fall through

    physics.Shutdown();
}

TEST_CASE("Capsule bodies collide with the ground")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("Capsule");

    Entity ground = scene.CreateEntity("Ground");
    ground.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(10.0f, 0.5f, 10.0f);

    Entity capsule = scene.CreateEntity("Capsule");
    capsule.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 6.0f, 0.0f);
    capsule.AddComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
    auto& collider = capsule.AddComponent<CapsuleColliderComponent>();
    collider.Radius = 0.4f;
    collider.HalfHeight = 0.6f;

    physics.Step(scene, 0.0);
    CHECK(physics.GetBodyCount() == 2);

    for (int i = 0; i < 180; ++i)
        physics.Step(scene, 1.0 / 60.0);

    // Capsule rests on the ground: half height + radius above the surface.
    const float restY = capsule.GetComponent<TransformComponent>().Translation.y;
    CHECK(restY > 0.4f);
    CHECK(restY < 2.0f);

    physics.Shutdown();
}

TEST_CASE("Physics raycast hits the ground")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("Raycast");

    Entity ground = scene.CreateEntity("Ground");
    ground.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(10.0f, 0.5f, 10.0f);

    physics.Step(scene, 0.0);

    glm::vec3 hitPoint(0.0f);
    Entity hitEntity;
    const bool hit = physics.Raycast(scene, glm::vec3(0.0f, 5.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f), 20.0f, hitPoint, hitEntity);

    REQUIRE(hit);
    CHECK(hitPoint.y == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(hitEntity.IsValid());

    physics.Shutdown();
}

TEST_CASE("Destroying an entity removes its physics body")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("Cleanup");

    Entity box = scene.CreateEntity("Box");
    box.GetComponent<TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
    box.AddComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
    box.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);

    physics.Step(scene, 0.0);
    REQUIRE(physics.GetBodyCount() == 1);

    scene.DestroyEntity(box);
    physics.Step(scene, 1.0 / 60.0);
    CHECK(physics.GetBodyCount() == 0);

    physics.Shutdown();
}

TEST_CASE("Kinematic bodies follow their transform")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("Kinematic");

    Entity platform = scene.CreateEntity("Platform");
    platform.GetComponent<TransformComponent>().Translation = glm::vec3(2.0f, 3.0f, -1.0f);
    platform.AddComponent<RigidBodyComponent>().Type = BodyType::Kinematic;
    platform.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(1.0f, 0.25f, 1.0f);

    physics.Step(scene, 0.0);
    REQUIRE(physics.GetBodyCount() == 1);

    platform.GetComponent<TransformComponent>().Translation = glm::vec3(4.0f, 6.0f, -2.0f);
    physics.Step(scene, 1.0 / 60.0);

    // Kinematic bodies are driven by the transform and never fall.
    CHECK(platform.GetComponent<TransformComponent>().Translation.y == doctest::Approx(6.0f));

    physics.Shutdown();
}

TEST_CASE("Stacked boxes settle without exploding")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("Stack");

    Entity ground = scene.CreateEntity("Ground");
    ground.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f);

    const float heights[3] = { 0.5f, 1.5f, 2.5f };
    Entity boxes[3];
    for (int index = 0; index < 3; ++index)
    {
        boxes[index] = scene.CreateEntity("Stacked Box");
        boxes[index].GetComponent<TransformComponent>().Translation =
            glm::vec3(0.0f, heights[index] + 4.0f, 0.0f);
        boxes[index].AddComponent<RigidBodyComponent>().Type = BodyType::Dynamic;
        boxes[index].AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);
    }

    physics.Step(scene, 0.0);
    REQUIRE(physics.GetBodyCount() == 4);

    for (int step = 0; step < 300; ++step)
        physics.Step(scene, 1.0 / 60.0);

    // The stack should stay upright and close to the ground rather than
    // jitter or launch itself.
    for (Entity& box : boxes)
    {
        const glm::vec3 position = box.GetComponent<TransformComponent>().Translation;
        CHECK(position.y > 0.4f);
        CHECK(position.y < 4.0f);
        CHECK(std::abs(position.x) < 1.0f);
        CHECK(std::abs(position.z) < 1.0f);
    }

    physics.Shutdown();
}

TEST_CASE("Raycasts report misses cleanly")
{
    PhysicsWorld physics;
    physics.Init();

    Scene scene("RayMiss");

    Entity ground = scene.CreateEntity("Ground");
    ground.AddComponent<RigidBodyComponent>().Type = BodyType::Static;
    ground.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(2.0f, 0.5f, 2.0f);

    physics.Step(scene, 0.0);

    glm::vec3 hitPoint(0.0f);
    Entity hitEntity;
    CHECK_FALSE(physics.Raycast(scene, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
        10.0f, hitPoint, hitEntity));

    physics.Shutdown();
}
