#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "Slate/Assets/AssetLoader.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <unordered_map>

namespace Slate {

namespace {

struct LoadContext
{
    std::string BaseDirectory;
    Model* ModelPtr = nullptr;
    std::unordered_map<std::string, int32_t> TextureIndices;
};

int32_t LoadMaterialTexture(LoadContext& context, const cgltf_texture_view& view, bool isSRGB)
{
    if (!view.texture || !view.texture->image)
        return -1;

    const cgltf_image* image = view.texture->image;
    std::string key;
    if (image->uri)
        key = image->uri;
    else if (image->name)
        key = image->name;
    else if (image->buffer_view)
        key = "<embedded:" + std::to_string(image->buffer_view->offset) + ":" + std::to_string(image->buffer_view->size) + ">";
    else
        key = "<embedded>";

    const auto existing = context.TextureIndices.find(key);
    if (existing != context.TextureIndices.end())
        return existing->second;

    Ref<TextureData> texture;

    if (image->buffer_view && image->buffer_view->buffer->data)
    {
        const uint8_t* data = static_cast<const uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
        texture = LoadTextureFromMemory(data, image->buffer_view->size, isSRGB, key);
    }
    else if (image->uri && std::strncmp(image->uri, "data:", 5) != 0)
    {
        texture = LoadTextureFromFile((std::filesystem::path(context.BaseDirectory) / image->uri).string(), isSRGB);
    }

    if (!texture)
    {
        SLATE_WARN("Could not load texture '{}' for model '{}'", key, context.ModelPtr->Path);
        return -1;
    }

    const int32_t index = static_cast<int32_t>(context.ModelPtr->Textures.size());
    context.ModelPtr->Textures.push_back(texture);
    context.TextureIndices[key] = index;
    return index;
}

void ComputeNormals(Mesh& mesh)
{
    for (auto& vertex : mesh.Vertices)
        vertex.Normal = glm::vec3(0.0f);

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        Vertex& v0 = mesh.Vertices[mesh.Indices[i]];
        Vertex& v1 = mesh.Vertices[mesh.Indices[i + 1]];
        Vertex& v2 = mesh.Vertices[mesh.Indices[i + 2]];

        const glm::vec3 normal = glm::cross(v1.Position - v0.Position, v2.Position - v0.Position);
        v0.Normal += normal;
        v1.Normal += normal;
        v2.Normal += normal;
    }

    for (auto& vertex : mesh.Vertices)
    {
        if (glm::length(vertex.Normal) > 1e-8f)
            vertex.Normal = glm::normalize(vertex.Normal);
        else
            vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

void ComputeTangents(Mesh& mesh)
{
    std::vector<glm::vec3> tangents(mesh.Vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(mesh.Vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
    {
        Vertex& v0 = mesh.Vertices[mesh.Indices[i]];
        Vertex& v1 = mesh.Vertices[mesh.Indices[i + 1]];
        Vertex& v2 = mesh.Vertices[mesh.Indices[i + 2]];

        const glm::vec3 edge1 = v1.Position - v0.Position;
        const glm::vec3 edge2 = v2.Position - v0.Position;
        const glm::vec2 deltaUV1 = v1.TexCoord - v0.TexCoord;
        const glm::vec2 deltaUV2 = v2.TexCoord - v0.TexCoord;

        const float determinant = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(determinant) < 1e-8f)
            continue;

        const float reciprocal = 1.0f / determinant;
        const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * reciprocal;
        const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * reciprocal;

        tangents[mesh.Indices[i]] += tangent;
        tangents[mesh.Indices[i + 1]] += tangent;
        tangents[mesh.Indices[i + 2]] += tangent;

        bitangents[mesh.Indices[i]] += bitangent;
        bitangents[mesh.Indices[i + 1]] += bitangent;
        bitangents[mesh.Indices[i + 2]] += bitangent;
    }

    for (size_t i = 0; i < mesh.Vertices.size(); ++i)
    {
        Vertex& vertex = mesh.Vertices[i];
        glm::vec3 tangent = tangents[i];
        if (glm::length(tangent) < 1e-8f)
        {
            // Degenerate UVs: build an arbitrary orthogonal tangent frame.
            const glm::vec3 up = std::abs(vertex.Normal.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            tangent = glm::normalize(glm::cross(up, vertex.Normal));
        }
        else
        {
            tangent = glm::normalize(tangent);
        }

        const float handedness = glm::dot(glm::cross(vertex.Normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
        vertex.Tangent = glm::vec4(tangent, handedness);
    }
}

void ComputeBounds(Mesh& mesh)
{
    if (mesh.Vertices.empty())
        return;

    mesh.BoundsMin = mesh.Vertices[0].Position;
    mesh.BoundsMax = mesh.Vertices[0].Position;

    for (const auto& vertex : mesh.Vertices)
    {
        mesh.BoundsMin = glm::min(mesh.BoundsMin, vertex.Position);
        mesh.BoundsMax = glm::max(mesh.BoundsMax, vertex.Position);
    }
}

int32_t LoadMaterial(LoadContext& context, const cgltf_material* material)
{
    if (!material)
        return -1;

    Material result;
    result.Name = material->name ? material->name : "Material";

    if (material->has_pbr_metallic_roughness)
    {
        const auto& pbr = material->pbr_metallic_roughness;
        result.BaseColor = glm::vec4(pbr.base_color_factor[0], pbr.base_color_factor[1],
            pbr.base_color_factor[2], pbr.base_color_factor[3]);
        result.Metallic = pbr.metallic_factor;
        result.Roughness = pbr.roughness_factor;
        result.AlbedoTexture = LoadMaterialTexture(context, pbr.base_color_texture, true);
        result.MetallicRoughnessTexture = LoadMaterialTexture(context, pbr.metallic_roughness_texture, false);
    }

    result.Emissive = glm::vec3(material->emissive_factor[0], material->emissive_factor[1], material->emissive_factor[2]);
    result.NormalTexture = LoadMaterialTexture(context, material->normal_texture, false);
    result.EmissiveTexture = LoadMaterialTexture(context, material->emissive_texture, true);
    result.OcclusionTexture = LoadMaterialTexture(context, material->occlusion_texture, false);

    const int32_t index = static_cast<int32_t>(context.ModelPtr->Materials.size());
    context.ModelPtr->Materials.push_back(std::move(result));
    return index;
}

void LoadPrimitive(LoadContext& context, const cgltf_primitive& primitive, const glm::mat4& worldTransform)
{
    if (primitive.type != cgltf_primitive_type_triangles)
    {
        SLATE_WARN("Skipping non-triangle primitive in '{}'", context.ModelPtr->Path);
        return;
    }

    const cgltf_accessor* positionAccessor = nullptr;
    const cgltf_accessor* normalAccessor = nullptr;
    const cgltf_accessor* tangentAccessor = nullptr;
    const cgltf_accessor* texcoordAccessor = nullptr;

    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
    {
        const cgltf_attribute& attribute = primitive.attributes[i];
        switch (attribute.type)
        {
            case cgltf_attribute_type_position: positionAccessor = attribute.data; break;
            case cgltf_attribute_type_normal: normalAccessor = attribute.data; break;
            case cgltf_attribute_type_tangent: tangentAccessor = attribute.data; break;
            case cgltf_attribute_type_texcoord: if (attribute.index == 0) texcoordAccessor = attribute.data; break;
            default: break;
        }
    }

    if (!positionAccessor || positionAccessor->count == 0)
        return;

    Mesh mesh;
    mesh.Vertices.resize(positionAccessor->count);

    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    for (cgltf_size i = 0; i < positionAccessor->count; ++i)
    {
        Vertex& vertex = mesh.Vertices[i];

        float position[3] = { 0.0f, 0.0f, 0.0f };
        cgltf_accessor_read_float(positionAccessor, i, position, 3);
        const glm::vec4 worldPosition = worldTransform * glm::vec4(position[0], position[1], position[2], 1.0f);
        vertex.Position = glm::vec3(worldPosition);

        if (normalAccessor)
        {
            float normal[3] = { 0.0f, 0.0f, 0.0f };
            cgltf_accessor_read_float(normalAccessor, i, normal, 3);
            vertex.Normal = glm::normalize(normalMatrix * glm::vec3(normal[0], normal[1], normal[2]));
        }

        if (tangentAccessor)
        {
            float tangent[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
            cgltf_accessor_read_float(tangentAccessor, i, tangent, 4);
            const glm::vec3 direction = glm::normalize(glm::mat3(worldTransform) * glm::vec3(tangent[0], tangent[1], tangent[2]));
            vertex.Tangent = glm::vec4(direction, tangent[3]);
        }

        if (texcoordAccessor)
        {
            float texcoord[2] = { 0.0f, 0.0f };
            cgltf_accessor_read_float(texcoordAccessor, i, texcoord, 2);
            vertex.TexCoord = glm::vec2(texcoord[0], texcoord[1]);
        }
    }

    if (primitive.indices)
    {
        mesh.Indices.resize(primitive.indices->count);
        for (cgltf_size i = 0; i < primitive.indices->count; ++i)
            mesh.Indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
    }
    else
    {
        mesh.Indices.resize(positionAccessor->count);
        for (cgltf_size i = 0; i < positionAccessor->count; ++i)
            mesh.Indices[i] = static_cast<uint32_t>(i);
    }

    if (!normalAccessor)
        ComputeNormals(mesh);
    if (!tangentAccessor)
        ComputeTangents(mesh);

    ComputeBounds(mesh);

    Submesh submesh;
    submesh.FirstIndex = 0;
    submesh.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
    submesh.MaterialIndex = LoadMaterial(context, primitive.material);
    mesh.Submeshes.push_back(submesh);

    context.ModelPtr->Meshes.push_back(std::move(mesh));
}

void LoadNode(LoadContext& context, const cgltf_node* node)
{
    if (node->mesh)
    {
        float worldMatrix[16];
        cgltf_node_transform_world(node, worldMatrix);
        const glm::mat4 world = glm::make_mat4(worldMatrix);

        for (cgltf_size i = 0; i < node->mesh->primitives_count; ++i)
            LoadPrimitive(context, node->mesh->primitives[i], world);
    }

    for (cgltf_size i = 0; i < node->children_count; ++i)
        LoadNode(context, node->children[i]);
}

} // namespace

bool IsModelValid(const Model& model)
{
    if (model.Meshes.empty())
        return false;

    for (const auto& mesh : model.Meshes)
    {
        if (mesh.Vertices.empty() || mesh.Indices.empty())
            return false;
    }
    return true;
}

Ref<Model> LoadModelFromFile(const std::string& path)
{
    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    if (!FileSystem::Exists(resolved))
    {
        SLATE_ERROR("Model not found: {}", resolved.string());
        return nullptr;
    }

    cgltf_options options {};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, resolved.string().c_str(), &data);
    if (result != cgltf_result_success)
    {
        SLATE_ERROR("Failed to parse glTF '{}' (cgltf result {})", resolved.string(), static_cast<int>(result));
        return nullptr;
    }

    result = cgltf_load_buffers(&options, data, resolved.string().c_str());
    if (result != cgltf_result_success)
    {
        SLATE_ERROR("Failed to load glTF buffers for '{}' (cgltf result {})", resolved.string(), static_cast<int>(result));
        cgltf_free(data);
        return nullptr;
    }

    if (cgltf_validate(data) != cgltf_result_success)
        SLATE_WARN("glTF validation reported issues for '{}'", resolved.string());

    auto model = MakeRef<Model>();
    model->Path = path;

    LoadContext context;
    context.BaseDirectory = resolved.parent_path().string();
    context.ModelPtr = model.get();

    if (data->scene)
    {
        for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
            LoadNode(context, data->scene->nodes[i]);
    }
    else
    {
        // No scene: load every mesh with an identity transform.
        for (cgltf_size i = 0; i < data->meshes_count; ++i)
        {
            for (cgltf_size j = 0; j < data->meshes[i].primitives_count; ++j)
                LoadPrimitive(context, data->meshes[i].primitives[j], glm::mat4(1.0f));
        }
    }

    cgltf_free(data);

    if (!IsModelValid(*model))
    {
        SLATE_ERROR("Model '{}' contains no drawable geometry", path);
        return nullptr;
    }

    SLATE_INFO("Loaded model '{}': {} meshes, {} materials, {} textures",
        path, model->Meshes.size(), model->Materials.size(), model->Textures.size());

    return model;
}

} // namespace Slate
