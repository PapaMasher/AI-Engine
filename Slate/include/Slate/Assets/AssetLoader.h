#pragma once

#include "Slate/Assets/AssetTypes.h"

namespace Slate {

// Internal loader entry points, exposed for unit testing.
Ref<Model> LoadModelFromFile(const std::string& path);
Ref<TextureData> LoadTextureFromFile(const std::string& path, bool isSRGB);
Ref<TextureData> LoadTextureFromMemory(const uint8_t* data, size_t size, bool isSRGB, const std::string& name);

} // namespace Slate
