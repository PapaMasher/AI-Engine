#pragma once

#include "Slate/Assets/AssetTypes.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace Slate {

// Loads and caches CPU-side assets from the asset root. GPU resources are
// created by the renderer from these objects.
class AssetManager
{
public:
    AssetManager() = default;
    ~AssetManager() = default;

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Loads a glTF/GLB model, applying node transforms to vertices. Returns
    // nullptr on failure (errors are logged).
    Ref<Model> LoadModel(const std::string& path);

    // Loads a PNG/JPG/HDR image. Returns nullptr on failure.
    Ref<TextureData> LoadTexture(const std::string& path, bool isSRGB = true);

    // Creates a texture from raw RGBA8 pixels (used for procedural content and
    // editor thumbnails).
    Ref<TextureData> CreateTextureFromPixels(uint32_t width, uint32_t height, std::vector<uint8_t> pixels,
        bool isSRGB = true, const std::string& name = "<memory>");

    void Clear();

    size_t GetModelCount() const;
    size_t GetTextureCount() const;

private:
    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, Ref<Model>> m_Models;
    std::unordered_map<std::string, Ref<TextureData>> m_Textures;
};

} // namespace Slate
