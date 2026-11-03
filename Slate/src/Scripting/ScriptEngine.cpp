#include "Slate/Scripting/ScriptEngine.h"
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

Key KeyFromString(const std::string& name)
{
    static const std::unordered_map<std::string, Key> keys = {
        { "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D },
        { "E", Key::E }, { "F", Key::F }, { "G", Key::G }, { "H", Key::H },
        { "I", Key::I }, { "J", Key::J }, { "K", Key::K }, { "L", Key::L },
        { "M", Key::M }, { "N", Key::N }, { "O", Key::O }, { "P", Key::P },
        { "Q", Key::Q }, { "R", Key::R }, { "S", Key::S }, { "T", Key::T },
        { "U", Key::U }, { "V", Key::V }, { "W", Key::W }, { "X", Key::X },
        { "Y", Key::Y }, { "Z", Key::Z },
        { "Space", Key::Space }, { "Enter", Key::Enter }, { "Escape", Key::Escape },
        { "LeftShift", Key::LeftShift }, { "LeftControl", Key::LeftControl },
        { "LeftAlt", Key::LeftAlt }, { "Up", Key::Up }, { "Down", Key::Down },
        { "Left", Key::Left }, { "Right", Key::Right }
    };

    const auto it = keys.find(name);
    return it != keys.end() ? it->second : Key::Space;
}

sol::table Vec3ToTable(sol::state& lua, const glm::vec3& value)
{
    sol::table table = lua.create_table();
    table["x"] = value.x;
    table["y"] = value.y;
    table["z"] = value.z;
    return table;
}

glm::vec3 TableToVec3(const sol::table& table)
{
    return glm::vec3(table.get_or("x", 0.0f), table.get_or("y", 0.0f), table.get_or("z", 0.0f));
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

void ScriptEngine::Init(Scene& scene, PhysicsWorld* physics)
{
    if (m_Impl)
        return;

    m_Impl = MakeScope<Impl>();
    m_Impl->ScenePtr = &scene;
    m_Impl->Physics = physics;

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
