#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "Slate/Core/ImageWriter.h"
#include "Slate/Core/Log.h"

namespace Slate::ImageWriter {

bool WritePng(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height)
{
    if (!rgba || width == 0 || height == 0)
        return false;

    const int result = stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height),
        4, rgba, static_cast<int>(width) * 4);

    if (result == 0)
    {
        SLATE_ERROR("Failed to write PNG '{}'", path);
        return false;
    }
    return true;
}

} // namespace Slate::ImageWriter
