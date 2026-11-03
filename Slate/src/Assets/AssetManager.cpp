#include "Slate/Assets/AssetManager.h"
#include "Slate/Assets/AssetLoader.h"
#include "Slate/Core/Log.h"

namespace Slate {

Ref<Model> AssetManager::LoadModel(const std::string& path)
{
    std::lock_guard lock(m_Mutex);

    const auto it = m_Models.find(path);
    if (it != m_Models.end())
        return it->second;

    Ref<Model> model = LoadModelFromFile(path);
    if (model)
        m_Models[path] = model;
    return model;
}

Ref<TextureData> AssetManager::LoadTexture(const std::string& path, bool isSRGB)
{
    std::lock_guard lock(m_Mutex);

    const auto it = m_Textures.find(path);
    if (it != m_Textures.end())
        return it->second;

    Ref<TextureData> texture = LoadTextureFromFile(path, isSRGB);
    if (texture)
        m_Textures[path] = texture;
    return texture;
}

Ref<TextureData> AssetManager::CreateTextureFromPixels(uint32_t width, uint32_t height, std::vector<uint8_t> pixels,
    bool isSRGB, const std::string& name)
{
    auto texture = MakeRef<TextureData>();
    texture->Width = width;
    texture->Height = height;
    texture->Channels = 4;
    texture->IsSRGB = isSRGB;
    texture->Pixels = std::move(pixels);
    texture->Source = name;

    std::lock_guard lock(m_Mutex);
    m_Textures[name] = texture;
    return texture;
}

void AssetManager::Clear()
{
    std::lock_guard lock(m_Mutex);
    m_Models.clear();
    m_Textures.clear();
}

size_t AssetManager::GetModelCount() const
{
    std::lock_guard lock(m_Mutex);
    return m_Models.size();
}

size_t AssetManager::GetTextureCount() const
{
    std::lock_guard lock(m_Mutex);
    return m_Textures.size();
}

} // namespace Slate
