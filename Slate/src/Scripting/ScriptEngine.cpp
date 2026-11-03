#include "Slate/Scripting/ScriptEngine.h"
#include "Slate/Renderer/Renderer.h"
#include "Slate/Audio/AudioEngine.h"
#include "Slate/Scene/Scene.h"
#include "Slate/Physics/PhysicsWorld.h"
#include "Slate/Platform/Input.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <sol/sol.hpp>

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace Slate {

namespace {

sol::table Vec3ToTable(sol::state& lua, const glm::vec3& value)
{
    sol::table table = lua.create_table();
    table["x"] = value.x;
    table["y"] = value.y;
    table["z"] = value.z;
    return table;
}

} // namespace

struct ScriptEngine::Impl
{
    struct Instance
    {
        Entity Entity;
        sol::environment Environment;
        sol::protected_function OnCreate;
        sol::protected_function OnUpdate;
        sol::protected_function OnDestroy;
    };

    sol::state Lua;
    Scene* ScenePtr = nullptr;
    PhysicsWorld* Physics = nullptr;
    Renderer* UIRenderer = nullptr;
    AudioEngine* Audio = nullptr;
    std::vector<Instance> Instances;
    double DeltaTime = 0.0;

    void ReportError(const sol::protected_function_result& result, const std::string& context)
    {
        if (!result.valid())
        {
            const sol::error error = result;
            SLATE_ERROR("Lua error in {}: {}", context, error.what());
        }
    }
};

ScriptEngine::ScriptEngine() = default;
ScriptEngine::~ScriptEngine() { Shutdown(); }

void ScriptEngine::Init(Scene& scene, PhysicsWorld* physics, Renderer* renderer,
    AudioEngine* audio)
{
    if (m_Impl)
        return;

    m_Impl = MakeScope<Impl>();
    m_Impl->ScenePtr = &scene;
    m_Impl->Physics = physics;
    m_Impl->UIRenderer = renderer;
    m_Impl->Audio = audio;

    m_Impl->Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);

    // Entity API.
    m_Impl->Lua.new_usertype<Entity>("Entity",
        sol::constructors<Entity()>(),
        "IsValid", [](const Entity& entity) { return entity.IsValid(); },
        "Name", [](const Entity& entity) -> std::string
        {
            return entity.HasComponent<TagComponent>() ? entity.GetComponent<TagComponent>().Name : "";
        },
        "SetName", [](Entity entity, const std::string& name)
        {
            if (entity.HasComponent<TagComponent>())
                entity.GetComponent<TagComponent>().Name = name;
        },
        "Position", [this](const Entity& entity)
        {
            glm::vec3 position(0.0f);
            if (entity.HasComponent<TransformComponent>())
                position = entity.GetComponent<TransformComponent>().Translation;
            return Vec3ToTable(m_Impl->Lua, position);
        },
        "SetPosition", [](Entity entity, float x, float y, float z)
        {
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Translation = glm::vec3(x, y, z);
        },
        "Rotation", [this](const Entity& entity)
        {
            glm::vec3 rotation(0.0f);
            if (entity.HasComponent<TransformComponent>())
                rotation = entity.GetComponent<TransformComponent>().Rotation;
            return Vec3ToTable(m_Impl->Lua, rotation);
        },
        "SetRotation", [](Entity entity, float x, float y, float z)
        {
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Rotation = glm::vec3(x, y, z);
        },
        "Scale", [this](const Entity& entity)
        {
            glm::vec3 scale(1.0f);
            if (entity.HasComponent<TransformComponent>())
                scale = entity.GetComponent<TransformComponent>().Scale;
            return Vec3ToTable(m_Impl->Lua, scale);
        },
        "SetScale", [](Entity entity, float x, float y, float z)
        {
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Scale = glm::vec3(x, y, z);
        },
        "SetMesh", [](Entity entity, const std::string& modelPath)
        {
            if (!entity.HasComponent<MeshRendererComponent>())
                entity.AddComponent<MeshRendererComponent>();
            entity.GetComponent<MeshRendererComponent>().ModelPath = modelPath;
        },
        "SetTint", [](Entity entity, float r, float g, float b, float a)
        {
            if (!entity.HasComponent<MeshRendererComponent>())
                entity.AddComponent<MeshRendererComponent>();
            entity.GetComponent<MeshRendererComponent>().Tint = glm::vec4(r, g, b, a);
        },
        "Tint", [this](const Entity& entity) -> sol::table
        {
            glm::vec4 tint(1.0f);
            if (entity.HasComponent<MeshRendererComponent>())
                tint = entity.GetComponent<MeshRendererComponent>().Tint;

            sol::table table = m_Impl->Lua.create_table();
            table["r"] = tint.r;
            table["g"] = tint.g;
            table["b"] = tint.b;
            table["a"] = tint.a;
            return table;
        },
        "SetVisible", [](Entity entity, bool visible)
        {
            if (entity.HasComponent<MeshRendererComponent>())
                entity.GetComponent<MeshRendererComponent>().Visible = visible;
        },
        "SetRigidBody", [](Entity entity, int type, float mass)
        {
            RigidBodyComponent& body = entity.HasComponent<RigidBodyComponent>()
                ? entity.GetComponent<RigidBodyComponent>()
                : entity.AddComponent<RigidBodyComponent>();

            body.Type = static_cast<BodyType>(type);
            body.Mass = mass;
            body.UseGravity = true;
        },
        "SetBoxCollider", [](Entity entity, float hx, float hy, float hz)
        {
            BoxColliderComponent& collider = entity.HasComponent<BoxColliderComponent>()
                ? entity.GetComponent<BoxColliderComponent>()
                : entity.AddComponent<BoxColliderComponent>();
            collider.HalfExtents = glm::vec3(hx, hy, hz);
        },
        "SetSphereCollider", [](Entity entity, float radius)
        {
            SphereColliderComponent& collider = entity.HasComponent<SphereColliderComponent>()
                ? entity.GetComponent<SphereColliderComponent>()
                : entity.AddComponent<SphereColliderComponent>();
            collider.Radius = radius;
        },
        "SetCapsuleCollider", [](Entity entity, float radius, float halfHeight)
        {
            CapsuleColliderComponent& collider = entity.HasComponent<CapsuleColliderComponent>()
                ? entity.GetComponent<CapsuleColliderComponent>()
                : entity.AddComponent<CapsuleColliderComponent>();
            collider.Radius = radius;
            collider.HalfHeight = halfHeight;
        },
        "HasRigidBody", [](const Entity& entity) { return entity.HasComponent<RigidBodyComponent>(); },
        "Translate", [](Entity entity, float x, float y, float z)
        {
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Translation += glm::vec3(x, y, z);
        },
        "Rotate", [](Entity entity, float x, float y, float z)
        {
            if (entity.HasComponent<TransformComponent>())
                entity.GetComponent<TransformComponent>().Rotation += glm::vec3(x, y, z);
        },
        "Destroy", [this](Entity entity)
        {
            if (entity.IsValid())
                m_Impl->ScenePtr->DestroyEntity(entity);
        });

    // Scene API.
    m_Impl->Lua.set_function("CreateEntity", [this](const std::string& name)
    {
        return m_Impl->ScenePtr->CreateEntity(name);
    });

    m_Impl->Lua.set_function("FindEntity", [this](const std::string& name)
    {
        Entity found;
        m_Impl->ScenePtr->GetRegistry().Each<TagComponent>([&](Entity entity, TagComponent& tag)
        {
            if (!found.IsValid() && tag.Name == name)
                found = entity;
        });
        return found;
    });

    m_Impl->Lua.set_function("SpawnPrefab", [this](const std::string& path, float x, float y, float z)
    {
        return m_Impl->ScenePtr->InstantiatePrefab(path, glm::vec3(x, y, z));
    });

    m_Impl->Lua.set_function("DestroyEntity", [this](Entity entity)
    {
        if (entity.IsValid())
            m_Impl->ScenePtr->DestroyEntity(entity);
    });

    // Input API.
    m_Impl->Lua.set_function("KeyDown", [](const std::string& key)
    {
        return Input::IsKeyDown(KeyFromString(key));
    });

    m_Impl->Lua.set_function("MouseDown", [](const std::string& button)
    {
        if (button == "Left") return Input::IsMouseButtonDown(MouseButton::Left);
        if (button == "Right") return Input::IsMouseButtonDown(MouseButton::Right);
        return Input::IsMouseButtonDown(MouseButton::Middle);
    });

    m_Impl->Lua.set_function("Log", [](const std::string& message)
    {
        SLATE_INFO("[Lua] {}", message);
    });

    // UI API.
    m_Impl->Lua.set_function("DrawText", [this](float x, float y, const std::string& text,
        float size, float r, float g, float b, float a)
    {
        if (m_Impl->UIRenderer)
            m_Impl->UIRenderer->DrawUIText(x, y, text, size, glm::vec4(r, g, b, a));
    });

    m_Impl->Lua.set_function("DrawRect", [this](float x, float y, float width, float height,
        float r, float g, float b, float a)
    {
        if (m_Impl->UIRenderer)
            m_Impl->UIRenderer->DrawUIRect(x, y, width, height, glm::vec4(r, g, b, a));
    });

    m_Impl->Lua.set_function("ScreenWidth", [this]() -> float
    {
        return m_Impl->UIRenderer ? m_Impl->UIRenderer->GetViewportSize().x : 0.0f;
    });

    m_Impl->Lua.set_function("ScreenHeight", [this]() -> float
    {
        return m_Impl->UIRenderer ? m_Impl->UIRenderer->GetViewportSize().y : 0.0f;
    });

    m_Impl->Lua.set_function("PlaySound", [this](const std::string& path, float volume,
        bool loop, bool spatial)
    {
        if (m_Impl->Audio)
            m_Impl->Audio->PlaySound(path, volume, loop, spatial);
    });

    m_Impl->Lua.set_function("PhysicsBodyCount", [this]() -> int
    {
        return m_Impl->Physics ? static_cast<int>(m_Impl->Physics->GetBodyCount()) : 0;
    });

    // Physics API.
    m_Impl->Lua.set_function("Raycast", [this](float ox, float oy, float oz,
        float dx, float dy, float dz, float maxDistance) -> sol::table
    {
        sol::table result = m_Impl->Lua.create_table();
        result["hit"] = false;

        if (!m_Impl->Physics)
            return result;

        glm::vec3 hitPoint(0.0f);
        Entity hitEntity;
        const bool hit = m_Impl->Physics->Raycast(*m_Impl->ScenePtr,
            glm::vec3(ox, oy, oz), glm::vec3(dx, dy, dz), maxDistance, hitPoint, hitEntity);

        result["hit"] = hit;
        if (hit)
        {
            result["x"] = hitPoint.x;
            result["y"] = hitPoint.y;
            result["z"] = hitPoint.z;
            result["entity"] = hitEntity;
        }
        return result;
    });

    SLATE_INFO("Lua scripting initialized");
}

void ScriptEngine::Shutdown()
{
    if (!m_Impl)
        return;
    Stop();
    m_Impl.reset();
}

void ScriptEngine::Start()
{
    if (!m_Impl)
        return;

    Stop();

    m_Impl->ScenePtr->GetRegistry().Each<ScriptComponent>([&](Entity entity, ScriptComponent& script)
    {
        if (script.ScriptPath.empty())
            return;

        const std::filesystem::path path = FileSystem::ResolveAssetPath(script.ScriptPath);
        const auto text = FileSystem::ReadTextFile(path);
        if (!text)
        {
            SLATE_WARN("Script '{}' could not be read", path.string());
            return;
        }

        sol::environment environment(m_Impl->Lua, sol::create, m_Impl->Lua.globals());
        environment["Entity"] = entity;

        const sol::protected_function_result result = m_Impl->Lua.safe_script(
            *text, environment, sol::script_pass_on_error);
        if (!result.valid())
        {
            const sol::error error = result;
            SLATE_ERROR("Failed to load script '{}': {}", path.string(), error.what());
            return;
        }

        Impl::Instance instance;
        instance.Entity = entity;
        instance.Environment = environment;
        instance.OnCreate = environment["OnCreate"];
        instance.OnUpdate = environment["OnUpdate"];
        instance.OnDestroy = environment["OnDestroy"];

        m_Impl->Instances.push_back(std::move(instance));
    });

    for (auto& instance : m_Impl->Instances)
    {
        if (instance.OnCreate.valid() && instance.Entity.IsValid())
            m_Impl->ReportError(instance.OnCreate(instance.Entity), "OnCreate");
    }

    SLATE_INFO("Started {} script instance(s)", m_Impl->Instances.size());
}

void ScriptEngine::Update(double deltaTime)
{
    if (!m_Impl)
        return;

    m_Impl->DeltaTime = deltaTime;
    m_Impl->Lua["DeltaTime"] = deltaTime;

    for (auto& instance : m_Impl->Instances)
    {
        if (!instance.Entity.IsValid() || !instance.Entity.HasComponent<ScriptComponent>())
            continue;

        if (instance.OnUpdate.valid())
            m_Impl->ReportError(instance.OnUpdate(instance.Entity, deltaTime), "OnUpdate");
    }
}

void ScriptEngine::Stop()
{
    if (!m_Impl)
        return;

    for (auto& instance : m_Impl->Instances)
    {
        if (instance.OnDestroy.valid() && instance.Entity.IsValid())
            m_Impl->ReportError(instance.OnDestroy(instance.Entity), "OnDestroy");
    }

    m_Impl->Instances.clear();
}

size_t ScriptEngine::GetInstanceCount() const
{
    return m_Impl ? m_Impl->Instances.size() : 0;
}

} // namespace Slate
