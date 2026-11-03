#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Core/UUID.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

namespace Slate {

// Stable identifier for an entity. Persisted in scenes so prefab instances and
// parent links survive save/load.
struct IDComponent
{
    UUID ID;
};

struct TagComponent
{
    std::string Name = "Entity";
};

struct TransformComponent
{
    glm::vec3 Translation { 0.0f };
    glm::vec3 Rotation { 0.0f }; // Euler angles in degrees, YXZ order
    glm::vec3 Scale { 1.0f };

    // Cached world matrix, recomputed by Scene::UpdateTransforms.
    glm::mat4 WorldMatrix { 1.0f };

    glm::mat4 GetLocalMatrix() const
    {
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, Translation);
        transform = glm::rotate(transform, glm::radians(Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, Scale);
        return transform;
    }
};

struct ParentComponent
{
    UUID Parent;
};

// Draws a model loaded from disk (glTF). Empty path means the entity has no
// visible geometry.
struct MeshRendererComponent
{
    std::string ModelPath;
    glm::vec4 Tint { 1.0f };
    bool Visible = true;
    bool CastShadows = true;
};

struct CameraComponent
{
    float FieldOfView = 60.0f;
    float NearClip = 0.1f;
    float FarClip = 1000.0f;
    bool Primary = true;
};

struct DirectionalLightComponent
{
    glm::vec3 Direction { -0.5f, -1.0f, -0.3f };
    glm::vec3 Color { 1.0f };
    float Intensity = 3.0f;
    bool CastShadows = true;
};

struct PointLightComponent
{
    glm::vec3 Color { 1.0f };
    float Intensity = 1.0f;
    float Range = 10.0f;
};

enum class BodyType
{
    Static = 0,
    Dynamic = 1,
    Kinematic = 2
};

struct RigidBodyComponent
{
    BodyType Type = BodyType::Static;
    float Mass = 1.0f;
    float Friction = 0.5f;
    float Restitution = 0.0f;
    bool UseGravity = true;
};

struct BoxColliderComponent
{
    glm::vec3 HalfExtents { 0.5f };
};

struct SphereColliderComponent
{
    float Radius = 0.5f;
};

struct ScriptComponent
{
    std::string ScriptPath;
};

struct AudioSourceComponent
{
    std::string ClipPath;
    float Volume = 1.0f;
    bool Loop = false;
    bool PlayOnStart = true;
    bool Spatial = true;
};

// Marks an entity as an instance of a prefab file.
struct PrefabInstanceComponent
{
    std::string PrefabPath;
};

} // namespace Slate
