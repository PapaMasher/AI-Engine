#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Slate/Assets/AssetLoader.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <cstring>

namespace Slate {

namespace {

bool HasHdrExtension(const std::string& path)
{
    const std::string ext = FileSystem::GetExtension(path);
    return ext == ".hdr" || ext == ".HDR";
}

} // namespace

Ref<TextureData> LoadTextureFromMemory(const uint8_t* data, size_t size, bool isSRGB, const std::string& name)
{
    if (!data || size == 0)
        return nullptr;

    auto texture = MakeRef<TextureData>();
    texture->Source = name;
    texture->IsSRGB = isSRGB;

    int width = 0;
    int height = 0;
    int channels = 0;

    if (stbi_is_hdr_from_memory(data, static_cast<int>(size)))
    {
        float* pixels = stbi_loadf_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
        if (!pixels)
        {
            SLATE_ERROR("Failed to decode HDR image '{}': {}", name, stbi_failure_reason());
            return nullptr;
        }

        texture->IsHDR = true;
        texture->IsSRGB = false;
        texture->Width = static_cast<uint32_t>(width);
        texture->Height = static_cast<uint32_t>(height);
        texture->Channels = 4;
        texture->PixelsFloat.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return texture;
    }

    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels)
    {
        SLATE_ERROR("Failed to decode image '{}': {}", name, stbi_failure_reason());
        return nullptr;
    }

    texture->Width = static_cast<uint32_t>(width);
    texture->Height = static_cast<uint32_t>(height);
    texture->Channels = 4;
    texture->Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return texture;
}

Ref<TextureData> LoadTextureFromFile(const std::string& path, bool isSRGB)
{
    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    if (!FileSystem::Exists(resolved))
    {
        SLATE_ERROR("Texture not found: {}", resolved.string());
        return nullptr;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    if (HasHdrExtension(path))
    {
        float* pixels = stbi_loadf(resolved.string().c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            SLATE_ERROR("Failed to load HDR texture '{}': {}", resolved.string(), stbi_failure_reason());
            return nullptr;
        }

        auto texture = MakeRef<TextureData>();
        texture->Source = path;
        texture->IsHDR = true;
        texture->IsSRGB = false;
        texture->Width = static_cast<uint32_t>(width);
        texture->Height = static_cast<uint32_t>(height);
        texture->Channels = 4;
        texture->PixelsFloat.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return texture;
    }

    uint8_t* pixels = stbi_load(resolved.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        SLATE_ERROR("Failed to load texture '{}': {}", resolved.string(), stbi_failure_reason());
        return nullptr;
    }

    auto texture = MakeRef<TextureData>();
    texture->Source = path;
    texture->IsSRGB = isSRGB;
    texture->Width = static_cast<uint32_t>(width);
    texture->Height = static_cast<uint32_t>(height);
    texture->Channels = 4;
    texture->Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return texture;
}

} // namespace Slate
