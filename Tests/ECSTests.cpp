#include <doctest/doctest.h>

#include "Slate/Scene/ECS.h"
#include "Slate/Scene/Components.h"

using namespace Slate;

namespace {

struct Position
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct Velocity
{
    float X = 0.0f;
    float Y = 0.0f;
};

} // namespace

TEST_CASE("Entities are created valid and destroyed invalidated")
{
    Registry registry;

    Entity entity = registry.CreateEntity();
    REQUIRE(entity.IsValid());
    CHECK(entity.GetID() != NullEntity);

    registry.DestroyEntity(entity);
    CHECK_FALSE(entity.IsValid());
}

TEST_CASE("Entity ids are reused with a new generation")
{
    Registry registry;

    Entity first = registry.CreateEntity();
    const EntityID firstId = first.GetID();
    const uint32_t firstGeneration = first.GetGeneration();

    registry.DestroyEntity(first);

    Entity second = registry.CreateEntity();
    CHECK(second.GetID() == firstId);
    CHECK(second.GetGeneration() != firstGeneration);
    CHECK(second.IsValid());
    CHECK_FALSE(first.IsValid());
}

TEST_CASE("Components can be added, fetched and removed")
{
    Registry registry;
    Entity entity = registry.CreateEntity();

    CHECK_FALSE(entity.HasComponent<Position>());

    entity.AddComponent<Position>(1.0f, 2.0f);
    REQUIRE(entity.HasComponent<Position>());
    CHECK(entity.GetComponent<Position>().X == 1.0f);
    CHECK(entity.GetComponent<Position>().Y == 2.0f);

    entity.RemoveComponent<Position>();
    CHECK_FALSE(entity.HasComponent<Position>());
}

TEST_CASE("Each visits only entities with all requested components")
{
    Registry registry;

    Entity both = registry.CreateEntity();
    both.AddComponent<Position>(1.0f, 1.0f);
    both.AddComponent<Velocity>(2.0f, 2.0f);

    Entity onlyPosition = registry.CreateEntity();
    onlyPosition.AddComponent<Position>(3.0f, 3.0f);

    int visited = 0;
    registry.Each<Position, Velocity>([&](Entity entity, Position& position, Velocity& velocity)
    {
        (void)entity;
        ++visited;
        position.X += velocity.X;
    });

    CHECK(visited == 1);
    CHECK(both.GetComponent<Position>().X == 3.0f);
    CHECK(onlyPosition.GetComponent<Position>().X == 3.0f);
}

TEST_CASE("Destroying an entity removes all of its components")
{
    Registry registry;
    Entity entity = registry.CreateEntity();
    entity.AddComponent<Position>(5.0f, 5.0f);
    entity.AddComponent<Velocity>(1.0f, 1.0f);

    registry.DestroyEntity(entity);

    CHECK(registry.GetPool<Position>().Size() == 0);
    CHECK(registry.GetPool<Velocity>().Size() == 0);
    CHECK(registry.GetEntityCount() == 0);
}

TEST_CASE("Registry::Clear resets all state")
{
    Registry registry;
    Entity entity = registry.CreateEntity();
    entity.AddComponent<Position>(1.0f, 1.0f);

    registry.Clear();

    CHECK(registry.GetEntityCount() == 0);
    CHECK(registry.GetPool<Position>().Size() == 0);

    Entity fresh = registry.CreateEntity();
    CHECK(fresh.IsValid());
}
