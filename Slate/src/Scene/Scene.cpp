#include "Slate/Scene/Scene.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace Slate {

using json = nlohmann::json;

namespace {

json Vec3ToJson(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }
json Vec4ToJson(const glm::vec4& v) { return json::array({ v.x, v.y, v.z, v.w }); }

glm::vec3 Vec3FromJson(const json& j)
{
    if (!j.is_array() || j.size() < 3)
        return glm::vec3(0.0f);
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

glm::vec4 Vec4FromJson(const json& j)
{
    if (!j.is_array() || j.size() < 4)
        return glm::vec4(1.0f);
    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
}

json SerializeEntityToJson(const Registry& registry, Entity entity)
{
    (void)registry;
    json j;
    if (entity.HasComponent<IDComponent>())
        j["uuid"] = entity.GetComponent<IDComponent>().ID.ToString();

    if (entity.HasComponent<TagComponent>())
        j["name"] = entity.GetComponent<TagComponent>().Name;

    if (entity.HasComponent<ParentComponent>())
        j["parent"] = entity.GetComponent<ParentComponent>().Parent.ToString();

    if (entity.HasComponent<TransformComponent>())
    {
        const auto& t = entity.GetComponent<TransformComponent>();
        j["transform"] = {
            { "translation", Vec3ToJson(t.Translation) },
            { "rotation", Vec3ToJson(t.Rotation) },
            { "scale", Vec3ToJson(t.Scale) }
        };
    }

    if (entity.HasComponent<MeshRendererComponent>())
    {
        const auto& m = entity.GetComponent<MeshRendererComponent>();
        j["meshRenderer"] = {
            { "model", m.ModelPath },
            { "tint", Vec4ToJson(m.Tint) },
            { "visible", m.Visible },
            { "castShadows", m.CastShadows }
        };
    }

    if (entity.HasComponent<CameraComponent>())
    {
        const auto& c = entity.GetComponent<CameraComponent>();
        j["camera"] = {
            { "fieldOfView", c.FieldOfView },
            { "nearClip", c.NearClip },
            { "farClip", c.FarClip },
            { "primary", c.Primary }
        };
    }

    if (entity.HasComponent<DirectionalLightComponent>())
    {
        const auto& l = entity.GetComponent<DirectionalLightComponent>();
        j["directionalLight"] = {
            { "direction", Vec3ToJson(l.Direction) },
            { "color", Vec3ToJson(l.Color) },
            { "intensity", l.Intensity },
            { "castShadows", l.CastShadows }
        };
    }

    if (entity.HasComponent<PointLightComponent>())
    {
        const auto& l = entity.GetComponent<PointLightComponent>();
        j["pointLight"] = {
            { "color", Vec3ToJson(l.Color) },
            { "intensity", l.Intensity },
            { "range", l.Range }
        };
    }

    if (entity.HasComponent<RigidBodyComponent>())
    {
        const auto& r = entity.GetComponent<RigidBodyComponent>();
        j["rigidBody"] = {
            { "type", static_cast<int>(r.Type) },
            { "mass", r.Mass },
            { "friction", r.Friction },
            { "restitution", r.Restitution },
            { "useGravity", r.UseGravity }
        };
    }

    if (entity.HasComponent<BoxColliderComponent>())
        j["boxCollider"] = { { "halfExtents", Vec3ToJson(entity.GetComponent<BoxColliderComponent>().HalfExtents) } };

    if (entity.HasComponent<SphereColliderComponent>())
        j["sphereCollider"] = { { "radius", entity.GetComponent<SphereColliderComponent>().Radius } };

    if (entity.HasComponent<ScriptComponent>())
        j["script"] = { { "path", entity.GetComponent<ScriptComponent>().ScriptPath } };

    if (entity.HasComponent<AudioSourceComponent>())
    {
        const auto& a = entity.GetComponent<AudioSourceComponent>();
        j["audioSource"] = {
            { "clip", a.ClipPath },
            { "volume", a.Volume },
            { "loop", a.Loop },
            { "playOnStart", a.PlayOnStart },
            { "spatial", a.Spatial }
        };
    }

    return j;
}

Entity DeserializeEntityFromJson(Scene& scene, const json& j)
{
    Registry& registry = scene.GetRegistry();

    const std::string name = j.value("name", "Entity");
    Entity entity = scene.CreateEntity(name);

    if (j.contains("transform"))
    {
        auto& t = entity.GetComponent<TransformComponent>();
        const auto& tj = j["transform"];
        t.Translation = Vec3FromJson(tj.value("translation", json::array({ 0.0f, 0.0f, 0.0f })));
        t.Rotation = Vec3FromJson(tj.value("rotation", json::array({ 0.0f, 0.0f, 0.0f })));
        t.Scale = Vec3FromJson(tj.value("scale", json::array({ 1.0f, 1.0f, 1.0f })));
    }

    if (j.contains("meshRenderer"))
    {
        auto& m = entity.AddComponent<MeshRendererComponent>();
        const auto& mj = j["meshRenderer"];
        m.ModelPath = mj.value("model", "");
        m.Tint = Vec4FromJson(mj.value("tint", json::array({ 1.0f, 1.0f, 1.0f, 1.0f })));
        m.Visible = mj.value("visible", true);
        m.CastShadows = mj.value("castShadows", true);
    }

    if (j.contains("camera"))
    {
        auto& c = entity.AddComponent<CameraComponent>();
        const auto& cj = j["camera"];
        c.FieldOfView = cj.value("fieldOfView", 60.0f);
        c.NearClip = cj.value("nearClip", 0.1f);
        c.FarClip = cj.value("farClip", 1000.0f);
        c.Primary = cj.value("primary", true);
    }

    if (j.contains("directionalLight"))
    {
        auto& l = entity.AddComponent<DirectionalLightComponent>();
        const auto& lj = j["directionalLight"];
        l.Direction = Vec3FromJson(lj.value("direction", json::array({ -0.5f, -1.0f, -0.3f })));
        l.Color = Vec3FromJson(lj.value("color", json::array({ 1.0f, 1.0f, 1.0f })));
        l.Intensity = lj.value("intensity", 3.0f);
        l.CastShadows = lj.value("castShadows", true);
    }

    if (j.contains("pointLight"))
    {
        auto& l = entity.AddComponent<PointLightComponent>();
        const auto& lj = j["pointLight"];
        l.Color = Vec3FromJson(lj.value("color", json::array({ 1.0f, 1.0f, 1.0f })));
        l.Intensity = lj.value("intensity", 1.0f);
        l.Range = lj.value("range", 10.0f);
    }

    if (j.contains("rigidBody"))
    {
        auto& r = entity.AddComponent<RigidBodyComponent>();
        const auto& rj = j["rigidBody"];
        r.Type = static_cast<BodyType>(rj.value("type", 0));
        r.Mass = rj.value("mass", 1.0f);
        r.Friction = rj.value("friction", 0.5f);
        r.Restitution = rj.value("restitution", 0.0f);
        r.UseGravity = rj.value("useGravity", true);
    }

    if (j.contains("boxCollider"))
        entity.AddComponent<BoxColliderComponent>().HalfExtents =
            Vec3FromJson(j["boxCollider"].value("halfExtents", json::array({ 0.5f, 0.5f, 0.5f })));

    if (j.contains("sphereCollider"))
        entity.AddComponent<SphereColliderComponent>().Radius = j["sphereCollider"].value("radius", 0.5f);

    if (j.contains("script"))
        entity.AddComponent<ScriptComponent>().ScriptPath = j["script"].value("path", "");

    if (j.contains("audioSource"))
    {
        auto& a = entity.AddComponent<AudioSourceComponent>();
        const auto& aj = j["audioSource"];
        a.ClipPath = aj.value("clip", "");
        a.Volume = aj.value("volume", 1.0f);
        a.Loop = aj.value("loop", false);
        a.PlayOnStart = aj.value("playOnStart", true);
        a.Spatial = aj.value("spatial", true);
    }

    if (j.contains("parent"))
    {
        const UUID parentId = UUID(std::stoull(j["parent"].get<std::string>(), nullptr, 16));
        entity.AddComponent<ParentComponent>().Parent = parentId;
    }

    if (j.contains("uuid"))
    {
        const UUID parsed(std::stoull(j["uuid"].get<std::string>(), nullptr, 16));
        if (entity.HasComponent<IDComponent>())
            entity.GetComponent<IDComponent>().ID = parsed;
        else
            entity.AddComponent<IDComponent>().ID = parsed;
    }

    return entity;
}

} // namespace

Scene::Scene(std::string name)
    : m_Name(std::move(name))
{
}

Scene::~Scene() = default;

Entity Scene::CreateEntity(const std::string& name)
{
    Entity entity = m_Registry.CreateEntity();
    entity.AddComponent<IDComponent>().ID = UUID::Generate();
    entity.AddComponent<TagComponent>().Name = name;
    entity.AddComponent<TransformComponent>();
    return entity;
}

void Scene::DestroyEntity(Entity entity)
{
    if (!entity.IsValid())
        return;

    // Destroy children first so no entity keeps a dangling parent link.
    std::vector<UUID> children;
    const UUID id = GetEntityUUID(entity);
    m_Registry.Each<ParentComponent>([&](Entity other, ParentComponent& parent)
    {
        if (parent.Parent == id)
            children.push_back(GetEntityUUID(other));
    });

    for (const UUID& childId : children)
    {
        Entity child = FindEntityByUUID(childId);
        if (child.IsValid())
            DestroyEntity(child);
    }

    m_Registry.DestroyEntity(entity);
}

Entity Scene::FindEntityByUUID(const UUID& id) const
{
    Entity found;
    const_cast<Registry&>(m_Registry).Each<IDComponent>([&](Entity entity, IDComponent& component)
    {
        if (component.ID == id)
            found = entity;
    });
    return found;
}

UUID Scene::GetEntityUUID(Entity entity) const
{
    if (!entity.IsValid() || !entity.HasComponent<IDComponent>())
        return UUID();
    return entity.GetComponent<IDComponent>().ID;
}

void Scene::SetParent(Entity child, Entity parent)
{
    if (!child.IsValid() || !parent.IsValid() || child == parent)
        return;

    // Reject cycles: walk up from the prospective parent.
    Entity cursor = parent;
    for (int depth = 0; depth < 256 && cursor.IsValid(); ++depth)
    {
        if (cursor == child)
            return;
        if (!cursor.HasComponent<ParentComponent>())
            break;
        cursor = FindEntityByUUID(cursor.GetComponent<ParentComponent>().Parent);
    }

    if (child.HasComponent<ParentComponent>())
        child.GetComponent<ParentComponent>().Parent = GetEntityUUID(parent);
    else
        child.AddComponent<ParentComponent>().Parent = GetEntityUUID(parent);
}

void Scene::ClearParent(Entity child)
{
    if (child.IsValid() && child.HasComponent<ParentComponent>())
        child.RemoveComponent<ParentComponent>();
}

void Scene::UpdateTransformRecursive(Entity entity, const glm::mat4& parentMatrix,
    std::unordered_map<EntityID, glm::mat4>& resolved)
{
    if (!entity.IsValid())
        return;

    const auto it = resolved.find(entity.GetID());
    if (it != resolved.end())
        return;

    glm::mat4 local(1.0f);
    if (entity.HasComponent<TransformComponent>())
        local = entity.GetComponent<TransformComponent>().GetLocalMatrix();

    const glm::mat4 world = parentMatrix * local;
    resolved[entity.GetID()] = world;

    if (entity.HasComponent<TransformComponent>())
        entity.GetComponent<TransformComponent>().WorldMatrix = world;

    const UUID id = GetEntityUUID(entity);
    m_Registry.Each<ParentComponent>([&](Entity child, ParentComponent& parent)
    {
        if (parent.Parent == id)
            UpdateTransformRecursive(child, world, resolved);
    });
}

void Scene::UpdateTransforms()
{
    std::unordered_map<EntityID, glm::mat4> resolved;
    m_Registry.Each<TransformComponent>([&](Entity entity, TransformComponent&)
    {
        if (!entity.HasComponent<ParentComponent>())
            UpdateTransformRecursive(entity, glm::mat4(1.0f), resolved);
    });

    // Entities whose parent is missing still get a world matrix.
    m_Registry.Each<TransformComponent>([&](Entity entity, TransformComponent& transform)
    {
        if (resolved.find(entity.GetID()) == resolved.end())
            transform.WorldMatrix = transform.GetLocalMatrix();
    });
}

Entity Scene::GetPrimaryCameraEntity() const
{
    Entity camera;
    const_cast<Registry&>(m_Registry).Each<CameraComponent>([&](Entity entity, CameraComponent& c)
    {
        if (!camera.IsValid() && c.Primary)
            camera = entity;
    });
    return camera;
}

CameraData Scene::GetCameraData(float aspectRatio) const
{
    CameraData data;
    Entity camera = GetPrimaryCameraEntity();
    if (!camera.IsValid())
        return data;

    const auto& c = camera.GetComponent<CameraComponent>();
    data.NearClip = c.NearClip;
    data.FarClip = c.FarClip;

    glm::vec3 position(0.0f);
    glm::mat4 world(1.0f);
    if (camera.HasComponent<TransformComponent>())
    {
        const auto& transform = camera.GetComponent<TransformComponent>();
        world = transform.WorldMatrix;
        position = glm::vec3(world[3]);
    }

    // Camera looks down its local -Z axis.
    const glm::vec3 forward = -glm::normalize(glm::vec3(world[2]));
    const glm::vec3 up = glm::normalize(glm::vec3(world[1]));

    data.Position = position;
    data.View = glm::lookAt(position, position + forward, up);
    data.Projection = glm::perspective(glm::radians(c.FieldOfView),
        aspectRatio > 0.0f ? aspectRatio : 1.0f, c.NearClip, c.FarClip);

    // NVRHI's Vulkan backend flips the viewport to D3D conventions, so NDC +Y
    // is up and no extra projection flip is needed.
    data.ViewProjection = data.Projection * data.View;
    return data;
}

void Scene::OnUpdate(double deltaTime)
{
    UpdateTransforms();
    (void)deltaTime;
}

bool Scene::Save(const std::string& path) const
{
    json root;
    root["name"] = m_Name;
    root["environment"] = {
        { "ambientColor", Vec3ToJson(m_Environment.AmbientColor) },
        { "ambientIntensity", m_Environment.AmbientIntensity },
        { "exposure", m_Environment.Exposure },
        { "hdriPath", m_Environment.HDRIPath },
        { "showSkybox", m_Environment.ShowSkybox }
    };

    json entities = json::array();
    const_cast<Registry&>(m_Registry).Each<IDComponent>([&](Entity entity, IDComponent&)
    {
        entities.push_back(SerializeEntityToJson(m_Registry, entity));
    });
    root["entities"] = std::move(entities);

    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    std::error_code error;
    if (resolved.has_parent_path())
        std::filesystem::create_directories(resolved.parent_path(), error);

    std::ofstream stream(resolved);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to save scene '{}'", resolved.string());
        return false;
    }

    stream << root.dump(2);
    SLATE_INFO("Saved scene '{}' ({} entities)", resolved.string(), GetEntityCount());
    return true;
}

bool Scene::Load(const std::string& path)
{
    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    std::ifstream stream(resolved);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to open scene '{}'", resolved.string());
        return false;
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception& exception)
    {
        SLATE_ERROR("Failed to parse scene '{}': {}", resolved.string(), exception.what());
        return false;
    }

    m_Name = root.value("name", "Untitled");

    if (root.contains("environment"))
    {
        const auto& env = root["environment"];
        m_Environment.AmbientColor = Vec3FromJson(env.value("ambientColor", json::array({ 0.1f, 0.1f, 0.12f })));
        m_Environment.AmbientIntensity = env.value("ambientIntensity", 0.3f);
        m_Environment.Exposure = env.value("exposure", 1.0f);
        m_Environment.HDRIPath = env.value("hdriPath", "");
        m_Environment.ShowSkybox = env.value("showSkybox", true);
    }

    // Rebuild the registry from scratch.
    m_Registry.Clear();

    if (root.contains("entities"))
    {
        for (const auto& entityJson : root["entities"])
            DeserializeEntityFromJson(*this, entityJson);
    }

    UpdateTransforms();
    SLATE_INFO("Loaded scene '{}' ({} entities)", resolved.string(), GetEntityCount());
    return true;
}

bool Scene::SavePrefab(Entity entity, const std::string& path) const
{
    if (!entity.IsValid())
        return false;

    json root;
    root["prefab"] = SerializeEntityToJson(m_Registry, entity);

    json children = json::array();
    const UUID id = GetEntityUUID(entity);
    const_cast<Registry&>(m_Registry).Each<ParentComponent>([&](Entity child, ParentComponent& parent)
    {
        if (parent.Parent == id)
            children.push_back(SerializeEntityToJson(m_Registry, child));
    });
    root["children"] = std::move(children);

    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    std::error_code error;
    if (resolved.has_parent_path())
        std::filesystem::create_directories(resolved.parent_path(), error);

    std::ofstream stream(resolved);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to save prefab '{}'", resolved.string());
        return false;
    }

    stream << root.dump(2);
    return true;
}

Entity Scene::InstantiatePrefab(const std::string& path, const glm::vec3& position)
{
    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    std::ifstream stream(resolved);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to open prefab '{}'", resolved.string());
        return Entity();
    }

    json root;
    try
    {
        stream >> root;
    }
    catch (const std::exception& exception)
    {
        SLATE_ERROR("Failed to parse prefab '{}': {}", resolved.string(), exception.what());
        return Entity();
    }

    if (!root.contains("prefab"))
        return Entity();

    Entity instance = DeserializeEntityFromJson(*this, root["prefab"]);
    if (!instance.IsValid())
        return Entity();

    if (instance.HasComponent<TransformComponent>())
        instance.GetComponent<TransformComponent>().Translation = position;

    if (root.contains("children"))
    {
        for (const auto& childJson : root["children"])
        {
            Entity child = DeserializeEntityFromJson(*this, childJson);
            if (child.IsValid())
                SetParent(child, instance);
        }
    }

    UpdateTransforms();
    return instance;
}

} // namespace Slate
