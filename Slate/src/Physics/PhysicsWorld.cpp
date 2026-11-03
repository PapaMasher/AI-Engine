#include "Slate/Physics/PhysicsWorld.h"
#include "Slate/Core/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <thread>
#include <unordered_map>

namespace Slate {

namespace PhysicsLayers {

static constexpr JPH::ObjectLayer NonMoving = 0;
static constexpr JPH::ObjectLayer Moving = 1;
static constexpr JPH::ObjectLayer Count = 2;

} // namespace PhysicsLayers

namespace BroadPhaseLayers {

static constexpr JPH::BroadPhaseLayer NonMoving(0);
static constexpr JPH::BroadPhaseLayer Moving(1);
static constexpr uint32_t Count = 2;

} // namespace BroadPhaseLayers

namespace {

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        m_ObjectToBroadPhase[PhysicsLayers::NonMoving] = BroadPhaseLayers::NonMoving;
        m_ObjectToBroadPhase[PhysicsLayers::Moving] = BroadPhaseLayers::Moving;
    }

    uint32_t GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::Count; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return m_ObjectToBroadPhase[layer];
    }

    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == BroadPhaseLayers::NonMoving ? "NON_MOVING" : "MOVING";
    }

private:
    JPH::BroadPhaseLayer m_ObjectToBroadPhase[PhysicsLayers::Count];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override
    {
        if (layer1 == PhysicsLayers::NonMoving)
            return layer2 == BroadPhaseLayers::Moving;
        return true;
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override
    {
        if (layer1 == PhysicsLayers::NonMoving)
            return layer2 == PhysicsLayers::Moving;
        if (layer2 == PhysicsLayers::NonMoving)
            return layer1 == PhysicsLayers::Moving;
        return true;
    }
};

glm::quat EulerToQuat(const glm::vec3& eulerDegrees)
{
    const glm::vec3 radians = glm::radians(eulerDegrees);
    const glm::quat qy = glm::angleAxis(radians.y, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat qx = glm::angleAxis(radians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat qz = glm::angleAxis(radians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize(qy * qx * qz);
}

glm::vec3 QuatToEulerDegrees(const glm::quat& q)
{
    const glm::mat4 m = glm::mat4_cast(q);

    // Decompose the YXZ rotation order used by TransformComponent.
    const float x = std::asin(std::clamp(-m[2][1], -1.0f, 1.0f));
    const float y = std::atan2(m[2][0], m[2][2]);
    const float z = std::atan2(m[0][1], m[1][1]);

    return glm::degrees(glm::vec3(x, y, z));
}

bool g_JoltRegistered = false;

} // namespace

struct PhysicsWorld::Impl
{
    JPH::TempAllocatorImpl* TempAllocator = nullptr;
    JPH::JobSystemThreadPool* JobSystem = nullptr;
    JPH::PhysicsSystem* Physics = nullptr;

    BPLayerInterfaceImpl BroadPhaseLayerInterface;
    ObjectVsBroadPhaseLayerFilterImpl ObjectVsBroadPhaseFilter;
    ObjectLayerPairFilterImpl ObjectLayerPairFilter;

    std::unordered_map<EntityID, JPH::BodyID> Bodies;
    std::unordered_map<uint32_t, EntityID> EntityByBody;
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

    if (!g_JoltRegistered)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        g_JoltRegistered = true;
    }

    auto impl = MakeScope<Impl>();
    impl->TempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024);

    const unsigned int workerThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    impl->JobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(workerThreads));

    impl->Physics = new JPH::PhysicsSystem();
    impl->Physics->Init(1024, 0, 1024, 1024,
        impl->BroadPhaseLayerInterface, impl->ObjectVsBroadPhaseFilter, impl->ObjectLayerPairFilter);
    impl->Physics->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    m_Impl = std::move(impl);
    SLATE_INFO("Physics initialized (Jolt, {} worker threads)", workerThreads);
}

void PhysicsWorld::Shutdown()
{
    if (!m_Impl)
        return;

    delete m_Impl->Physics;
    delete m_Impl->JobSystem;
    delete m_Impl->TempAllocator;
    m_Impl.reset();
}

void PhysicsWorld::Clear()
{
    if (!m_Impl)
        return;

    JPH::BodyInterface& bodyInterface = m_Impl->Physics->GetBodyInterface();
    for (auto& [entity, body] : m_Impl->Bodies)
    {
        bodyInterface.RemoveBody(body);
        bodyInterface.DestroyBody(body);
    }

    m_Impl->Bodies.clear();
    m_Impl->EntityByBody.clear();
}

void PhysicsWorld::SetGravity(const glm::vec3& gravity)
{
    if (m_Impl)
        m_Impl->Physics->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
}

void PhysicsWorld::Step(Scene& scene, double deltaTime)
{
    if (!m_Impl)
        return;

    // Bodies are created from world transforms, so make sure they are current.
    scene.UpdateTransforms();

    JPH::BodyInterface& bodyInterface = m_Impl->Physics->GetBodyInterface();

    // Remove bodies for entities that no longer exist or lost their rigid body.
    for (auto it = m_Impl->Bodies.begin(); it != m_Impl->Bodies.end();)
    {
        bool alive = false;
        scene.GetRegistry().Each<IDComponent>([&](Entity e, IDComponent&)
        {
            if (e.GetID() == it->first && e.HasComponent<RigidBodyComponent>())
                alive = true;
        });

        if (!alive)
        {
            bodyInterface.RemoveBody(it->second);
            bodyInterface.DestroyBody(it->second);
            m_Impl->EntityByBody.erase(it->second.GetIndexAndSequenceNumber());
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
                JPH::RefConst<JPH::Shape> shape;

                if (entity.HasComponent<BoxColliderComponent>())
                {
                    const glm::vec3 half = entity.GetComponent<BoxColliderComponent>().HalfExtents;
                    shape = new JPH::BoxShape(JPH::Vec3(half.x, half.y, half.z));
                }
                else if (entity.HasComponent<SphereColliderComponent>())
                {
                    shape = new JPH::SphereShape(entity.GetComponent<SphereColliderComponent>().Radius);
                }
                else
                {
                    shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
                }

                JPH::EMotionType motionType = JPH::EMotionType::Static;
                JPH::ObjectLayer layer = PhysicsLayers::NonMoving;

                if (body.Type == BodyType::Dynamic)
                {
                    motionType = JPH::EMotionType::Dynamic;
                    layer = PhysicsLayers::Moving;
                }
                else if (body.Type == BodyType::Kinematic)
                {
                    motionType = JPH::EMotionType::Kinematic;
                    layer = PhysicsLayers::Moving;
                }

                const glm::vec3 position = glm::vec3(transform.WorldMatrix[3]);
                const glm::quat rotation = EulerToQuat(transform.Rotation);

                JPH::BodyCreationSettings settings(shape,
                    JPH::RVec3(position.x, position.y, position.z),
                    JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                    motionType, layer);
                settings.mFriction = body.Friction;
                settings.mRestitution = body.Restitution;
                settings.mAllowSleeping = true;

                const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(
                    settings, JPH::EActivation::Activate);

                m_Impl->Bodies[entity.GetID()] = bodyId;
                m_Impl->EntityByBody[bodyId.GetIndexAndSequenceNumber()] = entity.GetID();
            }
            else if (body.Type == BodyType::Kinematic)
            {
                const glm::vec3 position = glm::vec3(transform.WorldMatrix[3]);
                const glm::quat rotation = EulerToQuat(transform.Rotation);
                bodyInterface.SetPositionAndRotation(it->second,
                    JPH::RVec3(position.x, position.y, position.z),
                    JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
                    JPH::EActivation::Activate);
            }
        });

    if (deltaTime > 0.0)
    {
        m_Impl->Physics->Update(static_cast<float>(deltaTime), 1, m_Impl->TempAllocator, m_Impl->JobSystem);
    }

    // Write dynamic body transforms back to the scene.
    scene.GetRegistry().Each<RigidBodyComponent, TransformComponent>(
        [&](Entity entity, RigidBodyComponent& body, TransformComponent& transform)
        {
            if (body.Type != BodyType::Dynamic)
                return;

            const auto it = m_Impl->Bodies.find(entity.GetID());
            if (it == m_Impl->Bodies.end())
                return;

            JPH::RVec3 position;
            JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(it->second, position, rotation);

            transform.Translation = glm::vec3(position.GetX(), position.GetY(), position.GetZ());
            transform.Rotation = QuatToEulerDegrees(glm::quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()));
        });
}

bool PhysicsWorld::Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction,
    float maxDistance, glm::vec3& outHitPoint, Entity& outEntity) const
{
    if (!m_Impl || maxDistance <= 0.0f)
        return false;

    const glm::vec3 normalized = glm::normalize(direction);
    const glm::vec3 scaled = normalized * maxDistance;

    JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
        JPH::Vec3(scaled.x, scaled.y, scaled.z));

    JPH::RayCastResult hit;
    if (!m_Impl->Physics->GetNarrowPhaseQuery().CastRay(ray, hit))
        return false;

    outHitPoint = origin + normalized * (maxDistance * hit.mFraction);

    const auto it = m_Impl->EntityByBody.find(hit.mBodyID.GetIndexAndSequenceNumber());
    if (it != m_Impl->EntityByBody.end())
    {
        Registry& registry = scene.GetRegistry();
        outEntity = Entity(it->second, registry.GetGeneration(it->second), &registry);
    }
    else
    {
        outEntity = Entity();
    }
    return true;
}

size_t PhysicsWorld::GetBodyCount() const
{
    return m_Impl ? m_Impl->Bodies.size() : 0;
}

} // namespace Slate
