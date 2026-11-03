#include <doctest/doctest.h>

#include "Slate/Core/UUID.h"
#include "Slate/Core/Timer.h"
#include "Slate/Core/FileSystem.h"

#include <thread>

using namespace Slate;

TEST_CASE("UUID generation produces non-zero unique values")
{
    const UUID first = UUID::Generate();
    const UUID second = UUID::Generate();

    CHECK(first.GetValue() != 0);
    CHECK(second.GetValue() != 0);
    CHECK(first != second);
    CHECK(first.ToString().size() == 16);
}

TEST_CASE("UUID explicit value round-trips")
{
    const UUID uuid(0xDEADBEEFCAFEBABEull);
    CHECK(uuid.GetValue() == 0xDEADBEEFCAFEBABEull);
    CHECK(static_cast<bool>(uuid));
    CHECK_FALSE(static_cast<bool>(UUID(0)));
}

TEST_CASE("Timer measures elapsed wall-clock time")
{
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    CHECK(timer.ElapsedMilliseconds() >= 15.0);
    CHECK(timer.ElapsedSeconds() >= 0.015);
}

TEST_CASE("FileSystem writes and reads text files")
{
    const auto path = std::filesystem::temp_directory_path() / "slate_test_text.txt";
    const std::string contents = "Slate engine file system test\nline two\n";

    REQUIRE(FileSystem::WriteTextFile(path, contents));
    REQUIRE(FileSystem::Exists(path));

    const auto readBack = FileSystem::ReadTextFile(path);
    REQUIRE(readBack.has_value());
    CHECK(*readBack == contents);

    std::filesystem::remove(path);
}

TEST_CASE("FileSystem writes and reads binary files")
{
    const auto path = std::filesystem::temp_directory_path() / "slate_test_binary.bin";
    const std::vector<uint8_t> data = { 0x00, 0x01, 0x02, 0xFF, 0x7F };

    REQUIRE(FileSystem::WriteBinaryFile(path, data.data(), data.size()));
    CHECK(FileSystem::ReadBinaryFile(path) == data);

    std::filesystem::remove(path);
}

TEST_CASE("FileSystem path helpers")
{
    const std::filesystem::path path = "folder/subfolder/mesh.gltf";
    CHECK(FileSystem::GetFileName(path) == "mesh.gltf");
    CHECK(FileSystem::GetFileNameWithoutExtension(path) == "mesh");
    CHECK(FileSystem::GetExtension(path) == ".gltf");
}
