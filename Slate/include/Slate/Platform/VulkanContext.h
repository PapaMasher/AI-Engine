#pragma once

#include "Slate/Core/Base.h"

#include <volk.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>

#include <array>

struct GLFWwindow;

namespace Slate {

class Window;

// Owns the Vulkan instance, device and swap chain, and the NVRHI device that
// the renderer records into. The final image is composited (tonemapped) into
// the swap chain with a small hand-written Vulkan pass, followed by the ImGui
// overlay when the editor is active.
//
// Synchronization model (all work on the graphics queue):
//   1. acquire swap chain image with a binary semaphore
//   2. submit an empty wait-only batch so all subsequent work waits for the image
//   3. NVRHI records and submits scene rendering, signalling a timeline semaphore
//   4. the composite + UI pass waits on that timeline semaphore, signals a binary semaphore
//   5. present waits on the binary semaphore
class VulkanContext
{
public:
    static constexpr uint32_t FramesInFlight = 2;

    VulkanContext(Window& window, bool enableValidation);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void BeginFrame();
    void ExecuteCommandList(nvrhi::ICommandList* commandList);
    void Composite();
    void EndFrame();

    void BeginImGuiFrame();
    void RenderImGui();
    void InitImGui();
    void ShutdownImGui();
    bool IsImGuiInitialized() const { return m_ImGuiInitialized; }

    void WaitIdle();
    void RecreateSwapchain();

    // Reads the last presented swap chain image back to the CPU and writes it
    // as a PNG. Must be called between frames.
    bool CaptureScreenshot(const std::string& path);

    // Sets the texture (HDR) that the composite pass tonemaps into the swap
    // chain. The view must stay valid until the next call.
    void SetCompositeSource(VkImageView view);

    nvrhi::IDevice* GetNvrhiDevice() const { return m_NvrhiDevice; }
    VkExtent2D GetSwapchainExtent() const { return m_SwapchainExtent; }
    VkFormat GetSwapchainFormat() const { return m_SwapchainFormat; }
    VkImage GetCurrentSwapchainImage() const { return m_SwapchainImages[m_ImageIndex]; }
    VkImageView GetCurrentSwapchainImageView() const { return m_SwapchainImageViews[m_ImageIndex]; }
    VkRenderPass GetCompositeRenderPass() const { return m_CompositeRenderPass; }
    VkRenderPass GetImGuiRenderPass() const { return m_ImGuiRenderPass; }
    VkCommandBuffer GetCurrentCommandBuffer() const { return m_Frames[m_CurrentFrame].CommandBuffer; }
    VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
    uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
    VkInstance GetInstance() const { return m_Instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    VkDevice GetDevice() const { return m_Device; }
    VkSurfaceKHR GetSurface() const { return m_Surface; }
    uint32_t GetImageCount() const { return m_ImageCount; }
    uint32_t GetMinImageCount() const { return m_MinImageCount; }
    uint32_t GetFrameIndex() const { return m_CurrentFrame; }
    const VkPhysicalDeviceProperties& GetDeviceProperties() const { return m_DeviceProperties; }

    bool IsSwapchainOutOfDate() const { return m_SwapchainOutOfDate; }

private:
    class MessageCallbackImpl : public nvrhi::IMessageCallback
    {
    public:
        void message(nvrhi::MessageSeverity severity, const char* messageText) override;
    };

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateSwapchainImageViews();
    void CreateRenderPasses();
    void CreateCompositePipeline();
    void CreateCompositeFramebuffers();
    void CreateCommandResources();
    void CreateSyncObjects();
    void CreateNvrhiDevice();
    void DestroySwapchain();

    VkShaderModule CreateShaderModule(const std::vector<uint8_t>& code);

    Window& m_Window;
    bool m_ValidationEnabled = false;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_DeviceProperties {};
    uint32_t m_GraphicsQueueFamily = 0;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;

    std::vector<const char*> m_InstanceExtensions;
    std::vector<const char*> m_DeviceExtensions;

    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_SwapchainFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR m_SwapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D m_SwapchainExtent { 0, 0 };
    std::vector<VkImage> m_SwapchainImages;
    std::vector<VkImageView> m_SwapchainImageViews;
    uint32_t m_MinImageCount = 2;
    uint32_t m_ImageCount = 0;

    VkRenderPass m_CompositeRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_ImGuiRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_CompositeFramebuffers;
    std::vector<VkFramebuffer> m_ImGuiFramebuffers;

    VkDescriptorSetLayout m_CompositeDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_CompositeDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_CompositeDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout m_CompositePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_CompositePipeline = VK_NULL_HANDLE;
    VkSampler m_CompositeSampler = VK_NULL_HANDLE;
    VkShaderModule m_CompositeVertexShader = VK_NULL_HANDLE;
    VkShaderModule m_CompositeFragmentShader = VK_NULL_HANDLE;
    VkImageView m_CompositeSourceView = VK_NULL_HANDLE;

    VkCommandPool m_CommandPool = VK_NULL_HANDLE;

    struct FrameResources
    {
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
    };
    std::array<FrameResources, FramesInFlight> m_Frames;

    uint32_t m_CurrentFrame = 0;
    uint32_t m_ImageIndex = 0;
    bool m_FrameActive = false;
    bool m_SwapchainOutOfDate = false;

    // One acquire semaphore per frame in flight, one render-finished semaphore
    // per swap chain image. Indexing the latter by image guarantees the
    // semaphore is free before it is signalled again (the image can only be
    // re-acquired after its previous present completed).
    std::array<VkSemaphore, FramesInFlight> m_AcquireSemaphores {};
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;
    VkSemaphore m_FrameDoneSemaphore = VK_NULL_HANDLE;
    uint64_t m_FrameDoneValue = 0;

    nvrhi::DeviceHandle m_NvrhiDevice;
    nvrhi::vulkan::DeviceHandle m_NvrhiVulkanDevice;
    MessageCallbackImpl m_MessageCallback;

    VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;
    bool m_ImGuiInitialized = false;

    // Temporary shader directory resolution helper.
    std::filesystem::path GetShaderPath(const std::string& name) const;
};

} // namespace Slate
