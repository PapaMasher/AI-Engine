#include "Slate/Editor/GameExporter.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"
#include "Slate/Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace Slate {

bool ExportGame(Scene& scene, const ExportOptions& options, std::string& outError)
{
    namespace fs = std::filesystem;

    const fs::path assetRoot = FileSystem::GetAssetRoot();
    const fs::path executableDirectory = FileSystem::GetExecutableDirectory();
    const fs::path target = fs::absolute(fs::path(options.OutputDirectory) / options.Name);

    if (!FileSystem::Exists(assetRoot))
    {
        outError = "Asset root does not exist: " + assetRoot.string();
        return false;
    }

    const fs::path runtimeExecutable = executableDirectory / "Slate-Runtime.exe";
    if (!FileSystem::Exists(runtimeExecutable))
    {
        outError = "Runtime executable not found: " + runtimeExecutable.string();
        return false;
    }

    std::error_code error;
    fs::create_directories(target, error);
    fs::create_directories(target / "Assets", error);

    fs::copy(assetRoot, target / "Assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    if (error)
    {
        outError = "Failed to copy assets: " + error.message();
        return false;
    }

    fs::copy_file(runtimeExecutable, target / "Slate-Runtime.exe",
        fs::copy_options::overwrite_existing, error);
    if (error)
    {
        outError = "Failed to copy runtime executable: " + error.message();
        return false;
    }

    // Serialize the live scene so runtime edits are included in the export.
    const fs::path sceneTarget = target / "Assets" / "Scenes" / (options.Name + ".json");
    fs::create_directories(sceneTarget.parent_path(), error);
    if (!scene.Save(sceneTarget.string()))
    {
        outError = "Failed to save scene to " + sceneTarget.string();
        return false;
    }

    nlohmann::json manifest;
    manifest["name"] = options.Name;
    manifest["scene"] = "Scenes/" + options.Name + ".json";
    manifest["engine"] = "Slate";
    manifest["version"] = "0.1.0";

    std::ofstream stream(target / "game.json");
    if (!stream.is_open())
    {
        outError = "Failed to write game.json";
        return false;
    }
    stream << manifest.dump(2);

    SLATE_INFO("Exported '{}' to '{}'", options.Name, target.string());
    return true;
}

} // namespace Slate
