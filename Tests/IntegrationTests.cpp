#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Slate/Application.h"
#include "Slate/Core/Log.h"

#include <stb_image.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Renders into a hidden window so tests can run unattended.
class HeadlessApplication : public Slate::Application
{
public:
    explicit HeadlessApplication(std::string scenePath)
        : Application({ "SlateTest", 640, 360, false, false }, false)
        , m_ScenePath(std::move(scenePath))
    {
    }

protected:
    void OnStart() override
    {
        if (!GetScene().Load(m_ScenePath))
            SLATE_ERROR("Test scene failed to load: {}", m_ScenePath);
    }

private:
    std::string m_ScenePath;
};

struct PixelStats
{
    int Width = 0;
    int Height = 0;
    std::vector<unsigned char> Pixels;
};

PixelStats RenderScene(const std::string& scenePath, const std::string& screenshotName, int frames = 12)
{
    const std::filesystem::path screenshotPath =
        std::filesystem::temp_directory_path() / screenshotName;
    std::filesystem::remove(screenshotPath);

    const std::string frameCount = std::to_string(frames);

#if defined(_WIN32)
    _putenv_s("SLATE_MAX_FRAMES", frameCount.c_str());
    _putenv_s("SLATE_SCREENSHOT_PATH", screenshotPath.string().c_str());
#else
    setenv("SLATE_MAX_FRAMES", frameCount.c_str(), 1);
    setenv("SLATE_SCREENSHOT_PATH", screenshotPath.string().c_str(), 1);
#endif

    {
        HeadlessApplication application(scenePath);
        application.Run();
    }

    REQUIRE(std::filesystem::exists(screenshotPath));

    PixelStats stats;
    int channels = 0;
    unsigned char* pixels = stbi_load(screenshotPath.string().c_str(),
        &stats.Width, &stats.Height, &channels, 4);
    REQUIRE(pixels != nullptr);
    REQUIRE(stats.Width > 0);
    REQUIRE(stats.Height > 0);

    stats.Pixels.assign(pixels, pixels + stats.Width * stats.Height * 4);
    stbi_image_free(pixels);
    return stats;
}

double MeanLuminance(const PixelStats& stats, int centreX, int centreY, int radius)
{
    double sum = 0.0;
    int count = 0;

    for (int y = centreY - radius; y <= centreY + radius; ++y)
    {
        for (int x = centreX - radius; x <= centreX + radius; ++x)
        {
            if (x < 0 || y < 0 || x >= stats.Width || y >= stats.Height)
                continue;

            const unsigned char* pixel = &stats.Pixels[(y * stats.Width + x) * 4];
            sum += 0.2126 * pixel[0] + 0.7152 * pixel[1] + 0.0722 * pixel[2];
            ++count;
        }
    }

    return count > 0 ? sum / count : 0.0;
}

} // namespace

TEST_CASE("World up (+Y) renders above world down (-Y)")
{
    const PixelStats stats = RenderScene("Scenes/OrientationTest.json", "slate_orientation.png");

    double redSum = 0.0;
    double greenSum = 0.0;
    int redCount = 0;
    int greenCount = 0;

    for (int y = 0; y < stats.Height; ++y)
    {
        for (int x = 0; x < stats.Width; ++x)
        {
            const unsigned char* pixel = &stats.Pixels[(y * stats.Width + x) * 4];
            const int r = pixel[0];
            const int g = pixel[1];
            const int b = pixel[2];

            // Classify by dominant channel so shaded/textured faces still count.
            if (r > 40 && r > g + 30 && r > b + 30)
            {
                redSum += y;
                ++redCount;
            }
            else if (g > 40 && g > r + 30 && g > b + 30)
            {
                greenSum += y;
                ++greenCount;
            }
        }
    }

    // Both boxes must be visible and reasonably large in the frame.
    CHECK(redCount > 200);
    CHECK(greenCount > 200);

    // Screen y grows downward, so the red box's average row must be smaller.
    const double redCentre = redSum / static_cast<double>(redCount);
    const double greenCentre = greenSum / static_cast<double>(greenCount);
    CHECK(redCentre < greenCentre);
}

TEST_CASE("Cube front faces are visible, not inside-out")
{
    const PixelStats stats = RenderScene("Scenes/CubeFaceTest.json", "slate_cubeface.png");

    // The camera looks straight at a brightly lit white cube. A correctly
    // wound mesh shows the lit front face; reversed winding culls it and
    // exposes the unlit interior, which is far darker.
    const double luminance = MeanLuminance(stats, stats.Width / 2, stats.Height / 2, 20);
    CHECK(luminance > 120.0);
}
