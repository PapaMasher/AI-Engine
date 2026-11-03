#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Platform/VulkanContext.h"
#include "Slate/Assets/AssetManager.h"
#include "Slate/Scene/Scene.h"

#include <nvrhi/nvrhi.h>
#include <glm/glm.hpp>

#include <unordered_map>

namespace Slate {

// Matches the FrameConstants block declared by every shader (std140 layout).
struct FrameConstants
{
    glm::mat4 ViewProjection { 1.0f };
    glm::mat4 View { 1.0f };
    glm::mat4 Projection { 1.0f };
    glm::mat4 InvViewProjection { 1.0f };
    glm::mat4 InvProjection { 1.0f };
    glm::mat4 LightViewProjection { 1.0f };

    glm::vec4 CameraPosition { 0.0f };
    glm::vec4 LightDirection { 0.0f, -1.0f, 0.0f, 0.0f };
    glm::vec4 LightColor { 1.0f };
    glm::vec4 AmbientColor { 0.1f, 0.1f, 0.12f, 0.3f };
    glm::vec4 ShadowParams { 0.0f }; // x = 1/shadow size, y = bias
    glm::vec4 ScreenParams { 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 Exposure { 1.0f };
    glm::vec4 IBLParams { 4.0f, 4.0f, 0.0f, 0.0f }; // x = prefilter mips, y = skybox mip, z = point light count

    glm::vec4 PointLightPosition[8] {};
    glm::vec4 PointLightColor[8] {};
};

// Per-object data passed through Vulkan push constants (112 bytes).
struct ObjectPushConstants
{
    glm::mat4 Model { 1.0f };
    glm::vec4 BaseColor { 1.0f };
    glm::vec4 Params { 0.0f, 0.5f, 1.0f, 0.0f }; // metallic, roughness, emissive, flags
    glm::vec4 Emissive { 0.0f };
};

class Renderer
{
public:
    struct Statistics
    {
        uint32_t DrawCalls = 0;
        uint32_t Triangles = 0;
        uint32_t Entities = 0;
    };

    void Init(VulkanContext& context);
    void Shutdown();

    void Resize(uint32_t width, uint32_t height);
    void Render(Scene& scene);

    AssetManager& GetAssets() { return m_Assets; }

    nvrhi::ITexture* GetFinalColorTexture() const { return m_HdrColor; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    const Statistics& GetStatistics() const { return m_Statistics; }
    bool IsIBLReady() const { return m_IBLReady; }

    // Queues an HDR panorama for IBL generation on the next frame.
    void SetEnvironment(Ref<TextureData> hdr);

private:
    struct GpuMesh
    {
        nvrhi::BufferHandle VertexBuffer;
        nvrhi::BufferHandle IndexBuffer;
        std::vector<Submesh> Submeshes;
    };

    struct GpuModel
    {
        std::vector<GpuMesh> Meshes;
        std::vector<Material> Materials;
        std::vector<nvrhi::TextureHandle> Textures;
    };

    void CreateSamplers();
    void CreateLayouts();
    void CreatePipelines();
    void CreateTargets(uint32_t width, uint32_t height);
    void CreatePlaceholderIBL();
    void GenerateBRDFLUT();
    void GenerateIBL();

    GpuModel& GetOrCreateGpuModel(const std::string& path);
    nvrhi::TextureHandle GetOrCreateGpuTexture(const Ref<TextureData>& texture);
    nvrhi::TextureHandle CreateDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb);
    void CreateMaterialBindingSets();

    void DrawShadowPass(Scene& scene);
    void DrawPrepass(Scene& scene);
    void DrawSSAO();
    void DrawSkybox();
    void DrawOpaque(Scene& scene);

    void ComputeFrameConstants(Scene& scene, const CameraData& camera);
    void ComputeLightMatrices(Scene& scene);
    void PublishCompositeSource();

    VulkanContext* m_Context = nullptr;
    nvrhi::DeviceHandle m_Device;
    nvrhi::CommandListHandle m_CommandList;

    AssetManager m_Assets;
    FrameConstants m_FrameConstants;

    nvrhi::BufferHandle m_FrameCB[2];

    nvrhi::SamplerHandle m_LinearRepeat;
    nvrhi::SamplerHandle m_LinearClamp;
    nvrhi::SamplerHandle m_ShadowSampler;
    nvrhi::SamplerHandle m_CubeSampler;

    nvrhi::BindingLayoutHandle m_MaterialLayout;
    nvrhi::BindingLayoutHandle m_ShadowLayout;
    nvrhi::BindingLayoutHandle m_PrepassLayout;
    nvrhi::BindingLayoutHandle m_SSAOLayout;
    nvrhi::BindingLayoutHandle m_BlurLayout;
    nvrhi::BindingLayoutHandle m_SkyboxLayout;
    nvrhi::BindingLayoutHandle m_EquirectLayout;
    nvrhi::BindingLayoutHandle m_IrradianceLayout;
    nvrhi::BindingLayoutHandle m_PrefilterLayout;
    nvrhi::BindingLayoutHandle m_BrdfLayout;

    nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
    nvrhi::GraphicsPipelineHandle m_PrepassPipeline;
    nvrhi::GraphicsPipelineHandle m_PbrPipeline;
    nvrhi::GraphicsPipelineHandle m_SSAOPipeline;
    nvrhi::GraphicsPipelineHandle m_SSAOBlurPipeline;
    nvrhi::GraphicsPipelineHandle m_SkyboxPipeline;
    nvrhi::GraphicsPipelineHandle m_EquirectPipeline;
    nvrhi::GraphicsPipelineHandle m_IrradiancePipeline;
    nvrhi::GraphicsPipelineHandle m_PrefilterPipeline;
    nvrhi::GraphicsPipelineHandle m_BrdfPipeline;

    nvrhi::TextureHandle m_HdrColor;
    nvrhi::TextureHandle m_Depth;
    nvrhi::TextureHandle m_Normals;
    nvrhi::TextureHandle m_SSAO;
    nvrhi::TextureHandle m_SSAOBlurred;
    nvrhi::TextureHandle m_ShadowMap;

    nvrhi::FramebufferHandle m_HdrFramebuffer;
    nvrhi::FramebufferHandle m_PrepassFramebuffer;
    nvrhi::FramebufferHandle m_SSAOFramebuffer;
    nvrhi::FramebufferHandle m_SSAOBlurFramebuffer;
    nvrhi::FramebufferHandle m_ShadowFramebuffer;

    nvrhi::TextureHandle m_DefaultWhite;
    nvrhi::TextureHandle m_DefaultNormal;
    nvrhi::TextureHandle m_DefaultBlack;

    // IBL resources.
    nvrhi::TextureHandle m_EnvironmentCube;
    nvrhi::TextureHandle m_IrradianceCube;
    nvrhi::TextureHandle m_PrefilterCube;
    nvrhi::TextureHandle m_BrdfLUT;
    nvrhi::BindingSetHandle m_EquirectBindingSet;
    nvrhi::BindingSetHandle m_IrradianceBindingSet;
    nvrhi::BindingSetHandle m_PrefilterBindingSet;
    nvrhi::BindingSetHandle m_ShadowBindingSets[2] {};
    nvrhi::BindingSetHandle m_PrepassBindingSets[2] {};
    bool m_IBLReady = false;
    bool m_BrdfLUTReady = false;
    bool m_EnvironmentDirty = false;
    bool m_BindingSetsDirty = true;
    Ref<TextureData> m_PendingHDR;
    std::string m_LoadedEnvironmentPath;

    std::unordered_map<std::string, GpuModel> m_GpuModels;
    std::unordered_map<const TextureData*, nvrhi::TextureHandle> m_GpuTextures;
    std::unordered_map<std::string, std::array<nvrhi::BindingSetHandle, 2>> m_MaterialBindingSets;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    Statistics m_Statistics;
};

} // namespace Slate
