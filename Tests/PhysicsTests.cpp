#include <doctest/doctest.h>

#include "Slate/Physics/PhysicsWorld.h"

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
