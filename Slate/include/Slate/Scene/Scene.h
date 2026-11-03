#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Scene/ECS.h"
#include "Slate/Scene/Components.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

namespace Slate {

// Scene environment settings shared by the renderer.
struct Environment
{
    glm::vec3 AmbientColor { 0.1f, 0.1f, 0.12f };
    float AmbientIntensity = 0.3f;
    float Exposure = 1.0f;
    std::string HDRIPath;
    bool ShowSkybox = true;
};

// Camera parameters extracted from the primary camera entity.
struct CameraData
{
    glm::mat4 View { 1.0f };
    glm::mat4 Projection { 1.0f };
    glm::mat4 ViewProjection { 1.0f };
    glm::vec3 Position { 0.0f };
    float NearClip = 0.1f;
    float FarClip = 1000.0f;
};

// A scene owns the entity registry, environment settings and hierarchy.
class Scene
{
public:
    explicit Scene(std::string name = "Untitled");
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    const std::string& GetName() const { return m_Name; }
    void SetName(std::string name) { m_Name = std::move(name); }

    Registry& GetRegistry() { return m_Registry; }
    const Registry& GetRegistry() const { return m_Registry; }

    Environment& GetEnvironment() { return m_Environment; }
    const Environment& GetEnvironment() const { return m_Environment; }

    Entity CreateEntity(const std::string& name = "Entity");
    void DestroyEntity(Entity entity);

    Entity FindEntityByUUID(const UUID& id) const;
    UUID GetEntityUUID(Entity entity) const;

    // Sets the parent of `child` to `parent`. Rejects cycles.
    void SetParent(Entity child, Entity parent);
    void ClearParent(Entity child);

    // Recomputes world matrices for every entity, respecting hierarchy.
    void UpdateTransforms();

    Entity GetPrimaryCameraEntity() const;
    CameraData GetCameraData(float aspectRatio) const;

    void OnUpdate(double deltaTime);

    // Scene serialization (JSON).
    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

    // Prefabs: save an entity subtree, instantiate it later.
    bool SavePrefab(Entity entity, const std::string& path) const;
    Entity InstantiatePrefab(const std::string& path, const glm::vec3& position = glm::vec3(0.0f));

    size_t GetEntityCount() const { return m_Registry.GetEntityCount(); }

private:
    void UpdateTransformRecursive(Entity entity, const glm::mat4& parentMatrix,
        std::unordered_map<EntityID, glm::mat4>& resolved);

    std::string m_Name;
    Environment m_Environment;
    Registry m_Registry;
};

} // namespace Slate
