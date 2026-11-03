#pragma once

#include "Slate/Core/Base.h"

#include <glm/glm.hpp>

#include <unordered_map>
#include <vector>

namespace Slate {

// Interleaved vertex layout used by every mesh in the engine. Tangent is a
// four-component vector: xyz is the tangent direction and w is the bitangent
// sign used by normal mapping.
struct Vertex
{
    glm::vec3 Position { 0.0f };
    glm::vec3 Normal { 0.0f, 1.0f, 0.0f };
    glm::vec4 Tangent { 1.0f, 0.0f, 0.0f, 1.0f };
    glm::vec2 TexCoord { 0.0f };
};

struct Submesh
{
    uint32_t FirstIndex = 0;
    uint32_t IndexCount = 0;
    int32_t MaterialIndex = -1;
};

struct Mesh
{
    std::vector<Vertex> Vertices;
    std::vector<uint32_t> Indices;
    std::vector<Submesh> Submeshes;
    glm::vec3 BoundsMin { 0.0f };
    glm::vec3 BoundsMax { 0.0f };
};

// Physically based material parameters following the glTF 2.0 metallic-
// roughness workflow. Texture indices refer to Model::Textures.
struct Material
{
    std::string Name;
    glm::vec4 BaseColor { 1.0f };
    float Metallic = 0.0f;
    float Roughness = 1.0f;
    glm::vec3 Emissive { 0.0f };
    float EmissiveStrength = 1.0f;

    int32_t AlbedoTexture = -1;
    int32_t NormalTexture = -1;
    int32_t MetallicRoughnessTexture = -1;
    int32_t EmissiveTexture = -1;
    int32_t OcclusionTexture = -1;
};

// CPU-side texture pixels. HDR images keep floating-point values; LDR images
// keep 8-bit RGBA pixels. `IsSRGB` records the color space the image encodes so
// the renderer can choose the matching GPU format.
struct TextureData
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Channels = 0;
    bool IsHDR = false;
    bool IsSRGB = false;
    std::vector<uint8_t> Pixels;
    std::vector<float> PixelsFloat;
    std::string Source;
};

struct Model
{
    std::string Path;
    std::vector<Mesh> Meshes;
    std::vector<Material> Materials;
    std::vector<Ref<TextureData>> Textures;
};

// Returns true when the model has at least one drawable mesh.
bool IsModelValid(const Model& model);

} // namespace Slate
