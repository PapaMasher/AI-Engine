#pragma once

#include "Slate/Core/Base.h"

#include <optional>

namespace Slate::FileSystem {

// Root directory that contains the engine's Assets/ folder. Defaults to the
// executable directory; in development builds it points at the repository's
// Assets folder so assets can be edited without rebuilding.
const std::filesystem::path& GetAssetRoot();
void SetAssetRoot(const std::filesystem::path& path);

std::filesystem::path GetExecutableDirectory();
std::filesystem::path GetWorkingDirectory();

bool Exists(const std::filesystem::path& path);
bool CreateDirectories(const std::filesystem::path& path);

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path);
std::optional<std::string> ReadTextFile(const std::filesystem::path& path);

bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);
bool WriteTextFile(const std::filesystem::path& path, std::string_view text);

std::string GetFileName(const std::filesystem::path& path);
std::string GetFileNameWithoutExtension(const std::filesystem::path& path);
std::string GetExtension(const std::filesystem::path& path);

// Resolves a path relative to the asset root. Absolute paths are returned
// unchanged.
std::filesystem::path ResolveAssetPath(const std::filesystem::path& path);

} // namespace Slate::FileSystem
