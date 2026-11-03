#include "Slate/Platform/VulkanContext.h"
#include "Slate/Platform/Window.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/ImageWriter.h"
#include "Slate/Core/Log.h"

#include <volk.h>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <filesystem>
#include <cstring>
#include <set>

// NVRHI's Vulkan backend uses vulkan-hpp's dynamic dispatcher. When NVRHI is
// built as a static library the application must provide storage for the
// default dispatcher and initialize it before creating the NVRHI device.
namespace vk {
namespace detail {
DispatchLoaderDynamic defaultDispatchLoaderDynamic;
} // namespace detail
} // namespace vk

namespace Slate {

namespace {

const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool CheckValidationLayerSupport()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    for (const auto& layer : layers)
    {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0)
            return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    (void)type; (void)userData;

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        SLATE_ERROR("[Vulkan] {}", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        SLATE_WARN("[Vulkan] {}", data->pMessage);
    else
        SLATE_DEBUG("[Vulkan] {}", data->pMessage);

    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* messenger)
{
    auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!function)
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    return function(instance, createInfo, allocator, messenger);
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* allocator)
{
    auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (function)
        function(instance, messenger, allocator);
}

struct SwapchainSupportDetails
{
    VkSurfaceCapabilitiesKHR capabilities {};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0)
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0)
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

} // namespace

void VulkanContext::MessageCallbackImpl::message(nvrhi::MessageSeverity severity, const char* messageText)
{
    switch (severity)
    {
        case nvrhi::MessageSeverity::Info:    SLATE_DEBUG("[NVRHI] {}", messageText); break;
        case nvrhi::MessageSeverity::Warning: SLATE_WARN("[NVRHI] {}", messageText); break;
        case nvrhi::MessageSeverity::Error:   SLATE_ERROR("[NVRHI] {}", messageText); break;
        case nvrhi::MessageSeverity::Fatal:   SLATE_CRITICAL("[NVRHI] {}", messageText); break;
    }
}

VulkanContext::VulkanContext(Window& window, bool enableValidation)
    : m_Window(window)
    , m_ValidationEnabled(enableValidation)
{
    if (volkInitialize() != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to load the Vulkan loader (vulkan-1). Is a Vulkan driver installed?");
        return;
    }

    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateSwapchain();
    CreateSwapchainImageViews();
    CreateRenderPasses();
    CreateCompositePipeline();
    CreateCompositeFramebuffers();
    CreateCommandResources();
    CreateSyncObjects();
    CreateNvrhiDevice();

    SLATE_INFO("Vulkan initialized: {} ({})", m_DeviceProperties.deviceName,
        m_ValidationEnabled ? "validation on" : "validation off");
}

VulkanContext::~VulkanContext()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    WaitIdle();
    ShutdownImGui();

    // Release the NVRHI device before destroying the Vulkan device: NVRHI owns
    // Vulkan objects (descriptor pools, pipeline cache, allocator blocks) that
    // must be freed while the device is still alive.
    m_NvrhiDevice = nullptr;
    m_NvrhiVulkanDevice = nullptr;

    for (auto& frame : m_Frames)
    {
        if (frame.Fence)
            vkDestroyFence(m_Device, frame.Fence, nullptr);
        if (frame.CommandBuffer)
            vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &frame.CommandBuffer);
    }

    if (m_CommandPool)
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    if (m_FrameDoneSemaphore)
        vkDestroySemaphore(m_Device, m_FrameDoneSemaphore, nullptr);
    for (auto semaphore : m_AcquireSemaphores)
    {
        if (semaphore)
            vkDestroySemaphore(m_Device, semaphore, nullptr);
    }

    if (m_CompositeSampler)
        vkDestroySampler(m_Device, m_CompositeSampler, nullptr);
    if (m_CompositePipeline)
        vkDestroyPipeline(m_Device, m_CompositePipeline, nullptr);
    if (m_CompositePipelineLayout)
        vkDestroyPipelineLayout(m_Device, m_CompositePipelineLayout, nullptr);
    if (m_CompositeDescriptorPool)
        vkDestroyDescriptorPool(m_Device, m_CompositeDescriptorPool, nullptr);
    if (m_CompositeDescriptorLayout)
        vkDestroyDescriptorSetLayout(m_Device, m_CompositeDescriptorLayout, nullptr);
    if (m_CompositeVertexShader)
        vkDestroyShaderModule(m_Device, m_CompositeVertexShader, nullptr);
    if (m_CompositeFragmentShader)
        vkDestroyShaderModule(m_Device, m_CompositeFragmentShader, nullptr);

    DestroySwapchain();

    if (m_CompositeRenderPass)
        vkDestroyRenderPass(m_Device, m_CompositeRenderPass, nullptr);
    if (m_ImGuiRenderPass)
        vkDestroyRenderPass(m_Device, m_ImGuiRenderPass, nullptr);

    vkDestroyDevice(m_Device, nullptr);

    if (m_DebugMessenger)
        DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
    if (m_Surface)
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    if (m_Instance)
        vkDestroyInstance(m_Instance, nullptr);
}

void VulkanContext::CreateInstance()
{
    VkApplicationInfo appInfo { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Slate";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Slate";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    for (uint32_t i = 0; i < glfwExtensionCount; ++i)
        m_InstanceExtensions.push_back(glfwExtensions[i]);

    if (m_ValidationEnabled)
    {
        if (!CheckValidationLayerSupport())
        {
            SLATE_WARN("Vulkan validation layers requested but not available; continuing without them");
            m_ValidationEnabled = false;
        }
        else
        {
            m_InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }

    VkInstanceCreateInfo createInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_InstanceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_InstanceExtensions.data();

    if (m_ValidationEnabled)
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayer;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
    if (result != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to create Vulkan instance (VkResult {})", static_cast<int>(result));
        return;
    }

    volkLoadInstance(m_Instance);
}

void VulkanContext::SetupDebugMessenger()
{
    if (!m_ValidationEnabled || !m_Instance)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
        SLATE_WARN("Failed to set up the Vulkan debug messenger");
}

void VulkanContext::CreateSurface()
{
    if (glfwCreateWindowSurface(m_Instance, m_Window.GetNativeWindow(), nullptr, &m_Surface) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the Vulkan window surface");
}

void VulkanContext::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        SLATE_CRITICAL("No Vulkan-capable physical devices found");
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties(device, &properties);

        if (properties.apiVersion < VK_API_VERSION_1_3)
        {
            SLATE_WARN("Skipping '{}': Vulkan 1.3 is required (device reports {}.{}.{})",
                properties.deviceName,
                VK_VERSION_MAJOR(properties.apiVersion),
                VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion));
            continue;
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        bool hasGraphicsAndPresent = false;
        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
            {
                hasGraphicsAndPresent = true;
                break;
            }
        }
        if (!hasGraphicsAndPresent)
            continue;

        const SwapchainSupportDetails support = QuerySwapchainSupport(device, m_Surface);
        if (support.formats.empty() || support.presentModes.empty())
            continue;

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1000;
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 500;
        score += static_cast<int>(properties.limits.maxImageDimension2D);

        if (score > bestScore)
        {
            bestScore = score;
            m_PhysicalDevice = device;
            m_DeviceProperties = properties;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE)
        SLATE_CRITICAL("No suitable Vulkan 1.3 device found");
}

void VulkanContext::CreateLogicalDevice()
{
    if (m_PhysicalDevice == VK_NULL_HANDLE)
        return;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

    bool found = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, i, m_Surface, &presentSupport);
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            m_GraphicsQueueFamily = i;
            found = true;
            break;
        }
    }

    if (!found)
    {
        SLATE_CRITICAL("No graphics+present queue family found");
        return;
    }

    // Query which features are available before requesting them.
    VkPhysicalDeviceFeatures2 supportedFeatures { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan11Features supported11 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features supported12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features supported13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    supportedFeatures.pNext = &supported11;
    supported11.pNext = &supported12;
    supported12.pNext = &supported13;
    vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supportedFeatures);

    VkPhysicalDeviceFeatures2 features2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan11Features features11 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features features12 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features features13 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features2.pNext = &features11;
    features11.pNext = &features12;
    features12.pNext = &features13;

    features2.features.samplerAnisotropy = supportedFeatures.features.samplerAnisotropy;
    features2.features.fillModeNonSolid = supportedFeatures.features.fillModeNonSolid;
    features2.features.independentBlend = supportedFeatures.features.independentBlend;
    features2.features.depthClamp = supportedFeatures.features.depthClamp;
    features2.features.sampleRateShading = supportedFeatures.features.sampleRateShading;
    features2.features.textureCompressionBC = supportedFeatures.features.textureCompressionBC;
    features2.features.shaderStorageImageWriteWithoutFormat = supportedFeatures.features.shaderStorageImageWriteWithoutFormat;
    features2.features.vertexPipelineStoresAndAtomics = supportedFeatures.features.vertexPipelineStoresAndAtomics;
    features2.features.fragmentStoresAndAtomics = supportedFeatures.features.fragmentStoresAndAtomics;

    features11.shaderDrawParameters = supported11.shaderDrawParameters;
    features11.multiview = supported11.multiview;

    features12.timelineSemaphore = supported12.timelineSemaphore;
    features12.bufferDeviceAddress = supported12.bufferDeviceAddress;
    features12.descriptorIndexing = supported12.descriptorIndexing;
    features12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
    features12.shaderStorageBufferArrayNonUniformIndexing = supported12.shaderStorageBufferArrayNonUniformIndexing;
    features12.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
    features12.descriptorBindingVariableDescriptorCount = supported12.descriptorBindingVariableDescriptorCount;
    features12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
    features12.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
    features12.descriptorBindingStorageImageUpdateAfterBind = supported12.descriptorBindingStorageImageUpdateAfterBind;
    features12.descriptorBindingStorageBufferUpdateAfterBind = supported12.descriptorBindingStorageBufferUpdateAfterBind;
    features12.descriptorBindingUniformBufferUpdateAfterBind = supported12.descriptorBindingUniformBufferUpdateAfterBind;
    features12.hostQueryReset = supported12.hostQueryReset;
    features12.scalarBlockLayout = supported12.scalarBlockLayout;
    features12.uniformAndStorageBuffer8BitAccess = supported12.uniformAndStorageBuffer8BitAccess;
    features12.storageBuffer8BitAccess = supported12.storageBuffer8BitAccess;

    features13.synchronization2 = supported13.synchronization2;
    features13.dynamicRendering = supported13.dynamicRendering;
    features13.maintenance4 = supported13.maintenance4;

    if (!features12.timelineSemaphore || !features12.bufferDeviceAddress || !features13.synchronization2)
    {
        SLATE_CRITICAL("The selected GPU lacks required Vulkan 1.2/1.3 features "
            "(timelineSemaphore={}, bufferDeviceAddress={}, synchronization2={})",
            features12.timelineSemaphore, features12.bufferDeviceAddress, features13.synchronization2);
        return;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueCreateInfo.queueFamilyIndex = m_GraphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    m_DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkDeviceCreateInfo createInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

    const VkResult result = vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
    if (result != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to create the Vulkan device (VkResult {})", static_cast<int>(result));
        return;
    }

    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
    volkLoadDevice(m_Device);

    // Initialize vulkan-hpp's dynamic dispatcher for NVRHI.
    vk::detail::defaultDispatchLoaderDynamic.init(m_Instance, vkGetInstanceProcAddr);
    vk::detail::defaultDispatchLoaderDynamic.init(m_Instance, vkGetInstanceProcAddr, m_Device, vkGetDeviceProcAddr);
}

void VulkanContext::CreateSwapchain()
{
    const SwapchainSupportDetails support = QuerySwapchainSupport(m_PhysicalDevice, m_Surface);

    VkSurfaceFormatKHR surfaceFormat = support.formats[0];
    for (const auto& format : support.formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = format;
            break;
        }
    }
    m_SwapchainFormat = surfaceFormat.format;
    m_SwapchainColorSpace = surfaceFormat.colorSpace;

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_Window.IsVSync())
    {
        for (const auto& mode : support.presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = mode;
                break;
            }
        }
    }

    VkExtent2D extent = support.capabilities.currentExtent;
    if (extent.width == UINT32_MAX)
    {
        extent.width = std::clamp(m_Window.GetWidth(), support.capabilities.minImageExtent.width,
            support.capabilities.maxImageExtent.width);
        extent.height = std::clamp(m_Window.GetHeight(), support.capabilities.minImageExtent.height,
            support.capabilities.maxImageExtent.height);
    }

    m_SwapchainExtent = extent;
    m_MinImageCount = support.capabilities.minImageCount;

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_SwapchainFormat;
    createInfo.imageColorSpace = m_SwapchainColorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = m_Swapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &newSwapchain) != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to create the Vulkan swap chain");
        return;
    }

    if (m_Swapchain)
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
    m_Swapchain = newSwapchain;

    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &m_ImageCount, nullptr);
    m_SwapchainImages.resize(m_ImageCount);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &m_ImageCount, m_SwapchainImages.data());

    m_SwapchainOutOfDate = false;
}

void VulkanContext::CreateSwapchainImageViews()
{
    m_SwapchainImageViews.resize(m_SwapchainImages.size());

    for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
    {
        VkImageViewCreateInfo createInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        createInfo.image = m_SwapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_SwapchainFormat;
        createInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create a swap chain image view");
    }

    // One render-finished semaphore per image: presenting image N waits on
    // semaphore N, and the semaphore cannot be reused until image N is
    // acquired again (which implies its present completed).
    VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    m_RenderFinishedSemaphores.resize(m_SwapchainImages.size());
    for (auto& semaphore : m_RenderFinishedSemaphores)
    {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create a render-finished semaphore");
    }
}

void VulkanContext::CreateRenderPasses()
{
    VkAttachmentDescription colorAttachment {};
    colorAttachment.format = m_SwapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;

    // Composite pass: clear and draw the tonemapped scene, leaving the image
    // ready for the UI pass.
    VkAttachmentDescription compositeAttachment = colorAttachment;
    compositeAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    compositeAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    compositeAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    compositeAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    compositeAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    compositeAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo compositeInfo { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    compositeInfo.attachmentCount = 1;
    compositeInfo.pAttachments = &compositeAttachment;
    compositeInfo.subpassCount = 1;
    compositeInfo.pSubpasses = &subpass;
    compositeInfo.dependencyCount = 1;
    compositeInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_Device, &compositeInfo, nullptr, &m_CompositeRenderPass) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite render pass");

    // UI pass: load the composited image and present it.
    VkAttachmentDescription uiAttachment = colorAttachment;
    uiAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    uiAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    uiAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    uiAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    uiAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    uiAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkSubpassDependency uiDependency {};
    uiDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    uiDependency.dstSubpass = 0;
    uiDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    uiDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    uiDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    uiDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo uiInfo { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    uiInfo.attachmentCount = 1;
    uiInfo.pAttachments = &uiAttachment;
    uiInfo.subpassCount = 1;
    uiInfo.pSubpasses = &subpass;
    uiInfo.dependencyCount = 1;
    uiInfo.pDependencies = &uiDependency;

    if (vkCreateRenderPass(m_Device, &uiInfo, nullptr, &m_ImGuiRenderPass) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the UI render pass");
}

std::filesystem::path VulkanContext::GetShaderPath(const std::string& name) const
{
    return FileSystem::ResolveAssetPath("Shaders/" + name);
}

VkShaderModule VulkanContext::CreateShaderModule(const std::vector<uint8_t>& code)
{
    VkShaderModuleCreateInfo createInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &module) != VK_SUCCESS)
        SLATE_ERROR("Failed to create shader module ({} bytes)", code.size());
    return module;
}

void VulkanContext::CreateCompositePipeline()
{
    const std::filesystem::path vertexPath = GetShaderPath("fullscreen.vert.spv");
    const std::filesystem::path fragmentPath = GetShaderPath("tonemap.frag.spv");

    const std::vector<uint8_t> vertexCode = FileSystem::ReadBinaryFile(vertexPath);
    const std::vector<uint8_t> fragmentCode = FileSystem::ReadBinaryFile(fragmentPath);
    if (vertexCode.empty() || fragmentCode.empty())
    {
        SLATE_CRITICAL("Composite shaders missing at '{}' / '{}'", vertexPath.string(), fragmentPath.string());
        return;
    }

    m_CompositeVertexShader = CreateShaderModule(vertexCode);
    m_CompositeFragmentShader = CreateShaderModule(fragmentCode);

    VkPipelineShaderStageCreateInfo stages[2] {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_CompositeVertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_CompositeFragmentShader;
    stages[1].pName = "main";

    VkDescriptorSetLayoutBinding samplerBinding {};
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;
    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_CompositeDescriptorLayout) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite descriptor set layout");

    VkDescriptorPoolSize poolSize {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_CompositeDescriptorPool) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite descriptor pool");

    VkDescriptorSetAllocateInfo allocInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_CompositeDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_CompositeDescriptorLayout;
    if (vkAllocateDescriptorSets(m_Device, &allocInfo, &m_CompositeDescriptorSet) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to allocate the composite descriptor set");

    VkPipelineLayoutCreateInfo pipelineLayoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_CompositeDescriptorLayout;
    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_CompositePipelineLayout) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite pipeline layout");

    VkPipelineVertexInputStateCreateInfo vertexInput { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_CompositePipelineLayout;
    pipelineInfo.renderPass = m_CompositeRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_CompositePipeline) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite pipeline");

    VkSamplerCreateInfo samplerInfo { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_CompositeSampler) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the composite sampler");
}

void VulkanContext::CreateCompositeFramebuffers()
{
    m_CompositeFramebuffers.resize(m_SwapchainImageViews.size());
    m_ImGuiFramebuffers.resize(m_SwapchainImageViews.size());

    for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
    {
        VkFramebufferCreateInfo createInfo { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &m_SwapchainImageViews[i];
        createInfo.width = m_SwapchainExtent.width;
        createInfo.height = m_SwapchainExtent.height;
        createInfo.layers = 1;

        createInfo.renderPass = m_CompositeRenderPass;
        if (vkCreateFramebuffer(m_Device, &createInfo, nullptr, &m_CompositeFramebuffers[i]) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create a composite framebuffer");

        createInfo.renderPass = m_ImGuiRenderPass;
        if (vkCreateFramebuffer(m_Device, &createInfo, nullptr, &m_ImGuiFramebuffers[i]) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create a UI framebuffer");
    }
}

void VulkanContext::CreateCommandResources()
{
    VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to create the command pool");
        return;
    }

    VkCommandBufferAllocateInfo allocInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = FramesInFlight;

    std::array<VkCommandBuffer, FramesInFlight> commandBuffers {};
    if (vkAllocateCommandBuffers(m_Device, &allocInfo, commandBuffers.data()) != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to allocate command buffers");
        return;
    }

    VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < FramesInFlight; ++i)
    {
        m_Frames[i].CommandBuffer = commandBuffers[i];
        if (vkCreateFence(m_Device, &fenceInfo, nullptr, &m_Frames[i].Fence) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create a frame fence");
    }
}

void VulkanContext::CreateSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto& semaphore : m_AcquireSemaphores)
    {
        if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
            SLATE_CRITICAL("Failed to create an image-acquired semaphore");
    }

    VkSemaphoreTypeCreateInfo timelineInfo { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    VkSemaphoreCreateInfo timelineSemaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    timelineSemaphoreInfo.pNext = &timelineInfo;
    if (vkCreateSemaphore(m_Device, &timelineSemaphoreInfo, nullptr, &m_FrameDoneSemaphore) != VK_SUCCESS)
        SLATE_CRITICAL("Failed to create the frame-done timeline semaphore");
}

void VulkanContext::CreateNvrhiDevice()
{
    nvrhi::vulkan::DeviceDesc desc;
    desc.errorCB = &m_MessageCallback;
    desc.instance = m_Instance;
    desc.physicalDevice = m_PhysicalDevice;
    desc.device = m_Device;
    desc.graphicsQueue = m_GraphicsQueue;
    desc.graphicsQueueIndex = static_cast<int>(m_GraphicsQueueFamily);
    desc.deviceExtensions = m_DeviceExtensions.data();
    desc.numDeviceExtensions = m_DeviceExtensions.size();
    desc.bufferDeviceAddressSupported = true;
    desc.descriptorBindingUniformBufferUpdateAfterBind = true;

    m_NvrhiVulkanDevice = nvrhi::vulkan::createDevice(desc);
    m_NvrhiDevice = m_NvrhiVulkanDevice;
    if (!m_NvrhiDevice)
        SLATE_CRITICAL("Failed to create the NVRHI Vulkan device");
}

void VulkanContext::InitImGui()
{
    if (m_ImGuiInitialized)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    // A readable monospace font and a restrained dark theme.
    const std::filesystem::path fontPath =
        FileSystem::GetAssetRoot() / "Fonts" / "JetBrainsMono-Regular.ttf";
    if (std::filesystem::exists(fontPath))
    {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        fontConfig.OversampleV = 1;
        io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 17.0f, &fontConfig);
    }

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 0.97f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.32f, 0.55f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.40f, 0.66f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.46f, 0.74f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.20f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.30f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.40f, 0.54f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.19f, 0.24f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.60f, 1.00f);

    ImGui_ImplGlfw_InitForVulkan(m_Window.GetNativeWindow(), true);

    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_ImGuiDescriptorPool) != VK_SUCCESS)
    {
        SLATE_CRITICAL("Failed to create the ImGui descriptor pool");
        return;
    }

    ImGui_ImplVulkan_InitInfo initInfo {};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = m_Instance;
    initInfo.PhysicalDevice = m_PhysicalDevice;
    initInfo.Device = m_Device;
    initInfo.QueueFamily = m_GraphicsQueueFamily;
    initInfo.Queue = m_GraphicsQueue;
    initInfo.DescriptorPool = m_ImGuiDescriptorPool;
    initInfo.MinImageCount = m_MinImageCount;
    initInfo.ImageCount = m_ImageCount;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.PipelineInfoMain.RenderPass = m_ImGuiRenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = false;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        SLATE_CRITICAL("Failed to initialize the ImGui Vulkan backend");
        return;
    }

    m_ImGuiInitialized = true;
}

void VulkanContext::ShutdownImGui()
{
    if (!m_ImGuiInitialized)
        return;

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_ImGuiDescriptorPool)
    {
        vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);
        m_ImGuiDescriptorPool = VK_NULL_HANDLE;
    }

    m_ImGuiInitialized = false;
}

void VulkanContext::BeginImGuiFrame()
{
    if (!m_ImGuiInitialized)
        return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanContext::RenderImGui()
{
    if (!m_ImGuiInitialized || !m_FrameActive)
        return;

    ImGui::Render();

    VkCommandBuffer commandBuffer = m_Frames[m_CurrentFrame].CommandBuffer;

    // The ImGui Vulkan backend records draws into an already-begun render
    // pass; it does not begin one itself.
    VkRenderPassBeginInfo renderPassInfo { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    renderPassInfo.renderPass = m_ImGuiRenderPass;
    renderPassInfo.framebuffer = m_ImGuiFramebuffers[m_ImageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_SwapchainExtent;
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanContext::SetCompositeSource(VkImageView view)
{
    m_CompositeSourceView = view;
    SLATE_INFO("Composite source view set: 0x{:016X}", reinterpret_cast<uintptr_t>(view));
    if (view == VK_NULL_HANDLE || m_CompositeDescriptorSet == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo imageInfo {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = m_CompositeSampler;

    VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_CompositeDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

void VulkanContext::WaitIdle()
{
    if (m_Device)
        vkDeviceWaitIdle(m_Device);
}

void VulkanContext::BeginFrame()
{
    if (m_Device == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE)
        return;

    if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
        return;

    FrameResources& frame = m_Frames[m_CurrentFrame];

    vkWaitForFences(m_Device, 1, &frame.Fence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkSemaphore acquireSemaphore = m_AcquireSemaphores[m_CurrentFrame];
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device, m_Swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
    {
        m_SwapchainOutOfDate = true;
        return;
    }
    if (acquireResult != VK_SUCCESS)
    {
        SLATE_ERROR("vkAcquireNextImageKHR failed with VkResult {}", static_cast<int>(acquireResult));
        return;
    }

    m_ImageIndex = imageIndex;

    // Gate all subsequent queue work on the acquired image without a CPU stall.
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo gateSubmit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    gateSubmit.waitSemaphoreCount = 1;
    gateSubmit.pWaitSemaphores = &acquireSemaphore;
    gateSubmit.pWaitDstStageMask = &waitStage;
    gateSubmit.commandBufferCount = 0;
    vkQueueSubmit(m_GraphicsQueue, 1, &gateSubmit, VK_NULL_HANDLE);

    vkResetFences(m_Device, 1, &frame.Fence);
    vkResetCommandBuffer(frame.CommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.CommandBuffer, &beginInfo);

    m_FrameActive = true;
}

void VulkanContext::ExecuteCommandList(nvrhi::ICommandList* commandList)
{
    if (!m_NvrhiDevice || !commandList)
        return;

    ++m_FrameDoneValue;
    m_NvrhiVulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_FrameDoneSemaphore, m_FrameDoneValue);
    m_NvrhiDevice->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);
}

void VulkanContext::Composite()
{
    if (!m_FrameActive || m_CompositeSourceView == VK_NULL_HANDLE)
        return;

    VkCommandBuffer commandBuffer = m_Frames[m_CurrentFrame].CommandBuffer;

    VkRenderPassBeginInfo renderPassInfo { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    renderPassInfo.renderPass = m_CompositeRenderPass;
    renderPassInfo.framebuffer = m_CompositeFramebuffers[m_ImageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = m_SwapchainExtent;
    VkClearValue clearValue {};
    clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CompositePipelineLayout,
        0, 1, &m_CompositeDescriptorSet, 0, nullptr);

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_SwapchainExtent.width);
    viewport.height = static_cast<float>(m_SwapchainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor {};
    scissor.offset = { 0, 0 };
    scissor.extent = m_SwapchainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanContext::EndFrame()
{
    if (!m_FrameActive)
        return;

    VkCommandBuffer commandBuffer = m_Frames[m_CurrentFrame].CommandBuffer;

    if (!m_ImGuiInitialized)
    {
        // Without a UI pass, transition the image to the present layout.
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_SwapchainImages[m_ImageIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(commandBuffer);

    VkTimelineSemaphoreSubmitInfo timelineInfo { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    uint64_t waitValue = m_FrameDoneValue;
    uint64_t signalValue = 0; // ignored for the binary render-finished semaphore
    timelineInfo.waitSemaphoreValueCount = 1;
    timelineInfo.pWaitSemaphoreValues = &waitValue;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkSemaphore renderFinished = m_RenderFinishedSemaphores[m_ImageIndex];
    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &timelineInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_FrameDoneSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_Frames[m_CurrentFrame].Fence) != VK_SUCCESS)
        SLATE_ERROR("Failed to submit the composite command buffer");

    VkPresentInfoKHR presentInfo { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &m_ImageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        m_SwapchainOutOfDate = true;

    m_FrameActive = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % FramesInFlight;
}

void VulkanContext::DestroySwapchain()
{
    for (auto framebuffer : m_CompositeFramebuffers)
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    m_CompositeFramebuffers.clear();

    for (auto framebuffer : m_ImGuiFramebuffers)
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    m_ImGuiFramebuffers.clear();

    for (auto semaphore : m_RenderFinishedSemaphores)
        vkDestroySemaphore(m_Device, semaphore, nullptr);
    m_RenderFinishedSemaphores.clear();

    for (auto view : m_SwapchainImageViews)
        vkDestroyImageView(m_Device, view, nullptr);
    m_SwapchainImageViews.clear();

    if (m_Swapchain)
    {
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    m_SwapchainImages.clear();
}

void VulkanContext::RecreateSwapchain()
{
    if (m_Device == VK_NULL_HANDLE)
        return;

    if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
        return;

    WaitIdle();

    DestroySwapchain();
    CreateSwapchain();
    CreateSwapchainImageViews();
    CreateCompositeFramebuffers();

    if (m_ImGuiInitialized)
        ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);

    m_SwapchainOutOfDate = false;
}

bool VulkanContext::CaptureScreenshot(const std::string& path)
{
    if (m_Device == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE || m_SwapchainImages.empty())
        return false;

    WaitIdle();

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(m_SwapchainExtent.width)
        * m_SwapchainExtent.height * 4;

    VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements(m_Device, buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocateInfo.allocationSize = requirements.size;

    VkPhysicalDeviceMemoryProperties memoryProperties {};
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

    bool foundMemoryType = false;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        if ((requirements.memoryTypeBits & (1u << i))
            && (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            && (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            allocateInfo.memoryTypeIndex = i;
            foundMemoryType = true;
            break;
        }
    }

    if (!foundMemoryType)
    {
        vkDestroyBuffer(m_Device, buffer, nullptr);
        return false;
    }

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(m_Device, &allocateInfo, nullptr, &memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_Device, buffer, nullptr);
        return false;
    }
    vkBindBufferMemory(m_Device, buffer, memory, 0);

    VkCommandBufferAllocateInfo commandAlloc { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    commandAlloc.commandPool = m_CommandPool;
    commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAlloc.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_Device, &commandAlloc, &commandBuffer);

    // Acquire a swap chain image so the layout transition is legal. The
    // acquired image contains one of the most recently rendered frames.
    VkSemaphoreCreateInfo semaphoreInfo { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
    vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &acquireSemaphore);

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        m_Device, m_Swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        vkDestroySemaphore(m_Device, acquireSemaphore, nullptr);
        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
        vkFreeMemory(m_Device, memory, nullptr);
        vkDestroyBuffer(m_Device, buffer, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    const VkImage image = m_SwapchainImages[imageIndex];

    VkImageMemoryBarrier toTransfer { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { m_SwapchainExtent.width, m_SwapchainExtent.height, 1 };
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);

    VkImageMemoryBarrier toPresent { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = image;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.layerCount = 1;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkEndCommandBuffer(commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &acquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
        vkFreeMemory(m_Device, memory, nullptr);
        vkDestroyBuffer(m_Device, buffer, nullptr);
        return false;
    }

    vkQueueWaitIdle(m_GraphicsQueue);

    // Return the image to the swap chain so the next frame can acquire it.
    VkPresentInfoKHR presentInfo { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(m_GraphicsQueue, &presentInfo);

    vkDestroySemaphore(m_Device, acquireSemaphore, nullptr);

    void* mapped = nullptr;
    vkMapMemory(m_Device, memory, 0, imageSize, 0, &mapped);

    std::vector<uint8_t> rgba(static_cast<size_t>(imageSize));
    const auto* bgra = static_cast<const uint8_t*>(mapped);
    const size_t pixelCount = static_cast<size_t>(m_SwapchainExtent.width) * m_SwapchainExtent.height;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
    }

    vkUnmapMemory(m_Device, memory);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
    vkFreeMemory(m_Device, memory, nullptr);
    vkDestroyBuffer(m_Device, buffer, nullptr);

    return ImageWriter::WritePng(path, rgba.data(), m_SwapchainExtent.width, m_SwapchainExtent.height);
}

} // namespace Slate
