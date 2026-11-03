#include <doctest/doctest.h>

#include "Slate/Core/CommandLine.h"

#include <initializer_list>
#include <vector>

using namespace Slate;

namespace {

CommandLineOptions Parse(std::initializer_list<const char*> arguments)
{
    std::vector<const char*> argv;
    argv.push_back("Slate");
    for (const char* argument : arguments)
        argv.push_back(argument);

    return ParseCommandLine(static_cast<int>(argv.size()), const_cast<char**>(argv.data()));
}

} // namespace

TEST_CASE("Positional scene argument is parsed")
{
    const CommandLineOptions options = Parse({ "Scenes/SlateBlocks.json" });
    CHECK(options.ScenePath == "Scenes/SlateBlocks.json");
    CHECK(options.AssetRoot.empty());
    CHECK_FALSE(options.ShowHelp);
}

TEST_CASE("--scene takes the next argument")
{
    const CommandLineOptions options = Parse({ "--scene", "Scenes/FeatureTest.json" });
    CHECK(options.ScenePath == "Scenes/FeatureTest.json");
}

TEST_CASE("--scene= form is parsed")
{
    const CommandLineOptions options = Parse({ "--scene=Scenes/OrientationTest.json" });
    CHECK(options.ScenePath == "Scenes/OrientationTest.json");
}

TEST_CASE("Short flags are parsed")
{
    const CommandLineOptions options = Parse({ "-s", "Scenes/TestScene.json", "-a", "F:/Some/Assets" });
    CHECK(options.ScenePath == "Scenes/TestScene.json");
    CHECK(options.AssetRoot == "F:/Some/Assets");
}

TEST_CASE("--assets= form is parsed")
{
    const CommandLineOptions options = Parse({ "--assets=F:/Export/Game/Assets" });
    CHECK(options.AssetRoot == "F:/Export/Game/Assets");
}

TEST_CASE("--help sets the flag")
{
    CHECK(Parse({ "--help" }).ShowHelp);
    CHECK(Parse({ "-h" }).ShowHelp);
}

TEST_CASE("Unknown arguments are ignored")
{
    const CommandLineOptions options = Parse({ "--unknown", "Scenes/TestScene.json", "--another" });
    CHECK(options.ScenePath == "Scenes/TestScene.json");
}

TEST_CASE("A missing value after --scene leaves the path empty")
{
    const CommandLineOptions options = Parse({ "--scene" });
    CHECK(options.ScenePath.empty());
}
