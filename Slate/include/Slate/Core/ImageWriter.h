#pragma once

#include "Slate/Core/Base.h"

namespace Slate::ImageWriter {

// Writes an RGBA8 image to disk as PNG. Returns false on failure.
bool WritePng(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height);

} // namespace Slate::ImageWriter
