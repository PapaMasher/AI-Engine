#include "Slate/Core/CommandLine.h"

#include <ostream>

namespace Slate {

CommandLineOptions ParseCommandLine(int argc, char** argv)
{
    CommandLineOptions options;

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];

        if (argument == "--help" || argument == "-h")
        {
            options.ShowHelp = true;
        }
        else if (argument == "--scene" || argument == "-s")
        {
            if (index + 1 < argc)
                options.ScenePath = argv[++index];
        }
        else if (argument.rfind("--scene=", 0) == 0)
        {
            options.ScenePath = argument.substr(std::string("--scene=").size());
        }
        else if (argument == "--assets" || argument == "-a")
        {
            if (index + 1 < argc)
                options.AssetRoot = argv[++index];
        }
        else if (argument.rfind("--assets=", 0) == 0)
        {
            options.AssetRoot = argument.substr(std::string("--assets=").size());
        }
        else if (!argument.empty() && argument[0] != '-')
        {
            options.ScenePath = argument;
        }
    }

    return options;
}

void PrintUsage(std::ostream& stream, const std::string& executableName,
    const std::string& summary)
{
    stream << executableName << "\n"
           << summary << "\n\n"
           << "Usage: " << executableName << " [scene] [options]\n\n"
           << "  scene                  scene to load, relative to the asset root\n"
           << "  --scene, -s <path>     same as the positional argument\n"
           << "  --assets, -a <dir>     override the asset root\n"
           << "  --help, -h             show this help\n\n"
           << "SLATE_SCENE and SLATE_AUTOMATION_PORT still work as environment\n"
           << "fallbacks; command-line arguments take precedence.\n";
}

} // namespace Slate
