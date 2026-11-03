#include "Slate/Application.h"
#include "Slate/Core/Log.h"
#include "Slate/Core/CrashHandler.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/CommandLine.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Resolution order: command line, SLATE_SCENE, game.json manifest, default.
std::string ResolveScenePath(const Slate::CommandLineOptions& options)
{
    if (!options.ScenePath.empty())
        return options.ScenePath;

    if (const char* environmentScene = std::getenv("SLATE_SCENE");
        environmentScene != nullptr && *environmentScene != '\0')
    {
        return environmentScene;
    }

    // Exported games carry a game.json manifest next to the executable.
    const std::filesystem::path manifestPath =
        Slate::FileSystem::GetExecutableDirectory() / "game.json";
    if (Slate::FileSystem::Exists(manifestPath))
    {
        if (const auto manifest = Slate::FileSystem::ReadTextFile(manifestPath))
        {
            try
            {
                return nlohmann::json::parse(*manifest).value("scene", "Scenes/TestScene.json");
            }
            catch (const std::exception& exception)
            {
                SLATE_WARN("Failed to parse game.json: {}", exception.what());
            }
        }
    }

    return "Scenes/TestScene.json";
}

class SlateRuntime : public Slate::Application
{
public:
    explicit SlateRuntime(std::string scenePath)
        : Application({ "Slate Runtime", 1280, 720 }, false)
        , m_ScenePath(std::move(scenePath))
    {
    }

protected:
    void OnStart() override
    {
        SLATE_INFO("Slate runtime started; scene: {}", m_ScenePath);

        if (!GetScene().Load(m_ScenePath))
            SLATE_ERROR("Failed to load scene '{}'", m_ScenePath);

        SetPlaying(true);
    }

private:
    std::string m_ScenePath;
};

} // namespace

int main(int argc, char** argv)
{
    const Slate::CommandLineOptions options = Slate::ParseCommandLine(argc, argv);
    if (options.ShowHelp)
    {
        Slate::PrintUsage(std::cout, "Slate-Runtime",
            "Runs a Slate scene, or an exported game via its game.json manifest.");
        return 0;
    }

    if (!options.AssetRoot.empty())
        Slate::FileSystem::SetAssetRoot(options.AssetRoot);

    Slate::Log::Init("Slate-Runtime.log");
    Slate::InstallCrashHandler();

    {
        SlateRuntime runtime(ResolveScenePath(options));
        runtime.Run();
    }

    Slate::Log::Shutdown();
    return 0;
}
