#include "Slate/Physics/PhysicsWorld.h"
#include "Slate/Core/Log.h"

#include <box3d/box3d.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace Slate {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// TransformComponent rotations are YXZ Euler degrees.
glm::quat EulerToQuat(const glm::vec3& degrees)
{
    const glm::vec3 radians = glm::radians(degrees);
    const glm::quat qx = glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat qy = glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat qz = glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return qy * qx * qz;
}

glm::vec3 QuatToEulerDegrees(const glm::quat& q)
{
    const glm::mat3 matrix = glm::mat3_cast(q);
    const float x = std::asin(glm::clamp(-matrix[2][1], -1.0f, 1.0f));
    const float y = std::atan2(matrix[2][0], matrix[2][2]);
    const float z = std::atan2(matrix[0][1], matrix[1][1]);
    return glm::degrees(glm::vec3(x, y, z));
}

b3Vec3 ToB3(const glm::vec3& value)
{
    return b3Vec3 { value.x, value.y, value.z };
}

b3Pos ToB3Pos(const glm::vec3& value)
{
    return b3Pos { value.x, value.y, value.z };
}

b3Quat ToB3(const glm::quat& value)
{
    return b3Quat { b3Vec3 { value.x, value.y, value.z }, value.w };
}

glm::vec3 FromB3(const b3Vec3& value)
{
    return glm::vec3(value.x, value.y, value.z);
}

glm::quat FromB3(const b3Quat& value)
{
    return glm::quat(value.s, value.v.x, value.v.y, value.v.z);
}

uint64_t BodyKey(const b3BodyId& id)
{
    return (static_cast<uint64_t>(id.generation) << 32) | static_cast<uint32_t>(id.index1);
}

float ShapeVolume(const Entity& entity)
{
    if (entity.HasComponent<BoxColliderComponent>())
    {
        const glm::vec3 half = entity.GetComponent<BoxColliderComponent>().HalfExtents;
        return 8.0f * std::max(half.x, 0.001f) * std::max(half.y, 0.001f)
            * std::max(half.z, 0.001f);
    }

    if (entity.HasComponent<SphereColliderComponent>())
    {
        const float radius = std::max(entity.GetComponent<SphereColliderComponent>().Radius, 0.001f);
        return (4.0f / 3.0f) * kPi * radius * radius * radius;
    }

    if (entity.HasComponent<CapsuleColliderComponent>())
    {
        const auto& capsule = entity.GetComponent<CapsuleColliderComponent>();
        const float radius = std::max(capsule.Radius, 0.001f);
        const float halfHeight = std::max(capsule.HalfHeight, 0.001f);
        return kPi * radius * radius * (2.0f * halfHeight)
            + (4.0f / 3.0f) * kPi * radius * radius * radius;
    }

    return 1.0f;
}

b3BodyType ToBodyType(BodyType type)
{
    switch (type)
    {
    case BodyType::Dynamic:
        return b3_dynamicBody;
    case BodyType::Kinematic:
        return b3_kinematicBody;
    case BodyType::Static:
    default:
        return b3_staticBody;
    }
}

} // namespace

struct PhysicsWorld::Impl
{
    b3WorldId World {};
    std::unordered_map<EntityID, b3BodyId> Bodies;
    std::unordered_map<uint64_t, Entity> EntityByBody;
    glm::vec3 Gravity { 0.0f, -9.81f, 0.0f };
};

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld()
{
    Shutdown();
}

void PhysicsWorld::Init()
{
    if (m_Impl)
        return;

    m_Impl = MakeScope<Impl>();

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = ToB3(m_Impl->Gravity);
    worldDef.enableSleep = true;
    worldDef.workerCount = std::max(1u, std::thread::hardware_concurrency());

    m_Impl->World = b3CreateWorld(&worldDef);
    if (!b3World_IsValid(m_Impl->World))
    {
        SLATE_ERROR("Box3D world creation failed");
        m_Impl.reset();
        return;
    }

    SLATE_INFO("Physics initialized (Box3D, {} worker threads)", worldDef.workerCount);
}

void PhysicsWorld::Shutdown()
{
    if (!m_Impl)
        return;

    Clear();

    if (b3World_IsValid(m_Impl->World))
        b3DestroyWorld(m_Impl->World);

    m_Impl.reset();
    SLATE_INFO("Physics shut down");
}

void PhysicsWorld::Clear()
{
    if (!m_Impl)
        return;

    for (const auto& [entityId, bodyId] : m_Impl->Bodies)
    {
        if (b3Body_IsValid(bodyId))
            b3DestroyBody(bodyId);
    }

    m_Impl->Bodies.clear();
    m_Impl->EntityByBody.clear();
}

void PhysicsWorld::SetGravity(const glm::vec3& gravity)
{
    if (!m_Impl)
        return;

    m_Impl->Gravity = gravity;
    if (b3World_IsValid(m_Impl->World))
        b3World_SetGravity(m_Impl->World, ToB3(gravity));
}

b3BodyId CreateBody(b3WorldId world, Entity entity, RigidBodyComponent& body,
    TransformComponent& transform)
{
    b3BodyDef bodyDef = b3DefaultBodyDef();
    bodyDef.type = ToBodyType(body.Type);
    bodyDef.position = ToB3Pos(glm::vec3(transform.WorldMatrix[3]));
    bodyDef.rotation = ToB3(EulerToQuat(transform.Rotation));
    bodyDef.enableSleep = true;
    bodyDef.gravityScale = body.UseGravity ? 1.0f : 0.0f;

    const b3BodyId bodyId = b3CreateBody(world, &bodyDef);
    if (!b3Body_IsValid(bodyId))
    {
        SLATE_WARN("Failed to create a physics body");
        return b3BodyId {};
    }

    b3ShapeDef shapeDef = b3DefaultShapeDef();
    shapeDef.baseMaterial.friction = body.Friction;
    shapeDef.baseMaterial.restitution = body.Restitution;

    const float volume = ShapeVolume(entity);
    shapeDef.density = (body.Type == BodyType::Dynamic && body.Mass > 0.0f && volume > 0.0f)
        ? body.Mass / volume
        : 1.0f;

    if (entity.HasComponent<BoxColliderComponent>())
    {
        const glm::vec3 half = entity.GetComponent<BoxColliderComponent>().HalfExtents;
        const b3BoxHull hull = b3MakeBoxHull(std::max(half.x, 0.01f),
            std::max(half.y, 0.01f), std::max(half.z, 0.01f));
        b3CreateHullShape(bodyId, &shapeDef, &hull.base);
    }
    else if (entity.HasComponent<SphereColliderComponent>())
    {
        const float radius = std::max(entity.GetComponent<SphereColliderComponent>().Radius, 0.01f);
        const b3Sphere sphere { b3Vec3 { 0.0f, 0.0f, 0.0f }, radius };
        b3CreateSphereShape(bodyId, &shapeDef, &sphere);
    }
    else if (entity.HasComponent<CapsuleColliderComponent>())
    {
        const auto& capsule = entity.GetComponent<CapsuleColliderComponent>();
        const float radius = std::max(capsule.Radius, 0.01f);
        const float halfHeight = std::max(capsule.HalfHeight, 0.01f);
        const b3Capsule shape {
            b3Vec3 { 0.0f, -halfHeight, 0.0f },
            b3Vec3 { 0.0f, halfHeight, 0.0f },
            radius
        };
        b3CreateCapsuleShape(bodyId, &shapeDef, &shape);
    }
    else
    {
        const b3BoxHull hull = b3MakeBoxHull(0.5f, 0.5f, 0.5f);
        b3CreateHullShape(bodyId, &shapeDef, &hull.base);
    }

    return bodyId;
}

void PhysicsWorld::Step(Scene& scene, double deltaTime)
{
    if (!m_Impl || !b3World_IsValid(m_Impl->World))
        return;

    // Bodies are created from world transforms, so make sure they are current.
    scene.UpdateTransforms();

    // Remove bodies for entities that no longer exist or lost their rigid body.
    for (auto it = m_Impl->Bodies.begin(); it != m_Impl->Bodies.end();)
    {
        bool alive = false;
        scene.GetRegistry().Each<IDComponent>([&](Entity entity, IDComponent&)
        {
            if (entity.GetID() == it->first && entity.HasComponent<RigidBodyComponent>())
                alive = true;
        });

        if (!alive)
        {
            const b3BodyId bodyId = it->second;
            m_Impl->EntityByBody.erase(BodyKey(bodyId));
            if (b3Body_IsValid(bodyId))
                b3DestroyBody(bodyId);
            it = m_Impl->Bodies.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Create missing bodies and push kinematic transforms.
    scene.GetRegistry().Each<RigidBodyComponent, TransformComponent>(
        [&](Entity entity, RigidBodyComponent& body, TransformComponent& transform)
        {
            const auto it = m_Impl->Bodies.find(entity.GetID());
            if (it == m_Impl->Bodies.end())
            {
                const b3BodyId bodyId = CreateBody(m_Impl->World, entity, body, transform);
                if (b3Body_IsValid(bodyId))
                {
                    m_Impl->Bodies[entity.GetID()] = bodyId;
                    m_Impl->EntityByBody[BodyKey(bodyId)] = entity;
                }
            }
            else if (body.Type == BodyType::Kinematic)
            {
                b3Body_SetTransform(it->second,
                    ToB3Pos(glm::vec3(transform.WorldMatrix[3])),
                    ToB3(EulerToQuat(transform.Rotation)));
            }
        });

    if (deltaTime > 0.0)
        b3World_Step(m_Impl->World, static_cast<float>(deltaTime), 4);

    // Write dynamic body transforms back into the scene.
    scene.GetRegistry().Each<RigidBodyComponent, TransformComponent>(
        [&](Entity entity, RigidBodyComponent& body, TransformComponent& transform)
        {
            if (body.Type != BodyType::Dynamic)
                return;

            const auto it = m_Impl->Bodies.find(entity.GetID());
            if (it == m_Impl->Bodies.end())
                return;

            const b3BodyId bodyId = it->second;
            if (!b3Body_IsValid(bodyId))
                return;

            transform.Translation = FromB3(b3Body_GetPosition(bodyId));
            transform.Rotation = QuatToEulerDegrees(FromB3(b3Body_GetRotation(bodyId)));
        });
}

bool PhysicsWorld::Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, glm::vec3& outHitPoint, Entity& outEntity) const
{
    (void)scene;

    if (!m_Impl || !b3World_IsValid(m_Impl->World))
        return false;

    const float length = glm::length(direction);
    if (length < 1e-6f || maxDistance <= 0.0f)
        return false;

    const glm::vec3 normalized = direction / length;
    const b3QueryFilter filter = b3DefaultQueryFilter();
    const b3RayResult result = b3World_CastRayClosest(m_Impl->World, ToB3Pos(origin),
        ToB3(normalized * maxDistance), filter);

    if (!result.hit)
        return false;

    outHitPoint = FromB3(result.point);

    const b3BodyId hitBody = b3Shape_GetBody(result.shapeId);
    const auto it = m_Impl->EntityByBody.find(BodyKey(hitBody));
    if (it != m_Impl->EntityByBody.end())
        outEntity = it->second;

    return true;
}

size_t PhysicsWorld::GetBodyCount() const
{
    return m_Impl ? m_Impl->Bodies.size() : 0;
}

} // namespace Slate
