#pragma once

#include "Slate/Core/Base.h"

#include <string>

namespace Slate {

class Scene;

struct ExportOptions
{
    std::string Name = "Game";
    std::string ScenePath = "Scenes/TestScene.json";
    std::string OutputDirectory = "Export";
};

// Packages the runtime executable, assets and the live scene into a
// distributable folder. Returns false and fills outError on failure.
bool ExportGame(Scene& scene, const ExportOptions& options, std::string& outError);

} // namespace Slate
