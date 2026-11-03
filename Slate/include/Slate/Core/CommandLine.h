#pragma once

#include "Slate/Core/Base.h"

#include <iosfwd>
#include <string>

namespace Slate {

struct CommandLineOptions
{
    std::string ScenePath;
    std::string AssetRoot;
    bool ShowHelp = false;
};

// Parses the shared Slate command line:
//   <scene>                 positional scene path
//   --scene, -s <path>      scene path
//   --assets, -a <dir>      asset root override
//   --help, -h              usage
// Unknown arguments are ignored.
CommandLineOptions ParseCommandLine(int argc, char** argv);

void PrintUsage(std::ostream& stream, const std::string& executableName,
    const std::string& summary);

} // namespace Slate
