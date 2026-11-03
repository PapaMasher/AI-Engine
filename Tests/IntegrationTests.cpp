#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Slate/Application.h"
#include "Slate/Core/Log.h"

#include <stb_image.h>

#include <cstdlib>
#include <filesystem>

namespace {

// Renders into a hidden window so the test can run unattended.
class HeadlessApplication : public Slate::Application
{
public:
    HeadlessApplication()
        : Application({ "SlateOrientationTest", 640, 360, false, false }, false)
    {
    }

protected:
    void OnStart() override
    {
        if (!GetScene().Load("Scenes/OrientationTest.json"))
            SLATE_ERROR("Orientation test scene failed to load");
    }
};

} // namespace

TEST_CASE("World up (+Y) renders above world down (-Y)")
{
    const std::filesystem::path screenshotPath =
        std::filesystem::temp_directory_path() / "slate_orientation.png";
    std::filesystem::remove(screenshotPath);

#if defined(_WIN32)
    _putenv_s("SLATE_MAX_FRAMES", "12");
    _putenv_s("SLATE_SCREENSHOT_PATH", screenshotPath.string().c_str());
#else
    setenv("SLATE_MAX_FRAMES", "12", 1);
    setenv("SLATE_SCREENSHOT_PATH", screenshotPath.string().c_str(), 1);
#endif

    {
        HeadlessApplication application;
        application.Run();
    }

    REQUIRE(std::filesystem::exists(screenshotPath));

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(screenshotPath.string().c_str(), &width, &height, &channels, 4);
    REQUIRE(pixels != nullptr);
    REQUIRE(width > 0);
    REQUIRE(height > 0);

    double redSum = 0.0;
    double greenSum = 0.0;
    int redCount = 0;
    int greenCount = 0;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const unsigned char* pixel = pixels + (y * width + x) * 4;
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

    stbi_image_free(pixels);
    // Screenshot kept for inspection when the test fails.

    // Both boxes must be visible and reasonably large in the frame.
    CHECK(redCount > 200);
    CHECK(greenCount > 200);

    // Screen y grows downward, so the red box's average row must be smaller.
    const double redCenter = redSum / static_cast<double>(redCount);
    const double greenCenter = greenSum / static_cast<double>(greenCount);
    CHECK(redCenter < greenCenter);
}
