#include "Slate/Renderer/Renderer.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"
#include "Slate/Assets/AssetLoader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Slate {

namespace {

constexpr uint32_t kShadowMapSize = 2048;
constexpr uint32_t kEnvironmentCubeSize = 256;
constexpr uint32_t kIrradianceCubeSize = 32;
constexpr uint32_t kPrefilterCubeSize = 128;
constexpr uint32_t kPrefilterMipCount = 5;
constexpr uint32_t kBrdfLutSize = 256;

nvrhi::ShaderHandle LoadShader(nvrhi::IDevice* device, const std::string& name, nvrhi::ShaderType type)
{
    const std::filesystem::path path = FileSystem::ResolveAssetPath("Shaders/" + name + ".spv");
    const std::vector<uint8_t> code = FileSystem::ReadBinaryFile(path);
    if (code.empty())
    {
        SLATE_ERROR("Missing shader '{}'", path.string());
        return nullptr;
    }

    return device->createShader(
        nvrhi::ShaderDesc().setShaderType(type).setEntryName("main"), code.data(), code.size());
}

nvrhi::InputLayoutHandle CreateMeshInputLayout(nvrhi::IDevice* device, nvrhi::IShader* vertexShader,
    uint32_t attributeCount)
{
    const nvrhi::VertexAttributeDesc attributes[] = {
        nvrhi::VertexAttributeDesc()
            .setName("POSITION")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(Vertex, Position))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("NORMAL")
            .setFormat(nvrhi::Format::RGB32_FLOAT)
            .setOffset(offsetof(Vertex, Normal))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TEXCOORD")
            .setFormat(nvrhi::Format::RG32_FLOAT)
            .setOffset(offsetof(Vertex, TexCoord))
            .setElementStride(sizeof(Vertex)),
        nvrhi::VertexAttributeDesc()
            .setName("TANGENT")
            .setFormat(nvrhi::Format::RGBA32_FLOAT)
            .setOffset(offsetof(Vertex, Tangent))
            .setElementStride(sizeof(Vertex))
    };

    return device->createInputLayout(attributes, attributeCount, vertexShader);
}

std::vector<std::vector<uint8_t>> GenerateMipChain(const uint8_t* rgba, uint32_t width, uint32_t height)
{
    std::vector<std::vector<uint8_t>> mips;
    mips.emplace_back(rgba, rgba + static_cast<size_t>(width) * height * 4);

    uint32_t mipWidth = width;
    uint32_t mipHeight = height;

    while (mipWidth > 1 || mipHeight > 1)
    {
        const uint32_t nextWidth = std::max(1u, mipWidth / 2);
        const uint32_t nextHeight = std::max(1u, mipHeight / 2);

        const std::vector<uint8_t>& previous = mips.back();
        std::vector<uint8_t> next(static_cast<size_t>(nextWidth) * nextHeight * 4);

        for (uint32_t y = 0; y < nextHeight; ++y)
        {
            for (uint32_t x = 0; x < nextWidth; ++x)
            {
                uint32_t sum[4] = { 0, 0, 0, 0 };
                uint32_t count = 0;

                for (uint32_t dy = 0; dy < 2; ++dy)
                {
                    for (uint32_t dx = 0; dx < 2; ++dx)
                    {
                        const uint32_t sx = std::min(mipWidth - 1, x * 2 + dx);
                        const uint32_t sy = std::min(mipHeight - 1, y * 2 + dy);
                        const uint8_t* pixel = &previous[(static_cast<size_t>(sy) * mipWidth + sx) * 4];
                        for (int c = 0; c < 4; ++c)
                            sum[c] += pixel[c];
                        ++count;
                    }
                }

                uint8_t* out = &next[(static_cast<size_t>(y) * nextWidth + x) * 4];
                for (int c = 0; c < 4; ++c)
                    out[c] = static_cast<uint8_t>(sum[c] / count);
            }
        }

        mips.push_back(std::move(next));
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }

    return mips;
}

glm::vec3 TransformPoint(const glm::mat4& matrix, const glm::vec3& point)
{
    const glm::vec4 result = matrix * glm::vec4(point, 1.0f);
    return glm::vec3(result);
}

} // namespace

void Renderer::Init(VulkanContext& context)
{
    m_Context = &context;
    m_Device = context.GetNvrhiDevice();

    m_CommandList = m_Device->createCommandList();
    m_CommandList->setEnableAutomaticBarriers(true);

    // Per-frame constant buffers: one per frame in flight, written once per
    // frame. This avoids volatile-buffer versioning while remaining race-free
    // because the frame fence is waited on before reuse.
    for (nvrhi::BufferHandle& buffer : m_FrameCB)
    {
        nvrhi::BufferDesc cbDesc;
        cbDesc.byteSize = sizeof(FrameConstants);
        cbDesc.isConstantBuffer = true;
        cbDesc.setInitialState(nvrhi::ResourceStates::ConstantBuffer);
        cbDesc.setKeepInitialState(true);
        cbDesc.debugName = "Frame Constants";
        buffer = m_Device->createBuffer(cbDesc);
    }

    CreateSamplers();
    CreateLayouts();

    {
        nvrhi::BindingSetDesc desc;
        desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[0]));
        m_ShadowBindingSets[0] = m_Device->createBindingSet(desc, m_ShadowLayout);
        m_PrepassBindingSets[0] = m_Device->createBindingSet(desc, m_PrepassLayout);

        nvrhi::BindingSetDesc desc1;
        desc1.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[1]));
        m_ShadowBindingSets[1] = m_Device->createBindingSet(desc1, m_ShadowLayout);
        m_PrepassBindingSets[1] = m_Device->createBindingSet(desc1, m_PrepassLayout);
    }

    CreatePlaceholderIBL();
    CreatePipelines();
    CreateUIResources();

    Resize(context.GetSwapchainExtent().width, context.GetSwapchainExtent().height);
}

void Renderer::Shutdown()
{
    m_MaterialBindingSets.clear();
    m_GpuTextures.clear();
    m_GpuModels.clear();

    m_BrdfLUT = nullptr;
    m_PrefilterCube = nullptr;
    m_IrradianceCube = nullptr;
    m_EnvironmentCube = nullptr;

    m_DefaultBlack = nullptr;
    m_DefaultNormal = nullptr;
    m_DefaultWhite = nullptr;

    m_ShadowFramebuffer = nullptr;
    m_SSAOBlurFramebuffer = nullptr;
    m_SSAOFramebuffer = nullptr;
    m_PrepassFramebuffer = nullptr;
    m_HdrFramebuffer = nullptr;

    m_ShadowMap = nullptr;
    m_SSAOBlurred = nullptr;
    m_SSAO = nullptr;
    m_Normals = nullptr;
    m_Depth = nullptr;
    m_HdrColor = nullptr;

    m_UIVertices.clear();
    m_UIVertexBuffer = nullptr;
    m_UIFontTexture = nullptr;
    m_UIBindingSet = nullptr;
    m_UIPipeline = nullptr;
    m_UILayout = nullptr;

    m_BloomBindingSets.clear();
    m_BloomFramebuffers.clear();
    m_BloomTextures.clear();
    m_BloomCombineFramebuffer = nullptr;
    m_HdrBindingSet = nullptr;

    m_BloomCombinePipeline = nullptr;
    m_BloomUpsamplePipeline = nullptr;
    m_BloomDownsamplePipeline = nullptr;
    m_BloomPrefilterPipeline = nullptr;
    m_BloomLayout = nullptr;

    m_BrdfPipeline = nullptr;
    m_PrefilterPipeline = nullptr;
    m_IrradiancePipeline = nullptr;
    m_EquirectPipeline = nullptr;
    m_SkyboxPipeline = nullptr;
    m_SSAOBlurPipeline = nullptr;
    m_SSAOPipeline = nullptr;
    m_PbrPipeline = nullptr;
    m_PrepassPipeline = nullptr;
    m_ShadowPipeline = nullptr;

    m_BrdfLayout = nullptr;
    m_PrefilterLayout = nullptr;
    m_IrradianceLayout = nullptr;
    m_EquirectLayout = nullptr;
    m_SkyboxLayout = nullptr;
    m_BlurLayout = nullptr;
    m_SSAOLayout = nullptr;
    m_PrepassLayout = nullptr;
    m_ShadowLayout = nullptr;
    m_MaterialLayout = nullptr;

    m_CubeSampler = nullptr;
    m_ShadowSampler = nullptr;
    m_LinearClamp = nullptr;
    m_LinearRepeat = nullptr;

    for (nvrhi::BufferHandle& buffer : m_FrameCB)
        buffer = nullptr;
    m_CommandList = nullptr;
    m_Device = nullptr;
    m_Context = nullptr;
}

void Renderer::CreateSamplers()
{
    nvrhi::SamplerDesc repeat;
    repeat.setAllFilters(true);
    repeat.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
    repeat.setMaxAnisotropy(8.0f);
    m_LinearRepeat = m_Device->createSampler(repeat);

    nvrhi::SamplerDesc clamp;
    clamp.setAllFilters(true);
    clamp.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_LinearClamp = m_Device->createSampler(clamp);

    nvrhi::SamplerDesc shadow;
    shadow.setAllFilters(true);
    shadow.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    shadow.setBorderColor(nvrhi::Color(1.0f));
    m_ShadowSampler = m_Device->createSampler(shadow);

    nvrhi::SamplerDesc cube;
    cube.setAllFilters(true);
    cube.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_CubeSampler = m_Device->createSampler(cube);
}

void Renderer::CreateLayouts()
{
    const uint32_t pushSize = sizeof(ObjectPushConstants);

    nvrhi::BindingLayoutDesc material;
    material.setVisibility(nvrhi::ShaderType::All);
    material.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    for (uint32_t i = 0; i < 10; ++i)
        material.addItem(nvrhi::BindingLayoutItem::Texture_SRV(i));
    for (uint32_t i = 0; i < 3; ++i)
        material.addItem(nvrhi::BindingLayoutItem::Sampler(i));
    material.addItem(nvrhi::BindingLayoutItem::PushConstants(0, pushSize));
    m_MaterialLayout = m_Device->createBindingLayout(material);

    nvrhi::BindingLayoutDesc shadow;
    shadow.setVisibility(nvrhi::ShaderType::All);
    shadow.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    shadow.addItem(nvrhi::BindingLayoutItem::PushConstants(0, pushSize));
    m_ShadowLayout = m_Device->createBindingLayout(shadow);

    nvrhi::BindingLayoutDesc prepass;
    prepass.setVisibility(nvrhi::ShaderType::All);
    prepass.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    prepass.addItem(nvrhi::BindingLayoutItem::PushConstants(0, pushSize));
    m_PrepassLayout = m_Device->createBindingLayout(prepass);

    nvrhi::BindingLayoutDesc ssao;
    ssao.setVisibility(nvrhi::ShaderType::Pixel);
    ssao.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    ssao.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    ssao.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
    ssao.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_SSAOLayout = m_Device->createBindingLayout(ssao);

    nvrhi::BindingLayoutDesc blur;
    blur.setVisibility(nvrhi::ShaderType::Pixel);
    blur.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    blur.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    blur.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_BlurLayout = m_Device->createBindingLayout(blur);

    nvrhi::BindingLayoutDesc skybox;
    skybox.setVisibility(nvrhi::ShaderType::Pixel);
    skybox.addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0));
    skybox.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    skybox.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    m_SkyboxLayout = m_Device->createBindingLayout(skybox);

    nvrhi::BindingLayoutDesc equirect;
    equirect.setVisibility(nvrhi::ShaderType::Pixel);
    equirect.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    equirect.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    equirect.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(int)));
    m_EquirectLayout = m_Device->createBindingLayout(equirect);

    nvrhi::BindingLayoutDesc irradiance;
    irradiance.setVisibility(nvrhi::ShaderType::Pixel);
    irradiance.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    irradiance.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    irradiance.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(int)));
    m_IrradianceLayout = m_Device->createBindingLayout(irradiance);

    nvrhi::BindingLayoutDesc prefilter;
    prefilter.setVisibility(nvrhi::ShaderType::Pixel);
    prefilter.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    prefilter.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    prefilter.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(float) * 3));
    m_PrefilterLayout = m_Device->createBindingLayout(prefilter);

    nvrhi::BindingLayoutDesc brdf;
    brdf.setVisibility(nvrhi::ShaderType::Pixel);
    m_BrdfLayout = m_Device->createBindingLayout(brdf);

    nvrhi::BindingLayoutDesc bloom;
    bloom.setVisibility(nvrhi::ShaderType::Pixel);
    bloom.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    bloom.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    bloom.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(BloomPushConstants)));
    m_BloomLayout = m_Device->createBindingLayout(bloom);
}

void Renderer::CreatePlaceholderIBL()
{
    nvrhi::TextureDesc cubeDesc;
    cubeDesc.width = 1;
    cubeDesc.height = 1;
    cubeDesc.dimension = nvrhi::TextureDimension::TextureCube;
    cubeDesc.arraySize = 6;
    cubeDesc.mipLevels = 1;
    cubeDesc.format = nvrhi::Format::RGBA16_FLOAT;
    cubeDesc.isShaderResource = true;
    cubeDesc.isRenderTarget = true;
    cubeDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    cubeDesc.setKeepInitialState(true);
    cubeDesc.debugName = "IBL Placeholder";

    m_IrradianceCube = m_Device->createTexture(cubeDesc);
    m_PrefilterCube = m_Device->createTexture(cubeDesc);

    nvrhi::TextureDesc brdfDesc;
    brdfDesc.width = kBrdfLutSize;
    brdfDesc.height = kBrdfLutSize;
    brdfDesc.format = nvrhi::Format::RG16_FLOAT;
    brdfDesc.isShaderResource = true;
    brdfDesc.isRenderTarget = true;
    brdfDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    brdfDesc.setKeepInitialState(true);
    brdfDesc.debugName = "BRDF LUT";
    m_BrdfLUT = m_Device->createTexture(brdfDesc);

    m_DefaultWhite = CreateDefaultTexture(255, 255, 255, 255, false);
    m_DefaultNormal = CreateDefaultTexture(128, 128, 255, 255, false);
    m_DefaultBlack = CreateDefaultTexture(0, 0, 0, 255, false);
}

nvrhi::TextureHandle Renderer::CreateDefaultTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb)
{
    nvrhi::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
    desc.isShaderResource = true;
    desc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    desc.setKeepInitialState(true);
    desc.debugName = "Default Texture";

    nvrhi::TextureHandle texture = m_Device->createTexture(desc);
    const uint8_t pixels[4] = { r, g, b, a };

    m_CommandList->open();
    m_CommandList->writeTexture(texture, 0, 0, pixels, 4);
    m_CommandList->close();
    m_Context->ExecuteCommandList(m_CommandList);

    return texture;
}

void Renderer::CreatePipelines()
{
    const nvrhi::ShaderHandle fullscreenVertex = LoadShader(m_Device, "fullscreen.vert", nvrhi::ShaderType::Vertex);
    const nvrhi::ShaderHandle pbrVertex = LoadShader(m_Device, "pbr.vert", nvrhi::ShaderType::Vertex);
    const nvrhi::ShaderHandle pbrFragment = LoadShader(m_Device, "pbr.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle shadowVertex = LoadShader(m_Device, "shadow.vert", nvrhi::ShaderType::Vertex);
    const nvrhi::ShaderHandle prepassVertex = LoadShader(m_Device, "prepass.vert", nvrhi::ShaderType::Vertex);
    const nvrhi::ShaderHandle prepassFragment = LoadShader(m_Device, "prepass.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle ssaoFragment = LoadShader(m_Device, "ssao.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle ssaoBlurFragment = LoadShader(m_Device, "ssao_blur.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle skyboxFragment = LoadShader(m_Device, "skybox.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle equirectFragment = LoadShader(m_Device, "equirect_to_cube.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle irradianceFragment = LoadShader(m_Device, "irradiance.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle prefilterFragment = LoadShader(m_Device, "prefilter.frag", nvrhi::ShaderType::Pixel);
    const nvrhi::ShaderHandle brdfFragment = LoadShader(m_Device, "brdf_lut.frag", nvrhi::ShaderType::Pixel);

    if (!fullscreenVertex || !pbrVertex || !pbrFragment)
    {
        SLATE_CRITICAL("Failed to load core shaders");
        return;
    }

    const nvrhi::InputLayoutHandle meshLayout = CreateMeshInputLayout(m_Device, pbrVertex, 4);
    const nvrhi::InputLayoutHandle shadowLayout = CreateMeshInputLayout(m_Device, shadowVertex, 1);
    const nvrhi::InputLayoutHandle prepassLayout = CreateMeshInputLayout(m_Device, prepassVertex, 2);

    // Shadow pass: depth only.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(true);
        state.depthStencilState.setDepthWriteEnable(true);
        state.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::Back);
        state.rasterState.frontCounterClockwise = true;

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setInputLayout(shadowLayout);
        desc.setVertexShader(shadowVertex);
        desc.setRenderState(state);
        desc.addBindingLayout(m_ShadowLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo().setDepthFormat(nvrhi::Format::D32);
        m_ShadowPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // Prepass: depth + view-space normals.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(true);
        state.depthStencilState.setDepthWriteEnable(true);
        state.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::Back);
        state.rasterState.frontCounterClockwise = true;

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setInputLayout(prepassLayout);
        desc.setVertexShader(prepassVertex);
        desc.setPixelShader(prepassFragment);
        desc.setRenderState(state);
        desc.addBindingLayout(m_PrepassLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo()
            .addColorFormat(nvrhi::Format::RGBA16_FLOAT)
            .setDepthFormat(nvrhi::Format::D32);
        m_PrepassPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // Forward PBR.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(true);
        state.depthStencilState.setDepthWriteEnable(false);
        state.depthStencilState.setDepthFunc(nvrhi::ComparisonFunc::Equal);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::Back);
        state.rasterState.frontCounterClockwise = true;

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setInputLayout(meshLayout);
        desc.setVertexShader(pbrVertex);
        desc.setPixelShader(pbrFragment);
        desc.setRenderState(state);
        desc.addBindingLayout(m_MaterialLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo()
            .addColorFormat(nvrhi::Format::RGBA16_FLOAT)
            .setDepthFormat(nvrhi::Format::D32);
        m_PbrPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // SSAO.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(false);
        state.depthStencilState.setDepthWriteEnable(false);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::None);

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setVertexShader(fullscreenVertex);
        desc.setPixelShader(ssaoFragment);
        desc.setRenderState(state);
        desc.addBindingLayout(m_SSAOLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::R8_UNORM);
        m_SSAOPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // SSAO blur.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(false);
        state.depthStencilState.setDepthWriteEnable(false);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::None);

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setVertexShader(fullscreenVertex);
        desc.setPixelShader(ssaoBlurFragment);
        desc.setRenderState(state);
        desc.addBindingLayout(m_BlurLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::R8_UNORM);
        m_SSAOBlurPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // Skybox.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(false);
        state.depthStencilState.setDepthWriteEnable(false);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::None);

        nvrhi::GraphicsPipelineDesc desc;
        desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
        desc.setVertexShader(fullscreenVertex);
        desc.setPixelShader(skyboxFragment);
        desc.setRenderState(state);
        desc.addBindingLayout(m_SkyboxLayout);

        const nvrhi::FramebufferInfo info = nvrhi::FramebufferInfo()
            .addColorFormat(nvrhi::Format::RGBA16_FLOAT)
            .setDepthFormat(nvrhi::Format::D32);
        m_SkyboxPipeline = m_Device->createGraphicsPipeline(desc, info);
    }

    // IBL passes.
    {
        nvrhi::RenderState state;
        state.depthStencilState.setDepthTestEnable(false);
        state.depthStencilState.setDepthWriteEnable(false);
        state.rasterState.setCullMode(nvrhi::RasterCullMode::None);

        const nvrhi::FramebufferInfo cubeInfo = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA16_FLOAT);
        const nvrhi::FramebufferInfo lutInfo = nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RG16_FLOAT);

        nvrhi::GraphicsPipelineDesc equirect;
        equirect.setPrimType(nvrhi::PrimitiveType::TriangleList);
        equirect.setVertexShader(fullscreenVertex);
        equirect.setPixelShader(equirectFragment);
        equirect.setRenderState(state);
        equirect.addBindingLayout(m_EquirectLayout);
        m_EquirectPipeline = m_Device->createGraphicsPipeline(equirect, cubeInfo);

        nvrhi::GraphicsPipelineDesc irradiance;
        irradiance.setPrimType(nvrhi::PrimitiveType::TriangleList);
        irradiance.setVertexShader(fullscreenVertex);
        irradiance.setPixelShader(irradianceFragment);
        irradiance.setRenderState(state);
        irradiance.addBindingLayout(m_IrradianceLayout);
        m_IrradiancePipeline = m_Device->createGraphicsPipeline(irradiance, cubeInfo);

        nvrhi::GraphicsPipelineDesc prefilter;
        prefilter.setPrimType(nvrhi::PrimitiveType::TriangleList);
        prefilter.setVertexShader(fullscreenVertex);
        prefilter.setPixelShader(prefilterFragment);
        prefilter.setRenderState(state);
        prefilter.addBindingLayout(m_PrefilterLayout);
        m_PrefilterPipeline = m_Device->createGraphicsPipeline(prefilter, cubeInfo);

        nvrhi::GraphicsPipelineDesc brdf;
        brdf.setPrimType(nvrhi::PrimitiveType::TriangleList);
        brdf.setVertexShader(fullscreenVertex);
        brdf.setPixelShader(brdfFragment);
        brdf.setRenderState(state);
        brdf.addBindingLayout(m_BrdfLayout);
        m_BrdfPipeline = m_Device->createGraphicsPipeline(brdf, lutInfo);
    }

    // Bloom chain: prefilter, downsample, upsample and additive combine.
    {
        const nvrhi::ShaderHandle bloomPrefilterFragment =
            LoadShader(m_Device, "bloom_prefilter.frag", nvrhi::ShaderType::Pixel);
        const nvrhi::ShaderHandle bloomDownsampleFragment =
            LoadShader(m_Device, "bloom_downsample.frag", nvrhi::ShaderType::Pixel);
        const nvrhi::ShaderHandle bloomUpsampleFragment =
            LoadShader(m_Device, "bloom_upsample.frag", nvrhi::ShaderType::Pixel);
        const nvrhi::ShaderHandle bloomCombineFragment =
            LoadShader(m_Device, "bloom_combine.frag", nvrhi::ShaderType::Pixel);

        const nvrhi::FramebufferInfo bloomInfo =
            nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA16_FLOAT);

        nvrhi::RenderState opaqueState;
        opaqueState.depthStencilState.setDepthTestEnable(false);
        opaqueState.depthStencilState.setDepthWriteEnable(false);
        opaqueState.rasterState.setCullMode(nvrhi::RasterCullMode::None);

        nvrhi::RenderState additiveState = opaqueState;
        additiveState.blendState.targets[0].enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::One)
            .setDestBlend(nvrhi::BlendFactor::One)
            .setBlendOp(nvrhi::BlendOp::Add);

        auto createBloomPipeline = [&](nvrhi::ShaderHandle fragment, const nvrhi::RenderState& state)
        {
            nvrhi::GraphicsPipelineDesc desc;
            desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
            desc.setVertexShader(fullscreenVertex);
            desc.setPixelShader(fragment);
            desc.setRenderState(state);
            desc.addBindingLayout(m_BloomLayout);
            return m_Device->createGraphicsPipeline(desc, bloomInfo);
        };

        m_BloomPrefilterPipeline = createBloomPipeline(bloomPrefilterFragment, opaqueState);
        m_BloomDownsamplePipeline = createBloomPipeline(bloomDownsampleFragment, opaqueState);
        m_BloomUpsamplePipeline = createBloomPipeline(bloomUpsampleFragment, additiveState);
        m_BloomCombinePipeline = createBloomPipeline(bloomCombineFragment, additiveState);
    }
}

void Renderer::CreateTargets(uint32_t width, uint32_t height)
{
    nvrhi::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = nvrhi::Format::RGBA16_FLOAT;
    colorDesc.isRenderTarget = true;
    colorDesc.isShaderResource = true;
    colorDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    colorDesc.setKeepInitialState(true);
    colorDesc.debugName = "HDR Color";
    m_HdrColor = m_Device->createTexture(colorDesc);

    nvrhi::TextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = nvrhi::Format::D32;
    depthDesc.isShaderResource = true;
    depthDesc.isRenderTarget = true;
    depthDesc.setInitialState(nvrhi::ResourceStates::DepthWrite);
    depthDesc.setKeepInitialState(true);
    depthDesc.debugName = "Scene Depth";
    m_Depth = m_Device->createTexture(depthDesc);

    nvrhi::TextureDesc normalDesc;
    normalDesc.width = width;
    normalDesc.height = height;
    normalDesc.format = nvrhi::Format::RGBA16_FLOAT;
    normalDesc.isRenderTarget = true;
    normalDesc.isShaderResource = true;
    normalDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    normalDesc.setKeepInitialState(true);
    normalDesc.debugName = "View Normals";
    m_Normals = m_Device->createTexture(normalDesc);

    nvrhi::TextureDesc ssaoDesc;
    ssaoDesc.width = width;
    ssaoDesc.height = height;
    ssaoDesc.format = nvrhi::Format::R8_UNORM;
    ssaoDesc.isRenderTarget = true;
    ssaoDesc.isShaderResource = true;
    ssaoDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    ssaoDesc.setKeepInitialState(true);
    ssaoDesc.debugName = "SSAO";
    m_SSAO = m_Device->createTexture(ssaoDesc);
    m_SSAOBlurred = m_Device->createTexture(ssaoDesc);

    nvrhi::TextureDesc shadowDesc;
    shadowDesc.width = kShadowMapSize;
    shadowDesc.height = kShadowMapSize;
    shadowDesc.format = nvrhi::Format::D32;
    shadowDesc.isShaderResource = true;
    shadowDesc.isRenderTarget = true;
    shadowDesc.setInitialState(nvrhi::ResourceStates::DepthWrite);
    shadowDesc.setKeepInitialState(true);
    shadowDesc.debugName = "Directional Shadow Map";
    m_ShadowMap = m_Device->createTexture(shadowDesc);

    m_HdrFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_HdrColor).setDepthAttachment(m_Depth));
    m_PrepassFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_Normals).setDepthAttachment(m_Depth));
    m_SSAOFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_SSAO));
    m_SSAOBlurFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_SSAOBlurred));
    m_ShadowFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().setDepthAttachment(m_ShadowMap));

    // Bloom mip chain: level 0 is half resolution, each next level halves.
    m_BloomTextures.clear();
    m_BloomFramebuffers.clear();
    m_BloomBindingSets.clear();

    uint32_t bloomWidth = std::max(1u, width / 2);
    uint32_t bloomHeight = std::max(1u, height / 2);

    for (uint32_t level = 0; level < kBloomMipCount; ++level)
    {
        nvrhi::TextureDesc desc;
        desc.width = bloomWidth;
        desc.height = bloomHeight;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.isRenderTarget = true;
        desc.isShaderResource = true;
        desc.setInitialState(nvrhi::ResourceStates::ShaderResource);
        desc.setKeepInitialState(true);
        desc.debugName = "Bloom Level";
        const nvrhi::TextureHandle texture = m_Device->createTexture(desc);
        m_BloomTextures.push_back(texture);
        m_BloomFramebuffers.push_back(
            m_Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));

        nvrhi::BindingSetDesc setDesc;
        setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture));
        setDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
        m_BloomBindingSets.push_back(m_Device->createBindingSet(setDesc, m_BloomLayout));

        bloomWidth = std::max(1u, bloomWidth / 2);
        bloomHeight = std::max(1u, bloomHeight / 2);
    }

    // The combine pass writes the HDR colour without a depth attachment, so it
    // needs a framebuffer matching the bloom pipeline format.
    m_BloomCombineFramebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_HdrColor));

    nvrhi::BindingSetDesc hdrSetDesc;
    hdrSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_HdrColor));
    hdrSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
    m_HdrBindingSet = m_Device->createBindingSet(hdrSetDesc, m_BloomLayout);

    m_BindingSetsDirty = true;
}

void Renderer::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    if (width == m_Width && height == m_Height && m_HdrColor)
        return;

    m_Width = width;
    m_Height = height;

    CreateTargets(width, height);
    PublishCompositeSource();
}

void Renderer::PublishCompositeSource()
{
    if (!m_Context || !m_HdrColor)
        return;

    const nvrhi::Object view = m_HdrColor->getNativeView(
        nvrhi::ObjectTypes::VK_ImageView, nvrhi::Format::RGBA16_FLOAT);
    m_Context->SetCompositeSource(reinterpret_cast<VkImageView>(view.integer));
}

void Renderer::SetEnvironment(Ref<TextureData> hdr)
{
    m_PendingHDR = std::move(hdr);
    m_EnvironmentDirty = true;
}

nvrhi::TextureHandle Renderer::GetOrCreateGpuTexture(const Ref<TextureData>& texture)
{
    if (!texture)
        return m_DefaultWhite;

    const auto it = m_GpuTextures.find(texture.get());
    if (it != m_GpuTextures.end())
        return it->second;

    nvrhi::TextureHandle handle;

    if (texture->IsHDR)
    {
        nvrhi::TextureDesc desc;
        desc.width = texture->Width;
        desc.height = texture->Height;
        desc.format = nvrhi::Format::RGBA32_FLOAT;
        desc.isShaderResource = true;
        desc.setInitialState(nvrhi::ResourceStates::ShaderResource);
        desc.setKeepInitialState(true);
        desc.debugName = texture->Source;
        handle = m_Device->createTexture(desc);

        m_CommandList->writeTexture(handle, 0, 0, texture->PixelsFloat.data(),
            texture->Width * sizeof(float) * 4);
    }
    else
    {
        const std::vector<std::vector<uint8_t>> mips = GenerateMipChain(
            texture->Pixels.data(), texture->Width, texture->Height);

        nvrhi::TextureDesc desc;
        desc.width = texture->Width;
        desc.height = texture->Height;
        desc.mipLevels = static_cast<uint32_t>(mips.size());
        desc.format = texture->IsSRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
        desc.isShaderResource = true;
        desc.setInitialState(nvrhi::ResourceStates::ShaderResource);
        desc.setKeepInitialState(true);
        desc.debugName = texture->Source;
        handle = m_Device->createTexture(desc);

        uint32_t mipWidth = texture->Width;
        uint32_t mipHeight = texture->Height;
        for (uint32_t mip = 0; mip < mips.size(); ++mip)
        {
            m_CommandList->writeTexture(handle, 0, mip, mips[mip].data(), mipWidth * 4);
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);
        }
    }

    m_GpuTextures[texture.get()] = handle;
    return handle;
}

Renderer::GpuModel& Renderer::GetOrCreateGpuModel(const std::string& path)
{
    const auto it = m_GpuModels.find(path);
    if (it != m_GpuModels.end())
        return it->second;

    GpuModel& gpu = m_GpuModels[path];

    Ref<Model> model = m_Assets.LoadModel(path);
    if (!model)
        return gpu;

    for (const Mesh& mesh : model->Meshes)
    {
        GpuMesh gpuMesh;

        nvrhi::BufferDesc vertexDesc;
        vertexDesc.byteSize = mesh.Vertices.size() * sizeof(Vertex);
        vertexDesc.isVertexBuffer = true;
        vertexDesc.setInitialState(nvrhi::ResourceStates::VertexBuffer);
        vertexDesc.setKeepInitialState(true);
        vertexDesc.debugName = path + " VB";
        gpuMesh.VertexBuffer = m_Device->createBuffer(vertexDesc);
        m_CommandList->writeBuffer(gpuMesh.VertexBuffer, mesh.Vertices.data(), vertexDesc.byteSize);

        nvrhi::BufferDesc indexDesc;
        indexDesc.byteSize = mesh.Indices.size() * sizeof(uint32_t);
        indexDesc.isIndexBuffer = true;
        indexDesc.setInitialState(nvrhi::ResourceStates::IndexBuffer);
        indexDesc.setKeepInitialState(true);
        indexDesc.debugName = path + " IB";
        gpuMesh.IndexBuffer = m_Device->createBuffer(indexDesc);
        m_CommandList->writeBuffer(gpuMesh.IndexBuffer, mesh.Indices.data(), indexDesc.byteSize);

        gpuMesh.Submeshes = mesh.Submeshes;
        gpu.Meshes.push_back(std::move(gpuMesh));
    }

    gpu.Materials = model->Materials;
    for (const Ref<TextureData>& texture : model->Textures)
        gpu.Textures.push_back(GetOrCreateGpuTexture(texture));

    m_BindingSetsDirty = true;
    return gpu;
}

void Renderer::CreateMaterialBindingSets()
{
    m_MaterialBindingSets.clear();

    for (const auto& [path, model] : m_GpuModels)
    {
        for (size_t index = 0; index < model.Materials.size(); ++index)
        {
            const Material& material = model.Materials[index];

            auto textureAt = [&](int32_t textureIndex, nvrhi::TextureHandle fallback)
            {
                if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < model.Textures.size())
                {
                    nvrhi::TextureHandle texture = model.Textures[textureIndex];
                    return texture ? texture : fallback;
                }
                return fallback;
            };

            std::array<nvrhi::BindingSetHandle, 2> sets;
            const std::string key = path + "#" + std::to_string(index);

            for (uint32_t frame = 0; frame < 2; ++frame)
            {
            nvrhi::BindingSetDesc desc;
            desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[frame]));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, textureAt(material.AlbedoTexture, m_DefaultWhite)));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, textureAt(material.NormalTexture, m_DefaultNormal)));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(2, textureAt(material.MetallicRoughnessTexture, m_DefaultWhite)));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(3, textureAt(material.OcclusionTexture, m_DefaultWhite)));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(4, textureAt(material.EmissiveTexture, m_DefaultBlack)));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(5, m_ShadowMap));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(6, m_IrradianceCube));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(7, m_PrefilterCube));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(8, m_BrdfLUT));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(9, m_SSAOBlurred));

            desc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearRepeat));
            desc.addItem(nvrhi::BindingSetItem::Sampler(1, m_ShadowSampler));
            desc.addItem(nvrhi::BindingSetItem::Sampler(2, m_CubeSampler));

            sets[frame] = m_Device->createBindingSet(desc, m_MaterialLayout);
            }

            m_MaterialBindingSets[key] = sets;
        }
    }

    m_BindingSetsDirty = false;
}

void Renderer::GenerateBRDFLUT()
{
    const nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(m_BrdfLUT));

    const nvrhi::GraphicsState state = nvrhi::GraphicsState()
        .setPipeline(m_BrdfPipeline)
        .setFramebuffer(framebuffer)
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
            nvrhi::Viewport(static_cast<float>(kBrdfLutSize), static_cast<float>(kBrdfLutSize))));

    m_CommandList->setGraphicsState(state);
    m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));

    m_BrdfLUTReady = true;
    m_BindingSetsDirty = true;
}

void Renderer::GenerateIBL()
{
    if (!m_PendingHDR || !m_PendingHDR->IsHDR)
    {
        SLATE_WARN("Cannot generate IBL: HDR panorama missing");
        m_EnvironmentDirty = false;
        return;
    }

    const Ref<TextureData>& hdr = m_PendingHDR;

    // Upload the equirectangular panorama.
    nvrhi::TextureDesc equirectDesc;
    equirectDesc.width = hdr->Width;
    equirectDesc.height = hdr->Height;
    equirectDesc.format = nvrhi::Format::RGBA32_FLOAT;
    equirectDesc.isShaderResource = true;
    equirectDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    equirectDesc.setKeepInitialState(true);
    equirectDesc.debugName = "Equirectangular HDR";
    const nvrhi::TextureHandle equirect = m_Device->createTexture(equirectDesc);
    m_CommandList->writeTexture(equirect, 0, 0, hdr->PixelsFloat.data(), hdr->Width * sizeof(float) * 4);

    // Environment cubemap.
    nvrhi::TextureDesc envDesc;
    envDesc.width = kEnvironmentCubeSize;
    envDesc.height = kEnvironmentCubeSize;
    envDesc.dimension = nvrhi::TextureDimension::TextureCube;
    envDesc.arraySize = 6;
    envDesc.mipLevels = 1;
    envDesc.format = nvrhi::Format::RGBA16_FLOAT;
    envDesc.isShaderResource = true;
    envDesc.isRenderTarget = true;
    envDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    envDesc.setKeepInitialState(true);
    envDesc.debugName = "Environment Cube";
    m_EnvironmentCube = m_Device->createTexture(envDesc);

    // Irradiance cubemap.
    nvrhi::TextureDesc irradianceDesc = envDesc;
    irradianceDesc.width = kIrradianceCubeSize;
    irradianceDesc.height = kIrradianceCubeSize;
    irradianceDesc.debugName = "Irradiance Cube";
    m_IrradianceCube = m_Device->createTexture(irradianceDesc);

    // Prefiltered specular cubemap with a mip chain.
    nvrhi::TextureDesc prefilterDesc = envDesc;
    prefilterDesc.width = kPrefilterCubeSize;
    prefilterDesc.height = kPrefilterCubeSize;
    prefilterDesc.mipLevels = kPrefilterMipCount;
    prefilterDesc.debugName = "Prefilter Cube";
    m_PrefilterCube = m_Device->createTexture(prefilterDesc);

    nvrhi::BindingSetDesc equirectSetDesc;
    equirectSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, equirect));
    equirectSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
    m_EquirectBindingSet = m_Device->createBindingSet(equirectSetDesc, m_EquirectLayout);

    nvrhi::BindingSetDesc envSetDesc;
    envSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_EnvironmentCube));
    envSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_CubeSampler));
    m_IrradianceBindingSet = m_Device->createBindingSet(envSetDesc, m_IrradianceLayout);
    m_PrefilterBindingSet = m_Device->createBindingSet(envSetDesc, m_PrefilterLayout);

    // Equirect -> cubemap.
    for (int face = 0; face < 6; ++face)
    {
        const nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(
                nvrhi::FramebufferAttachment().setTexture(m_EnvironmentCube).setArraySlice(face).setMipLevel(0)));

        const nvrhi::GraphicsState state = nvrhi::GraphicsState()
            .setPipeline(m_EquirectPipeline)
            .setFramebuffer(framebuffer)
            .addBindingSet(m_EquirectBindingSet)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<float>(kEnvironmentCubeSize), static_cast<float>(kEnvironmentCubeSize))));

        m_CommandList->setGraphicsState(state);
        m_CommandList->setPushConstants(&face, sizeof(face));
        m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    // Irradiance convolution.
    for (int face = 0; face < 6; ++face)
    {
        const nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(
                nvrhi::FramebufferAttachment().setTexture(m_IrradianceCube).setArraySlice(face).setMipLevel(0)));

        const nvrhi::GraphicsState state = nvrhi::GraphicsState()
            .setPipeline(m_IrradiancePipeline)
            .setFramebuffer(framebuffer)
            .addBindingSet(m_IrradianceBindingSet)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<float>(kIrradianceCubeSize), static_cast<float>(kIrradianceCubeSize))));

        m_CommandList->setGraphicsState(state);
        m_CommandList->setPushConstants(&face, sizeof(face));
        m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    // GGX prefiltering per roughness mip.
    for (uint32_t mip = 0; mip < kPrefilterMipCount; ++mip)
    {
        const float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMipCount - 1);
        const uint32_t mipSize = std::max(1u, kPrefilterCubeSize >> mip);

        for (int face = 0; face < 6; ++face)
        {
            const nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(
                nvrhi::FramebufferDesc().addColorAttachment(
                    nvrhi::FramebufferAttachment().setTexture(m_PrefilterCube).setArraySlice(face).setMipLevel(mip)));

            const nvrhi::GraphicsState state = nvrhi::GraphicsState()
                .setPipeline(m_PrefilterPipeline)
                .setFramebuffer(framebuffer)
                .addBindingSet(m_PrefilterBindingSet)
                .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                    nvrhi::Viewport(static_cast<float>(mipSize), static_cast<float>(mipSize))));

            const struct
            {
                int face;
                float roughness;
                float sourceMip;
            } pushConstants { face, roughness, 0.0f };

            m_CommandList->setGraphicsState(state);
            m_CommandList->setPushConstants(&pushConstants, sizeof(pushConstants));
            m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
        }
    }

    m_IBLReady = true;
    m_EnvironmentDirty = false;
    m_BindingSetsDirty = true;

    SLATE_INFO("Generated IBL from '{}' ({}x{})", hdr->Source, hdr->Width, hdr->Height);
}

void Renderer::ComputeFrameConstants(Scene& scene, const CameraData& camera)
{
    const Environment& environment = scene.GetEnvironment();

    m_FrameConstants.ViewProjection = camera.ViewProjection;
    m_FrameConstants.View = camera.View;
    m_FrameConstants.Projection = camera.Projection;
    m_FrameConstants.InvViewProjection = glm::inverse(camera.ViewProjection);
    m_FrameConstants.InvProjection = glm::inverse(camera.Projection);
    m_FrameConstants.CameraPosition = glm::vec4(camera.Position, 1.0f);

    m_FrameConstants.LightDirection = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    m_FrameConstants.LightColor = glm::vec4(1.0f);
    m_FrameConstants.AmbientColor = glm::vec4(environment.AmbientColor, environment.AmbientIntensity);
    m_FrameConstants.ShadowParams = glm::vec4(1.0f / static_cast<float>(kShadowMapSize), 0.0f, 0.0f, 0.0f);
    m_FrameConstants.ScreenParams = glm::vec4(
        static_cast<float>(m_Width), static_cast<float>(m_Height),
        1.0f / static_cast<float>(m_Width), 1.0f / static_cast<float>(m_Height));
    m_FrameConstants.Exposure = glm::vec4(environment.Exposure);
    m_FrameConstants.IBLParams = glm::vec4(
        static_cast<float>(kPrefilterMipCount - 1),
        static_cast<float>(m_EnvironmentCube ? 0.0f : 0.0f),
        0.0f, 0.0f);

    // Directional light.
    Entity lightEntity;
    scene.GetRegistry().Each<DirectionalLightComponent>([&](Entity entity, DirectionalLightComponent& light)
    {
        if (!lightEntity.IsValid())
            lightEntity = entity;
    });

    if (lightEntity.IsValid())
    {
        const auto& light = lightEntity.GetComponent<DirectionalLightComponent>();
        glm::vec3 direction = light.Direction;

        if (lightEntity.HasComponent<TransformComponent>())
        {
            const glm::mat4& world = lightEntity.GetComponent<TransformComponent>().WorldMatrix;
            direction = glm::mat3(world) * direction;
        }

        if (glm::length(direction) > 1e-5f)
            direction = glm::normalize(direction);

        m_FrameConstants.LightDirection = glm::vec4(direction, light.Intensity);
        m_FrameConstants.LightColor = glm::vec4(light.Color, 1.0f);
    }

    // Point lights (up to 8).
    uint32_t pointLightCount = 0;
    scene.GetRegistry().Each<PointLightComponent>([&](Entity entity, PointLightComponent& light)
    {
        if (pointLightCount >= 8)
            return;

        glm::vec3 position(0.0f);
        if (entity.HasComponent<TransformComponent>())
            position = glm::vec3(entity.GetComponent<TransformComponent>().WorldMatrix[3]);

        m_FrameConstants.PointLightPosition[pointLightCount] = glm::vec4(position, light.Range);
        m_FrameConstants.PointLightColor[pointLightCount] = glm::vec4(light.Color, light.Intensity);
        ++pointLightCount;
    });
    m_FrameConstants.IBLParams.z = static_cast<float>(pointLightCount);

    ComputeLightMatrices(scene);
}

void Renderer::ComputeLightMatrices(Scene& scene)
{
    // Fit an orthographic shadow frustum around the scene bounds.
    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    bool hasGeometry = false;

    scene.GetRegistry().Each<MeshRendererComponent, TransformComponent>(
        [&](Entity entity, MeshRendererComponent& renderer, TransformComponent& transform)
        {
            if (!renderer.Visible || renderer.ModelPath.empty())
                return;

            Ref<Model> model = m_Assets.LoadModel(renderer.ModelPath);
            if (!model || model->Meshes.empty())
                return;

            glm::vec3 modelMin(std::numeric_limits<float>::max());
            glm::vec3 modelMax(std::numeric_limits<float>::lowest());
            for (const Mesh& mesh : model->Meshes)
            {
                modelMin = glm::min(modelMin, mesh.BoundsMin);
                modelMax = glm::max(modelMax, mesh.BoundsMax);
            }

            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point(
                    (corner & 1) ? modelMax.x : modelMin.x,
                    (corner & 2) ? modelMax.y : modelMin.y,
                    (corner & 4) ? modelMax.z : modelMin.z);
                const glm::vec3 world = TransformPoint(transform.WorldMatrix, point);
                boundsMin = glm::min(boundsMin, world);
                boundsMax = glm::max(boundsMax, world);
                hasGeometry = true;
            }
        });

    if (!hasGeometry)
    {
        boundsMin = glm::vec3(-10.0f);
        boundsMax = glm::vec3(10.0f);
    }

    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
    const float radius = glm::length(boundsMax - boundsMin) * 0.5f + 0.5f;

    const glm::vec3 lightDirection = glm::normalize(glm::vec3(m_FrameConstants.LightDirection));
    const glm::vec3 lightPosition = center - lightDirection * radius * 2.0f;

    const glm::vec3 up = std::abs(lightDirection.y) > 0.99f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat4 lightView = glm::lookAt(lightPosition, center, up);

    glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);

    m_FrameConstants.LightViewProjection = lightProjection * lightView;
}

void Renderer::DrawShadowPass(Scene& scene)
{
    m_CommandList->clearDepthStencilTexture(m_ShadowMap, nvrhi::AllSubresources, true, 1.0f, false, 0);

    scene.GetRegistry().Each<MeshRendererComponent, TransformComponent>(
        [&](Entity entity, MeshRendererComponent& renderer, TransformComponent& transform)
        {
            if (!renderer.Visible || !renderer.CastShadows || renderer.ModelPath.empty())
                return;

            GpuModel& model = GetOrCreateGpuModel(renderer.ModelPath);
            const uint32_t frame = m_Context->GetFrameIndex();

            for (GpuMesh& mesh : model.Meshes)
            {
                ObjectPushConstants pushConstants;
                pushConstants.Model = transform.WorldMatrix;

                nvrhi::GraphicsState state = nvrhi::GraphicsState()
                    .setPipeline(m_ShadowPipeline)
                    .setFramebuffer(m_ShadowFramebuffer)
                    .addBindingSet(m_ShadowBindingSets[frame])
                    .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                        nvrhi::Viewport(static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize))));

                state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(mesh.VertexBuffer).setSlot(0).setOffset(0));
                state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(mesh.IndexBuffer)
                    .setFormat(nvrhi::Format::R32_UINT).setOffset(0));

                m_CommandList->setGraphicsState(state);
                m_CommandList->setPushConstants(&pushConstants, sizeof(pushConstants));

                for (const Submesh& submesh : mesh.Submeshes)
                {
                    m_CommandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount)
                        .setStartIndexLocation(submesh.FirstIndex));
                    ++m_Statistics.DrawCalls;
                }
            }
        });
}

void Renderer::DrawPrepass(Scene& scene)
{
    m_CommandList->clearTextureFloat(m_Normals, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
    m_CommandList->clearDepthStencilTexture(m_Depth, nvrhi::AllSubresources, true, 1.0f, false, 0);

    scene.GetRegistry().Each<MeshRendererComponent, TransformComponent>(
        [&](Entity entity, MeshRendererComponent& renderer, TransformComponent& transform)
        {
            if (!renderer.Visible || renderer.ModelPath.empty())
                return;

            GpuModel& model = GetOrCreateGpuModel(renderer.ModelPath);
            const uint32_t frame = m_Context->GetFrameIndex();

            for (GpuMesh& mesh : model.Meshes)
            {
                ObjectPushConstants pushConstants;
                pushConstants.Model = transform.WorldMatrix;

                nvrhi::GraphicsState state = nvrhi::GraphicsState()
                    .setPipeline(m_PrepassPipeline)
                    .setFramebuffer(m_PrepassFramebuffer)
                    .addBindingSet(m_PrepassBindingSets[frame])
                    .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                        nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

                state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(mesh.VertexBuffer).setSlot(0).setOffset(0));
                state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(mesh.IndexBuffer)
                    .setFormat(nvrhi::Format::R32_UINT).setOffset(0));

                m_CommandList->setGraphicsState(state);
                m_CommandList->setPushConstants(&pushConstants, sizeof(pushConstants));

                for (const Submesh& submesh : mesh.Submeshes)
                {
                    m_CommandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount)
                        .setStartIndexLocation(submesh.FirstIndex));
                    ++m_Statistics.DrawCalls;
                }
            }
        });
}

void Renderer::DrawSSAO()
{
    const uint32_t frame = m_Context->GetFrameIndex();
    {
        nvrhi::BindingSetDesc desc;
        desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[frame]));
        desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_Depth));
        desc.addItem(nvrhi::BindingSetItem::Texture_SRV(1, m_Normals));
        desc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(desc, m_SSAOLayout);

        const nvrhi::GraphicsState state = nvrhi::GraphicsState()
            .setPipeline(m_SSAOPipeline)
            .setFramebuffer(m_SSAOFramebuffer)
            .addBindingSet(bindingSet)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

        m_CommandList->setGraphicsState(state);
        m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
        ++m_Statistics.DrawCalls;
    }

    {
        nvrhi::BindingSetDesc desc;
        desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[frame]));
        desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_SSAO));
        desc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
        const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(desc, m_BlurLayout);

        const nvrhi::GraphicsState state = nvrhi::GraphicsState()
            .setPipeline(m_SSAOBlurPipeline)
            .setFramebuffer(m_SSAOBlurFramebuffer)
            .addBindingSet(bindingSet)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

        m_CommandList->setGraphicsState(state);
        m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
        ++m_Statistics.DrawCalls;
    }
}

void Renderer::DrawSkybox()
{
    if (!m_EnvironmentCube)
        return;

    const uint32_t frame = m_Context->GetFrameIndex();

    nvrhi::BindingSetDesc desc;
    desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_FrameCB[frame]));
    desc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_EnvironmentCube));
    desc.addItem(nvrhi::BindingSetItem::Sampler(0, m_CubeSampler));
    const nvrhi::BindingSetHandle bindingSet = m_Device->createBindingSet(desc, m_SkyboxLayout);

    const nvrhi::GraphicsState state = nvrhi::GraphicsState()
        .setPipeline(m_SkyboxPipeline)
        .setFramebuffer(m_HdrFramebuffer)
        .addBindingSet(bindingSet)
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
            nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

    m_CommandList->setGraphicsState(state);
    m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    ++m_Statistics.DrawCalls;
}

void Renderer::DrawOpaque(Scene& scene)
{
    scene.GetRegistry().Each<MeshRendererComponent, TransformComponent>(
        [&](Entity entity, MeshRendererComponent& renderer, TransformComponent& transform)
        {
            if (!renderer.Visible || renderer.ModelPath.empty())
                return;

            GpuModel& model = GetOrCreateGpuModel(renderer.ModelPath);
            ++m_Statistics.Entities;

            for (GpuMesh& mesh : model.Meshes)
            {
                for (const Submesh& submesh : mesh.Submeshes)
                {
                    const uint32_t materialIndex = submesh.MaterialIndex >= 0
                        ? static_cast<uint32_t>(submesh.MaterialIndex) : 0u;

                    Material material;
                    if (materialIndex < model.Materials.size())
                        material = model.Materials[materialIndex];

                    ObjectPushConstants pushConstants;
                    pushConstants.Model = transform.WorldMatrix;
                    pushConstants.BaseColor = material.BaseColor * renderer.Tint;
                    pushConstants.Params = glm::vec4(material.Metallic, material.Roughness,
                        material.EmissiveStrength, material.NormalTexture >= 0 ? 1.0f : 0.0f);
                    pushConstants.Emissive = glm::vec4(material.Emissive, 0.0f);

                    const std::string key = renderer.ModelPath + "#" + std::to_string(materialIndex);
                    const auto bindingIt = m_MaterialBindingSets.find(key);
                    if (bindingIt == m_MaterialBindingSets.end())
                        continue;

                    const uint32_t frame = m_Context->GetFrameIndex();
                    const nvrhi::BindingSetHandle materialSet = bindingIt->second[frame];

                    nvrhi::GraphicsState state = nvrhi::GraphicsState()
                        .setPipeline(m_PbrPipeline)
                        .setFramebuffer(m_HdrFramebuffer)
                        .addBindingSet(materialSet)
                        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                            nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

                    state.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(mesh.VertexBuffer).setSlot(0).setOffset(0));
                    state.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(mesh.IndexBuffer)
                        .setFormat(nvrhi::Format::R32_UINT).setOffset(0));

                    m_CommandList->setGraphicsState(state);
                    m_CommandList->setPushConstants(&pushConstants, sizeof(pushConstants));
                    m_CommandList->drawIndexed(nvrhi::DrawArguments().setVertexCount(submesh.IndexCount)
                        .setStartIndexLocation(submesh.FirstIndex));

                    ++m_Statistics.DrawCalls;
                    m_Statistics.Triangles += submesh.IndexCount / 3;
                }
            }
        });
}

void Renderer::DrawBloom()
{
    if (!m_BloomEnabled || m_BloomTextures.empty() || !m_BloomCombineFramebuffer)
        return;

    BloomPushConstants constants;
    constants.Threshold = m_BloomThreshold;
    constants.Knee = 0.6f;
    constants.Intensity = m_BloomIntensity;

    auto drawFullscreen = [&](nvrhi::GraphicsPipelineHandle pipeline,
                              nvrhi::FramebufferHandle framebuffer,
                              nvrhi::BindingSetHandle bindingSet,
                              uint32_t width, uint32_t height)
    {
        const nvrhi::GraphicsState state = nvrhi::GraphicsState()
            .setPipeline(pipeline)
            .setFramebuffer(framebuffer)
            .addBindingSet(bindingSet)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(static_cast<float>(width), static_cast<float>(height))));

        m_CommandList->setGraphicsState(state);
        m_CommandList->setPushConstants(&constants, sizeof(constants));
        m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    };

    // Extract bright pixels into level 0.
    const auto& level0 = m_BloomTextures[0]->getDesc();
    drawFullscreen(m_BloomPrefilterPipeline, m_BloomFramebuffers[0], m_HdrBindingSet,
        level0.width, level0.height);

    // Build the mip chain.
    for (uint32_t level = 1; level < m_BloomTextures.size(); ++level)
    {
        const auto& desc = m_BloomTextures[level]->getDesc();
        drawFullscreen(m_BloomDownsamplePipeline, m_BloomFramebuffers[level],
            m_BloomBindingSets[level - 1], desc.width, desc.height);
    }

    // Upsample additively back up the chain.
    for (uint32_t level = static_cast<uint32_t>(m_BloomTextures.size()) - 1; level > 0; --level)
    {
        const auto& desc = m_BloomTextures[level - 1]->getDesc();
        drawFullscreen(m_BloomUpsamplePipeline, m_BloomFramebuffers[level - 1],
            m_BloomBindingSets[level], desc.width, desc.height);
    }

    // Add the bloom back into the HDR scene target.
    drawFullscreen(m_BloomCombinePipeline, m_BloomCombineFramebuffer, m_BloomBindingSets[0],
        m_Width, m_Height);
}


void Renderer::CreateUIResources()
{
    m_Font.LoadFromFile("Fonts/JetBrainsMono-Regular.ttf", 48.0f);

    nvrhi::TextureDesc fontDesc;
    fontDesc.width = m_Font.GetWidth();
    fontDesc.height = m_Font.GetHeight();
    fontDesc.format = nvrhi::Format::RGBA8_UNORM;
    fontDesc.isShaderResource = true;
    fontDesc.setInitialState(nvrhi::ResourceStates::ShaderResource);
    fontDesc.setKeepInitialState(true);
    fontDesc.debugName = "UI Font Atlas";
    m_UIFontTexture = m_Device->createTexture(fontDesc);

    m_CommandList->open();
    m_CommandList->writeTexture(m_UIFontTexture, 0, 0, m_Font.GetPixels().data(),
        m_Font.GetWidth() * 4);
    m_CommandList->close();
    m_Context->ExecuteCommandList(m_CommandList);

    nvrhi::BindingLayoutDesc layout;
    // The UI vertex shader consumes the screen size push constant.
    layout.setVisibility(nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel);
    layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
    layout.addItem(nvrhi::BindingLayoutItem::Sampler(0));
    layout.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(UIConstants)));
    m_UILayout = m_Device->createBindingLayout(layout);

    nvrhi::BindingSetDesc setDesc;
    setDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_UIFontTexture));
    setDesc.addItem(nvrhi::BindingSetItem::Sampler(0, m_LinearClamp));
    m_UIBindingSet = m_Device->createBindingSet(setDesc, m_UILayout);

    const nvrhi::ShaderHandle vertexShader = LoadShader(m_Device, "ui.vert", nvrhi::ShaderType::Vertex);
    const nvrhi::ShaderHandle pixelShader = LoadShader(m_Device, "ui.frag", nvrhi::ShaderType::Pixel);

    nvrhi::VertexAttributeDesc attributes[3];
    attributes[0] = nvrhi::VertexAttributeDesc()
        .setName("POSITION")
        .setFormat(nvrhi::Format::RG32_FLOAT)
        .setOffset(offsetof(UIVertex, Position))
        .setElementStride(sizeof(UIVertex));
    attributes[1] = nvrhi::VertexAttributeDesc()
        .setName("TEXCOORD")
        .setFormat(nvrhi::Format::RG32_FLOAT)
        .setOffset(offsetof(UIVertex, UV))
        .setElementStride(sizeof(UIVertex));
    attributes[2] = nvrhi::VertexAttributeDesc()
        .setName("COLOR")
        .setFormat(nvrhi::Format::RGBA32_FLOAT)
        .setOffset(offsetof(UIVertex, Color))
        .setElementStride(sizeof(UIVertex));

    nvrhi::RenderState state;
    state.depthStencilState.setDepthTestEnable(false);
    state.depthStencilState.setDepthWriteEnable(false);
    state.rasterState.setCullMode(nvrhi::RasterCullMode::None);
    state.blendState.targets[0].enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setBlendOp(nvrhi::BlendOp::Add);

    nvrhi::GraphicsPipelineDesc desc;
    desc.setPrimType(nvrhi::PrimitiveType::TriangleList);
    desc.setVertexShader(vertexShader);
    desc.setPixelShader(pixelShader);
    desc.setInputLayout(m_Device->createInputLayout(attributes, 3, vertexShader));
    desc.setRenderState(state);
    desc.addBindingLayout(m_UILayout);

    const nvrhi::FramebufferInfo info =
        nvrhi::FramebufferInfo().addColorFormat(nvrhi::Format::RGBA16_FLOAT);
    m_UIPipeline = m_Device->createGraphicsPipeline(desc, info);

    m_UIVertexCapacity = 16384;
    nvrhi::BufferDesc bufferDesc;
    bufferDesc.byteSize = m_UIVertexCapacity * sizeof(UIVertex);
    bufferDesc.isVertexBuffer = true;
    bufferDesc.setInitialState(nvrhi::ResourceStates::VertexBuffer);
    bufferDesc.setKeepInitialState(true);
    bufferDesc.debugName = "UI Vertices";
    m_UIVertexBuffer = m_Device->createBuffer(bufferDesc);
}

void Renderer::BeginUIFrame()
{
    m_UIVertices.clear();
}

void Renderer::AddUIQuad(float x, float y, float width, float height,
    float u0, float v0, float u1, float v1, const glm::vec4& color)
{
    const UIVertex topLeft { glm::vec2(x, y), glm::vec2(u0, v0), color };
    const UIVertex topRight { glm::vec2(x + width, y), glm::vec2(u1, v0), color };
    const UIVertex bottomRight { glm::vec2(x + width, y + height), glm::vec2(u1, v1), color };
    const UIVertex bottomLeft { glm::vec2(x, y + height), glm::vec2(u0, v1), color };

    m_UIVertices.push_back(topLeft);
    m_UIVertices.push_back(topRight);
    m_UIVertices.push_back(bottomRight);
    m_UIVertices.push_back(topLeft);
    m_UIVertices.push_back(bottomRight);
    m_UIVertices.push_back(bottomLeft);
}

void Renderer::DrawUIRect(float x, float y, float width, float height, const glm::vec4& color)
{
    AddUIQuad(x, y, width, height, m_Font.GetWhiteU(), m_Font.GetWhiteV(),
        m_Font.GetWhiteU(), m_Font.GetWhiteV(), color);
}

void Renderer::DrawUIText(float x, float y, std::string_view text, float size,
    const glm::vec4& color)
{
    const float scale = size / m_Font.GetPixelHeight();
    const float lineHeight = m_Font.GetLineHeight() * scale;

    float penX = x;
    float baseline = y + m_Font.GetAscent() * scale;

    for (const char character : text)
    {
        if (character == '\n')
        {
            penX = x;
            baseline += lineHeight;
            continue;
        }

        const FontAtlas::Glyph* glyph = m_Font.GetGlyph(character);
        if (!glyph)
            continue;

        if (glyph->Width > 0.0f && glyph->Height > 0.0f)
        {
            AddUIQuad(penX + glyph->XOffset * scale,
                baseline + glyph->YOffset * scale,
                glyph->Width * scale,
                glyph->Height * scale,
                glyph->U0, glyph->V0, glyph->U1, glyph->V1, color);
        }

        penX += glyph->XAdvance * scale;
    }
}

void Renderer::DrawUI()
{
    if (m_UIVertices.empty() || !m_UIPipeline || !m_BloomCombineFramebuffer)
        return;

    if (m_UIVertices.size() > m_UIVertexCapacity)
    {
        m_UIVertexCapacity = static_cast<uint32_t>(m_UIVertices.size() * 2);
        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize = m_UIVertexCapacity * sizeof(UIVertex);
        bufferDesc.isVertexBuffer = true;
        bufferDesc.setInitialState(nvrhi::ResourceStates::VertexBuffer);
        bufferDesc.setKeepInitialState(true);
        bufferDesc.debugName = "UI Vertices";
        m_UIVertexBuffer = m_Device->createBuffer(bufferDesc);
    }

    m_CommandList->writeBuffer(m_UIVertexBuffer, m_UIVertices.data(),
        m_UIVertices.size() * sizeof(UIVertex));

    UIConstants constants;
    constants.ScreenSize = GetViewportSize();

    const nvrhi::GraphicsState state = nvrhi::GraphicsState()
        .setPipeline(m_UIPipeline)
        .setFramebuffer(m_BloomCombineFramebuffer)
        .addBindingSet(m_UIBindingSet)
        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(m_UIVertexBuffer).setSlot(0).setOffset(0))
        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
            nvrhi::Viewport(static_cast<float>(m_Width), static_cast<float>(m_Height))));

    m_CommandList->setGraphicsState(state);
    m_CommandList->setPushConstants(&constants, sizeof(constants));
    m_CommandList->draw(nvrhi::DrawArguments().setVertexCount(
        static_cast<uint32_t>(m_UIVertices.size())));
}

void Renderer::Render(Scene& scene)
{
    if (!m_CommandList || !m_HdrColor)
        return;

    scene.UpdateTransforms();

    const float aspect = m_Height > 0 ? static_cast<float>(m_Width) / static_cast<float>(m_Height) : 1.0f;
    const CameraData camera = scene.GetCameraData(aspect);

    // Load the environment if the scene references a new HDR panorama.
    const Environment& environment = scene.GetEnvironment();
    if (!environment.HDRIPath.empty() && environment.HDRIPath != m_LoadedEnvironmentPath)
    {
        Ref<TextureData> hdr = m_Assets.LoadTexture(environment.HDRIPath, false);
        if (hdr)
        {
            m_LoadedEnvironmentPath = environment.HDRIPath;
            SetEnvironment(hdr);
        }
    }

    ComputeFrameConstants(scene, camera);

    m_Statistics = Statistics {};

    m_CommandList->open();
    const uint32_t frameIndex = m_Context->GetFrameIndex();
    m_CommandList->writeBuffer(m_FrameCB[frameIndex], &m_FrameConstants, sizeof(FrameConstants));

    if (!m_BrdfLUTReady)
        GenerateBRDFLUT();

    if (m_EnvironmentDirty)
        GenerateIBL();

    if (m_BindingSetsDirty)
        CreateMaterialBindingSets();

    DrawShadowPass(scene);
    DrawPrepass(scene);
    DrawSSAO();

    m_CommandList->clearTextureFloat(m_HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));

    if (environment.ShowSkybox && m_EnvironmentCube)
        DrawSkybox();

    DrawOpaque(scene);
    DrawBloom();
    DrawUI();

    m_CommandList->close();
    m_Context->ExecuteCommandList(m_CommandList);
}

} // namespace Slate
