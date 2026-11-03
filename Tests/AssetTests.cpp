#include <doctest/doctest.h>

#include "Slate/Assets/AssetManager.h"
#include "Slate/Assets/AssetLoader.h"
#include "Slate/Core/FileSystem.h"

using namespace Slate;

TEST_CASE("Asset root resolves to a directory containing Assets content")
{
    const auto root = FileSystem::GetAssetRoot();
    CHECK(FileSystem::Exists(root));
    CHECK(FileSystem::Exists(root / "Models" / "BoxTextured.glb"));
}

TEST_CASE("BoxTextured.glb loads geometry, materials and textures")
{
    const Ref<Model> model = LoadModelFromFile("Models/BoxTextured.glb");
    REQUIRE(model != nullptr);
    REQUIRE(IsModelValid(*model));

    CHECK(model->Meshes.size() >= 1);
    CHECK(model->Materials.size() >= 1);
    CHECK(model->Textures.size() >= 1);

    const Mesh& mesh = model->Meshes.front();
    CHECK(mesh.Vertices.size() > 0);
    CHECK(mesh.Indices.size() > 0);
    CHECK(mesh.Indices.size() % 3 == 0);
    CHECK(!mesh.Submeshes.empty());

    // Bounds must be finite and non-degenerate.
    CHECK(mesh.BoundsMin.x <= mesh.BoundsMax.x);
    CHECK(mesh.BoundsMin.y <= mesh.BoundsMax.y);
    CHECK(mesh.BoundsMin.z <= mesh.BoundsMax.z);
    CHECK(mesh.BoundsMax.x - mesh.BoundsMin.x > 0.0f);

    // Every vertex must have a normalized normal and a valid tangent frame.
    for (const auto& vertex : mesh.Vertices)
    {
        CHECK(glm::length(vertex.Normal) > 0.9f);
        CHECK(std::abs(vertex.Tangent.w) == 1.0f);
    }

    // Every index must be in range.
    for (uint32_t index : mesh.Indices)
        CHECK(index < mesh.Vertices.size());
}

TEST_CASE("DamagedHelmet.glb loads a multi-mesh PBR model")
{
    const Ref<Model> model = LoadModelFromFile("Models/DamagedHelmet.glb");
    REQUIRE(model != nullptr);
    REQUIRE(IsModelValid(*model));

    CHECK(model->Meshes.size() >= 1);
    CHECK(model->Textures.size() >= 3);
    CHECK(model->Meshes.front().Vertices.size() > 1000);
}

TEST_CASE("AssetManager caches models and textures")
{
    AssetManager assets;

    const Ref<Model> first = assets.LoadModel("Models/BoxTextured.glb");
    const Ref<Model> second = assets.LoadModel("Models/BoxTextured.glb");
    REQUIRE(first != nullptr);
    CHECK(first == second);
    CHECK(assets.GetModelCount() == 1);

    const Ref<TextureData> hdr = assets.LoadTexture("HDRIs/studio_small_03_1k.hdr", false);
    REQUIRE(hdr != nullptr);
    CHECK(hdr->IsHDR);
    CHECK(hdr->Width == 1024);
    CHECK(hdr->Height == 512);
    CHECK(hdr->PixelsFloat.size() == static_cast<size_t>(1024) * 512 * 4);
    CHECK(assets.GetTextureCount() == 1);
}

TEST_CASE("Missing assets fail gracefully")
{
    CHECK(LoadModelFromFile("Models/DoesNotExist.glb") == nullptr);
    CHECK(LoadTextureFromFile("Textures/DoesNotExist.png", true) == nullptr);
}
