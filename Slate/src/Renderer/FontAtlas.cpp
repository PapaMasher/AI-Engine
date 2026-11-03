#include "Slate/Renderer/FontAtlas.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>

namespace Slate {

bool FontAtlas::LoadFromFile(const std::string& path, float pixelHeight)
{
    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    const std::vector<uint8_t> data = FileSystem::ReadBinaryFile(resolved);
    if (data.empty())
    {
        SLATE_ERROR("Font '{}' could not be read", resolved.string());
        return false;
    }

    stbtt_fontinfo font;
    const int fontOffset = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (fontOffset < 0 || !stbtt_InitFont(&font, data.data(), fontOffset))
    {
        SLATE_ERROR("Font '{}' is not a valid TrueType file", resolved.string());
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);

    m_PixelHeight = pixelHeight;
    m_Ascent = static_cast<float>(ascent) * scale;
    m_LineHeight = static_cast<float>(ascent - descent + lineGap) * scale;

    // Single-channel coverage atlas, later expanded to RGBA.
    std::vector<uint8_t> coverage(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    coverage[0] = 255; // white texel used by solid rectangles

    int penX = 2;
    int penY = 2;
    int rowHeight = 0;

    for (uint32_t code = kFirstChar; code <= kLastChar; ++code)
    {
        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(code), &advance, &leftSideBearing);

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, static_cast<int>(code), scale, scale, &x0, &y0, &x1, &y1);

        const int width = x1 - x0;
        const int height = y1 - y0;

        if (penX + width + 2 >= static_cast<int>(kAtlasSize))
        {
            penX = 2;
            penY += rowHeight + 2;
            rowHeight = 0;
        }

        Glyph& glyph = m_Glyphs[code - kFirstChar];
        glyph.U0 = static_cast<float>(penX) / static_cast<float>(kAtlasSize);
        glyph.V0 = static_cast<float>(penY) / static_cast<float>(kAtlasSize);
        glyph.U1 = static_cast<float>(penX + width) / static_cast<float>(kAtlasSize);
        glyph.V1 = static_cast<float>(penY + height) / static_cast<float>(kAtlasSize);
        glyph.Width = static_cast<float>(width);
        glyph.Height = static_cast<float>(height);
        glyph.XOffset = static_cast<float>(x0);
        glyph.YOffset = static_cast<float>(y0);
        glyph.XAdvance = static_cast<float>(advance) * scale;

        if (width > 0 && height > 0)
        {
            stbtt_MakeCodepointBitmap(&font, coverage.data() + penY * kAtlasSize + penX,
                width, height, kAtlasSize, scale, scale, static_cast<int>(code));
        }

        penX += width + 2;
        rowHeight = std::max(rowHeight, height);
    }

    m_Pixels.resize(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4);
    for (size_t index = 0; index < coverage.size(); ++index)
    {
        m_Pixels[index * 4 + 0] = 255;
        m_Pixels[index * 4 + 1] = 255;
        m_Pixels[index * 4 + 2] = 255;
        m_Pixels[index * 4 + 3] = coverage[index];
    }

    m_WhiteU = 0.5f / static_cast<float>(kAtlasSize);
    m_WhiteV = 0.5f / static_cast<float>(kAtlasSize);

    SLATE_INFO("Font '{}' baked at {} px", resolved.filename().string(), pixelHeight);
    return true;
}

const FontAtlas::Glyph* FontAtlas::GetGlyph(char character) const
{
    const int code = static_cast<unsigned char>(character);
    if (code < static_cast<int>(kFirstChar) || code > static_cast<int>(kLastChar))
        return nullptr;

    return &m_Glyphs[static_cast<size_t>(code) - kFirstChar];
}

} // namespace Slate
