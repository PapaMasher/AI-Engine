#pragma once

#include "Slate/Core/Base.h"

namespace Slate {

class Scene;
class PhysicsWorld;

// Lua scripting runtime. Each entity with a ScriptComponent gets its own Lua
// environment, so scripts can keep per-entity state. Scripts implement
// OnCreate(entity), OnUpdate(entity, deltaTime) and optionally OnDestroy(entity).
class ScriptEngine
{
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    void Init(Scene& scene, PhysicsWorld* physics);
    void Shutdown();
    bool IsInitialized() const { return m_Impl != nullptr; }

    // Loads scripts for every ScriptComponent and calls OnCreate.
    void Start();

    // Calls OnUpdate on all live script instances.
    void Update(double deltaTime);

    // Calls OnDestroy and releases script instances.
    void Stop();

    size_t GetInstanceCount() const;

private:
    struct Impl;
    Scope<Impl> m_Impl;
};

} // namespace Slate
