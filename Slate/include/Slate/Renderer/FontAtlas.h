#pragma once

#include "Slate/Core/Base.h"

#include <array>
#include <string>
#include <vector>

namespace Slate {

// Bakes an ASCII font atlas from a TTF with stb_truetype. The atlas stores
// coverage in the alpha channel and white in RGB so the same texture can be
// used for text and solid rectangles (texel 0,0 is fully white).
class FontAtlas
{
public:
    struct Glyph
    {
        float U0 = 0.0f;
        float V0 = 0.0f;
        float U1 = 0.0f;
        float V1 = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;
        float XOffset = 0.0f;
        float YOffset = 0.0f;
        float XAdvance = 0.0f;
    };

    bool LoadFromFile(const std::string& path, float pixelHeight = 48.0f);

    const Glyph* GetGlyph(char character) const;

    float GetPixelHeight() const { return m_PixelHeight; }
    float GetAscent() const { return m_Ascent; }
    float GetLineHeight() const { return m_LineHeight; }
    float GetWhiteU() const { return m_WhiteU; }
    float GetWhiteV() const { return m_WhiteV; }

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }

private:
    static constexpr uint32_t kFirstChar = 32;
    static constexpr uint32_t kLastChar = 126;
    static constexpr uint32_t kAtlasSize = 512;

    std::array<Glyph, kLastChar - kFirstChar + 1> m_Glyphs {};
    std::vector<uint8_t> m_Pixels;
    uint32_t m_Width = kAtlasSize;
    uint32_t m_Height = kAtlasSize;
    float m_PixelHeight = 48.0f;
    float m_Ascent = 0.0f;
    float m_LineHeight = 0.0f;
    float m_WhiteU = 0.0f;
    float m_WhiteV = 0.0f;
};

} // namespace Slate
