#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace Slate::FileSystem {

namespace {

std::filesystem::path s_AssetRoot;

} // namespace

std::filesystem::path GetExecutableDirectory()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return std::filesystem::current_path();
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0)
        return std::filesystem::current_path();
    return std::filesystem::weakly_canonical(path).parent_path();
#else
    std::array<char, 4096> buffer {};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0)
        return std::filesystem::current_path();
    return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path();
#endif
}

std::filesystem::path GetWorkingDirectory()
{
    return std::filesystem::current_path();
}

const std::filesystem::path& GetAssetRoot()
{
    if (!s_AssetRoot.empty())
        return s_AssetRoot;

    // A packaged game ships its assets next to the executable; that always
    // wins so exported builds are self-contained.
    const std::filesystem::path localAssets = GetExecutableDirectory() / "Assets";
    if (std::filesystem::exists(localAssets))
    {
        s_AssetRoot = std::filesystem::weakly_canonical(localAssets);
        return s_AssetRoot;
    }

    // Development builds define SLATE_ASSET_DIR to the repository's Assets
    // folder so content can be iterated on without rebuilding.
#ifdef SLATE_ASSET_DIR
    const std::filesystem::path devAssets = SLATE_ASSET_DIR;
    if (std::filesystem::exists(devAssets))
    {
        s_AssetRoot = std::filesystem::weakly_canonical(devAssets);
        return s_AssetRoot;
    }
#endif

    s_AssetRoot = std::filesystem::current_path() / "Assets";
    return s_AssetRoot;
}

void SetAssetRoot(const std::filesystem::path& path)
{
    s_AssetRoot = std::filesystem::weakly_canonical(path);
}

bool Exists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool CreateDirectories(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to open file '{}'", path.string());
        return {};
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0)
        return {};

    std::vector<uint8_t> data(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(data.data()), size))
    {
        SLATE_ERROR("Failed to read file '{}'", path.string());
        return {};
    }
    return data;
}

std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::in | std::ios::binary);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to open file '{}'", path.string());
        return std::nullopt;
    }

    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return text;
}

bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size)
{
    std::error_code error;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), error);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        SLATE_ERROR("Failed to open file for writing '{}'", path.string());
        return false;
    }

    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return stream.good();
}

bool WriteTextFile(const std::filesystem::path& path, std::string_view text)
{
    return WriteBinaryFile(path, text.data(), text.size());
}

std::string GetFileName(const std::filesystem::path& path)
{
    return path.filename().string();
}

std::string GetFileNameWithoutExtension(const std::filesystem::path& path)
{
    return path.stem().string();
}

std::string GetExtension(const std::filesystem::path& path)
{
    return path.extension().string();
}

std::filesystem::path ResolveAssetPath(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return path;
    return GetAssetRoot() / path;
}

} // namespace Slate::FileSystem
