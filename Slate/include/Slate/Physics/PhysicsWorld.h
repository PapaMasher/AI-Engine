#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Scene/Scene.h"

#include <glm/glm.hpp>

namespace Slate {

// Box3D-based rigid body physics (the 3D successor to Box2D v3). Bodies are
// created from entities that have a RigidBodyComponent plus a box, sphere or
// capsule collider. Dynamic bodies write their transforms back into the scene
// after each step.
class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Init();
    void Shutdown();
    bool IsInitialized() const { return m_Impl != nullptr; }

    // Advances the simulation and synchronizes transforms.
    void Step(Scene& scene, double deltaTime);

    // Removes all bodies (used when loading a new scene).
    void Clear();

    void SetGravity(const glm::vec3& gravity);

    // Casts a ray through the physics world. Returns true on hit.
    bool Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
        glm::vec3& outHitPoint, Entity& outEntity) const;

    size_t GetBodyCount() const;

private:
    struct Impl;
    Scope<Impl> m_Impl;
};

} // namespace Slate
