#include "Slate/Application.h"
#include "Slate/Core/Log.h"
#include "Slate/Core/CrashHandler.h"
#include "Slate/Core/FileSystem.h"

#include <nlohmann/json.hpp>

#include <cstdlib>

namespace {

class SlateRuntime : public Slate::Application
{
public:
    SlateRuntime()
        : Application({ "Slate Runtime", 1280, 720 }, false)
    {
    }

protected:
    void OnStart() override
    {
        SLATE_INFO("Slate runtime started");

        std::string scenePath = "Scenes/TestScene.json";

        // Exported games carry a game.json manifest next to the executable.
        const std::filesystem::path manifestPath =
            Slate::FileSystem::GetExecutableDirectory() / "game.json";
        if (Slate::FileSystem::Exists(manifestPath))
        {
            if (const auto manifest = Slate::FileSystem::ReadTextFile(manifestPath))
            {
                try
                {
                    const nlohmann::json json = nlohmann::json::parse(*manifest);
                    scenePath = json.value("scene", scenePath);
                }
                catch (const std::exception& exception)
                {
                    SLATE_WARN("Failed to parse game.json: {}", exception.what());
                }
            }
        }

        if (const char* envScene = std::getenv("SLATE_SCENE"))
            scenePath = envScene;

        if (!GetScene().Load(scenePath))
            SLATE_ERROR("Failed to load scene '{}'", scenePath);

        SetPlaying(true);
    }
};

} // namespace

int main()
{
    Slate::Log::Init("Slate-Runtime.log");
    Slate::InstallCrashHandler();

    {
        SlateRuntime runtime;
        runtime.Run();
    }

    Slate::Log::Shutdown();
    return 0;
}
