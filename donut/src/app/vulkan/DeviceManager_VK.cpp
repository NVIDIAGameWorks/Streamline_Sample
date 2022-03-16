#include <stdio.h>
#include <assert.h>

#include <string>
#include <set>

#include <donut/app/DeviceManager.h>

#if WIN32
#include <windows.h>
#endif

#include <nvrhi/vulkan.h>
#include <nvrhi/validation/validation.h>

using namespace donut::app;

struct IgnoredError
{
    uint32_t location;
    int32_t code;
};

#define MAKE_ERR(location, code) ((uint64_t(location) << 32) | (code))
static const std::unordered_set<uint64_t> ignoredCodes = {
    // MAKE_ERR(584, 6),   // vkCmdDrawIndexed(): Cannot use image 0x9 with specific layout VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL that doesn't match the actual current layout VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
    // MAKE_ERR(1123, 61), // Descriptor set 0xd57 encountered the following validation error at vkCmdDrawIndexed() time: Image layout specified at vkUpdateDescriptorSets() time doesn't match actual image layout at time descriptor is used. See previous error callback for specific details.
    // MAKE_ERR(9037, 341838338), // vkCreateSwapChainKHR() called with a non-supported presentMode (i.e. VK_PRESENT_MODE_IMMEDIATE_KHR).
    // MAKE_ERR(2042, 201020800), // The multiViewport feature is not enabled, so pCreateInfos[0].pViewportState->viewportCount must be 1 but is 0.
    // MAKE_ERR(2050, 281020802), // The multiViewport feature is not enabled, so pCreateInfos[0].pViewportState->viewportCount must be 1 but is 0.
};

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData)
{
    if (ignoredCodes.find(MAKE_ERR(location, code)) == ignoredCodes.end())
    {
        char buf[4096];
        snprintf(buf, sizeof(buf), "[Vulkan: location=%zu code=%d, layerPrefix='%s'] %s\n", location, code, layerPrefix, msg);

#if WIN32
        OutputDebugStringA(buf);
#else
        fprintf(stderr, "%s", buf);
#endif

        return VK_FALSE;
    }

    return VK_FALSE;
}

class DeviceManager_VK : public DeviceManager
{
public:
    virtual nvrhi::IDevice* GetDevice() override
    {
        if (validationLayer)
            return validationLayer;

        return renderer;
    }

    virtual void SetVsyncEnabled(bool enabled) override
    {
        // Do nothing - toggling vsync is not (easily) supported by vk
    }

    virtual nvrhi::GraphicsAPI GetGraphicsAPI() const override
    {
        return nvrhi::GraphicsAPI::VULKAN;
    }

protected:
    virtual bool CreateDeviceAndSwapChain() override;
    virtual void DestroyDeviceAndSwapChain() override;

    virtual void ResizeSwapChain() override
    {
        if (vulkanDevice)
        {
            destroySwapChain();
            createSwapChain();
        }
    }

    virtual nvrhi::ITexture* GetCurrentBackBuffer() override
    {
        return swapChainImages[swapChainIndex].rhiHandle;
    }
    virtual nvrhi::ITexture* GetBackBuffer(uint32_t index) override
    {
        if (index < swapChainImages.size())
            return swapChainImages[index].rhiHandle;
        return nullptr;
    }
    virtual uint32_t GetCurrentBackBufferIndex() override
    {
        return swapChainIndex;
    }
    virtual uint32_t GetBackBufferCount() override
    {
        return uint32_t(swapChainImages.size());
    }

    virtual void BeginFrame() override;
    virtual void Present() override;

    virtual const char* GetRendererString() override
    {
        return rendererString.c_str();
    }

private:
    bool createInstance();
    bool createWindowSurface();
    void installDebugCallback();
    bool pickPhysicalDevice();
    bool findQueueFamilies(vk::PhysicalDevice physicalDevice);
    bool createDevice();
    bool createSwapChain();
    void destroySwapChain();

    struct VulkanExtensionSet
    {
        std::set<std::string> instance;
        std::set<std::string> layers;
        std::set<std::string> device;
    };

    // minimal set of required extensions
    VulkanExtensionSet enabledExtensions = {
        // instance
        { },
        // layers
        { },
        // device
        { "VK_KHR_swapchain",
          "VK_KHR_maintenance1",
          "VK_NVX_binary_import",
          "VK_NVX_image_view_handle",
          "VK_KHR_push_descriptor",
          "VK_EXT_buffer_device_address"
        },
    };

    // optional extensions
    VulkanExtensionSet optionalExtensions = {
        // instance
        { "VK_EXT_sampler_filter_minmax" },
        // layers
        { },
        // device
        { "VK_EXT_debug_marker" },
    };

    std::string rendererString;

    vk::Instance vulkanInstance;
    vk::DebugReportCallbackEXT debugReportCallback;

    vk::PhysicalDevice vulkanPhysicalDevice;
    int graphicsQueueFamily = -1;
    int computeQueueFamily = -1;
    int transferQueueFamily = -1;
    int presentQueueFamily = -1;

    vk::Device vulkanDevice;
    vk::Queue graphicsQueue;
    vk::Queue computeQueue;
    vk::Queue transferQueue;
    vk::Queue presentQueue;

    bool EXT_debug_report = false;
    bool EXT_debug_marker = false;

    vk::SurfaceKHR windowSurface;

    vk::SurfaceFormatKHR swapChainFormat;
    vk::SwapchainKHR swapChain;

    struct SwapChainImage
    {
        vk::Image image;
        nvrhi::TextureHandle rhiHandle;
    };

    std::vector<SwapChainImage> swapChainImages;
    uint32_t swapChainIndex = uint32_t(-1);

    nvrhi::RefCountPtr<nvrhi::vulkan::Device> renderer;
    nvrhi::RefCountPtr<nvrhi::validation::DeviceWrapper> validationLayer;
};

static std::vector<const char*> stringSetToVector(const std::set<std::string>& set)
{
    std::vector<const char*> ret;
    for (const auto& s : set)
    {
        ret.push_back(s.c_str());
    }

    return ret;
}

template <typename T>
static std::vector<T> setToVector(const std::set<T>& set)
{
    std::vector<T> ret;
    for (const auto& s : set)
    {
        ret.push_back(s);
    }

    return ret;
}

bool DeviceManager_VK::createInstance()
{
    if (!glfwVulkanSupported())
    {
        return false;
    }

    // add any extensions required by GLFW
    uint32_t glfwExtCount;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    assert(glfwExt);

    for (uint32_t i = 0; i < glfwExtCount; i++)
    {
        enabledExtensions.instance.insert(std::string(glfwExt[i]));
    }

    // figure out which optional extensions are supported
    for (const auto& instanceExt : vk::enumerateInstanceExtensionProperties())
    {
        const std::string name = instanceExt.extensionName;
        if (optionalExtensions.instance.find(name) != optionalExtensions.instance.end())
        {
            enabledExtensions.instance.insert(name);
        }
    }

    for (const auto& layer : vk::enumerateInstanceLayerProperties())
    {
        const std::string name = layer.layerName;
        if (optionalExtensions.layers.find(name) != optionalExtensions.layers.end())
        {
            enabledExtensions.layers.insert(name);
        }
    }

    auto applicationInfo = vk::ApplicationInfo()
        .setApiVersion(VK_MAKE_VERSION(1, 1, 0));

    auto instanceExtVec = stringSetToVector(enabledExtensions.instance);
    auto layerVec = stringSetToVector(enabledExtensions.layers);

    // create the vulkan instance
    vk::InstanceCreateInfo info = vk::InstanceCreateInfo()
        .setEnabledLayerCount(uint32_t(layerVec.size()))
        .setPpEnabledLayerNames(layerVec.data())
        .setEnabledExtensionCount(uint32_t(instanceExtVec.size()))
        .setPpEnabledExtensionNames(instanceExtVec.data());

    vk::Result res = vk::createInstance(&info, nullptr, &vulkanInstance);
    if (res != vk::Result::eSuccess)
    {
        abort();
    }
    return true;
}

void DeviceManager_VK::installDebugCallback()
{
    auto info = vk::DebugReportCallbackCreateInfoEXT()
        .setFlags(vk::DebugReportFlagBitsEXT::eError |
            vk::DebugReportFlagBitsEXT::eWarning |
            //   vk::DebugReportFlagBitsEXT::eInformation |
            vk::DebugReportFlagBitsEXT::ePerformanceWarning)
        .setPfnCallback(vulkanDebugCallback);

    vk::Result res = vulkanInstance.createDebugReportCallbackEXT(&info, nullptr, &debugReportCallback);
    assert(res == vk::Result::eSuccess);
}

bool DeviceManager_VK::pickPhysicalDevice()
{
    vk::Format requestedFormat = nvrhi::vulkan::convertFormat(m_DeviceParams.swapChainFormat);
    vk::Extent2D requestedExtent(m_DeviceParams.backBufferWidth, m_DeviceParams.backBufferHeight);

    auto devices = vulkanInstance.enumeratePhysicalDevices();

    // build a list of GPUs
    std::vector<vk::PhysicalDevice> discreteGPUs;
    std::vector<vk::PhysicalDevice> otherGPUs;
    for (const auto& dev : devices)
    {
        auto prop = dev.getProperties();

        // check that all required device extensions are present
        std::set<std::string> requiredExtensions = enabledExtensions.device;
        auto deviceExtensions = dev.enumerateDeviceExtensionProperties();
        for (const auto& ext : deviceExtensions)
        {
            requiredExtensions.erase(ext.extensionName);
        }

        if (requiredExtensions.size() != 0)
        {
            // device is missing one or more required extensions
            continue;
        }

        auto deviceFeatures = dev.getFeatures();
        if (!deviceFeatures.samplerAnisotropy)
        {
            // device is a toaster oven
            continue;
        }
        if (!deviceFeatures.textureCompressionBC)
        {
            // uh-oh
            continue;
        }

        // check that this device supports our intended swap chain creation parameters
        auto surfaceCaps = dev.getSurfaceCapabilitiesKHR(windowSurface);
        auto surfaceFmts = dev.getSurfaceFormatsKHR(windowSurface);
        auto surfacePModes = dev.getSurfacePresentModesKHR(windowSurface);

        if (surfaceCaps.minImageCount > m_DeviceParams.swapChainBufferCount ||
            surfaceCaps.maxImageCount < m_DeviceParams.swapChainBufferCount ||
            surfaceCaps.minImageExtent.width > requestedExtent.width ||
            surfaceCaps.minImageExtent.height > requestedExtent.height ||
            surfaceCaps.maxImageExtent.width < requestedExtent.width ||
            surfaceCaps.maxImageExtent.height < requestedExtent.height)
        {
            // swap chain parameters don't match device capabilities
            continue;
        }

        bool surfaceFormatPresent = false;
        for (size_t i = 0; i < surfaceFmts.size(); i++)
        {
            if (surfaceFmts[i].format == requestedFormat)
            {
                surfaceFormatPresent = true;
                break;
            }
        }

        if (!surfaceFormatPresent)
        {
            // can't create a swap chain using the format requested
            continue;
        }

        bool queueOk = findQueueFamilies(dev);
        if (!findQueueFamilies(dev))
        {
            // device doesn't have all the queue families we need
            continue;
        }

        // check that we can present from the graphics queue
        uint32_t canPresent = dev.getSurfaceSupportKHR(graphicsQueueFamily, windowSurface);
        if (!canPresent)
        {
            continue;
        }

        if (prop.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            discreteGPUs.push_back(dev);
        }
        else {
            otherGPUs.push_back(dev);
        }
    }

    // pick the first discrete GPU if it exists, otherwise the first integrated GPU
    if (discreteGPUs.size() > 0)
    {
        vulkanPhysicalDevice = discreteGPUs[0];
        return true;
    }

    if (otherGPUs.size() > 0)
    {
        vulkanPhysicalDevice = otherGPUs[0];
        return true;
    }

    return false;
}

bool DeviceManager_VK::findQueueFamilies(vk::PhysicalDevice physicalDevice)
{
    auto props = physicalDevice.getQueueFamilyProperties();

    for (int i = 0; i < int(props.size()); i++)
    {
        const auto& queueFamily = props[i];

        if (graphicsQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
            {
                graphicsQueueFamily = i;
            }
        }

        if (computeQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & vk::QueueFlagBits::eCompute))
            {
                computeQueueFamily = i;
            }
        }

        if (transferQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer))
            {
                transferQueueFamily = i;
            }
        }

        if (presentQueueFamily == -1)
        {
            if (queueFamily.queueCount > 0 &&
                glfwGetPhysicalDevicePresentationSupport(vulkanInstance, physicalDevice, i))
            {
                presentQueueFamily = i;
            }
        }
    }

    if (graphicsQueueFamily == -1 || computeQueueFamily == -1 || transferQueueFamily == -1 || presentQueueFamily == -1)
    {
        return false;
    }

    return true;
}

bool DeviceManager_VK::createDevice()
{
    // figure out which optional extensions are supported
    auto deviceExtensions = vulkanPhysicalDevice.enumerateDeviceExtensionProperties();
    for (const auto& ext : deviceExtensions)
    {
        const std::string name = ext.extensionName;
        if (optionalExtensions.device.find(name) != optionalExtensions.device.end())
        {
            enabledExtensions.device.insert(name);
        }
    }

    std::set<int> uniqueQueueFamilies = { graphicsQueueFamily, computeQueueFamily, transferQueueFamily, presentQueueFamily };

    float priority = 1.f;
    std::vector<vk::DeviceQueueCreateInfo> queueDesc;
    for (int queueFamily : uniqueQueueFamilies)
    {
        queueDesc.push_back(vk::DeviceQueueCreateInfo()
            .setQueueFamilyIndex(queueFamily)
            .setQueueCount(1)
            .setPQueuePriorities(&priority));
    }

    auto deviceFeatures = vk::PhysicalDeviceFeatures()
        .setShaderImageGatherExtended(true)
        .setSamplerAnisotropy(true)
        .setShaderFloat64(true)
        .setTessellationShader(true)
        .setTextureCompressionBC(true)
        .setShaderStorageImageWriteWithoutFormat(true);

    auto layerVec = stringSetToVector(enabledExtensions.layers);
    auto extVec = stringSetToVector(enabledExtensions.device);

    auto physicalDeviceBufferDeviceAddressFeaturesExt = vk::PhysicalDeviceBufferDeviceAddressFeaturesEXT()
        .setBufferDeviceAddress(true)
        .setBufferDeviceAddressMultiDevice(true);

    auto deviceDesc = vk::DeviceCreateInfo()
        .setPQueueCreateInfos(queueDesc.data())
        .setQueueCreateInfoCount(uint32_t(queueDesc.size()))
        .setPEnabledFeatures(&deviceFeatures)
        .setEnabledExtensionCount(uint32_t(extVec.size()))
        .setPpEnabledExtensionNames(extVec.data())
        .setEnabledLayerCount(uint32_t(layerVec.size()))
        .setPpEnabledLayerNames(layerVec.data())
        .setPNext(&physicalDeviceBufferDeviceAddressFeaturesExt);

    vk::Result res;
    res = vulkanPhysicalDevice.createDevice(&deviceDesc, nullptr, &vulkanDevice);
    ASSERT_VK_OK(res);

    vulkanDevice.getQueue(graphicsQueueFamily, 0, &graphicsQueue);
    vulkanDevice.getQueue(computeQueueFamily, 0, &computeQueue);
    vulkanDevice.getQueue(transferQueueFamily, 0, &transferQueue);
    vulkanDevice.getQueue(presentQueueFamily, 0, &presentQueue);

    // stash the renderer string
    auto prop = vulkanPhysicalDevice.getProperties();
    rendererString = std::string(prop.deviceName);

    return true;
}

bool DeviceManager_VK::createWindowSurface()
{
    VkResult res;
    res = glfwCreateWindowSurface(vulkanInstance, m_Window, nullptr, (VkSurfaceKHR*)&windowSurface);
    if (res != VK_SUCCESS)
    {
        return false;
    }

    return true;
}

void DeviceManager_VK::destroySwapChain()
{
    if (vulkanDevice)
    {
        vulkanDevice.waitIdle();
    }

    if (swapChain)
    {
        vulkanDevice.destroySwapchainKHR(swapChain);
        swapChain = nullptr;
    }

    swapChainImages.clear();
}

bool DeviceManager_VK::createSwapChain()
{
    destroySwapChain();

    swapChainFormat = {
        vk::Format(nvrhi::vulkan::convertFormat(m_DeviceParams.swapChainFormat)),
        vk::ColorSpaceKHR::eSrgbNonlinear
    };

    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    vk::Extent2D extent = vk::Extent2D(m_DeviceParams.backBufferWidth, m_DeviceParams.backBufferHeight);

    std::set<uint32_t> uniqueQueues = { uint32_t(graphicsQueueFamily), uint32_t(computeQueueFamily), uint32_t(transferQueueFamily), uint32_t(presentQueueFamily) };
    std::vector<uint32_t> queues = setToVector(uniqueQueues);

    auto desc = vk::SwapchainCreateInfoKHR()
        .setSurface(windowSurface)
        .setMinImageCount(m_DeviceParams.swapChainBufferCount)
        .setImageFormat(swapChainFormat.format)
        .setImageColorSpace(swapChainFormat.colorSpace)
        .setImageExtent(extent)
        .setImageArrayLayers(1)
        .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .setImageSharingMode(queues.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent)
        .setQueueFamilyIndexCount(uint32_t(queues.size()))
        .setPQueueFamilyIndices(queues.data())
        .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
        .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
        .setPresentMode(m_DeviceParams.vsyncEnabled ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate)
        .setClipped(true)
        .setOldSwapchain(nullptr);

    vk::Result res;
    res = vulkanDevice.createSwapchainKHR(&desc, nullptr, &swapChain);
    ASSERT_VK_OK(res);

    // retrieve swap chain images
    auto images = vulkanDevice.getSwapchainImagesKHR(swapChain);
    for (auto image : images)
    {
        SwapChainImage sci;
        sci.image = image;

        nvrhi::TextureDesc desc;
        desc.width = m_DeviceParams.backBufferWidth;
        desc.height = m_DeviceParams.backBufferHeight;
        desc.format = m_DeviceParams.swapChainFormat;
        desc.debugName = "swap chain image";

        sci.rhiHandle = renderer->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(sci.image), desc);
        swapChainImages.push_back(sci);
    }

    swapChainIndex = 0;

    return true;
}

bool DeviceManager_VK::CreateDeviceAndSwapChain()
{
    if (m_DeviceParams.enableDebugRuntime)
    {
        enabledExtensions.instance.insert("VK_EXT_debug_report");
        enabledExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
    }

#define CHECK(a) if (!(a)) { return false; }

    CHECK(createInstance());
    vkExtInitInstance(vulkanInstance);

    if (m_DeviceParams.enableDebugRuntime)
    {
        installDebugCallback();
    }

    if (m_DeviceParams.swapChainFormat == nvrhi::Format::SRGBA8_UNORM)
        m_DeviceParams.swapChainFormat = nvrhi::Format::SBGRA8_UNORM;
    else if (m_DeviceParams.swapChainFormat == nvrhi::Format::RGBA8_UNORM)
        m_DeviceParams.swapChainFormat = nvrhi::Format::BGRA8_UNORM;

    CHECK(createWindowSurface());
    CHECK(pickPhysicalDevice());
    CHECK(findQueueFamilies(vulkanPhysicalDevice));
    CHECK(createDevice());

    auto vecInstanceExt = stringSetToVector(enabledExtensions.instance);
    auto vecLayers = stringSetToVector(enabledExtensions.layers);
    auto vecDeviceExt = stringSetToVector(enabledExtensions.device);

    renderer = nvrhi::RefCountPtr<nvrhi::vulkan::Device>::Create(new nvrhi::vulkan::Device(&DefaultMessageCallback::GetInstance(), vulkanInstance,
        vulkanPhysicalDevice,
        vulkanDevice,
        graphicsQueue, graphicsQueueFamily, nullptr,
        transferQueue, transferQueueFamily, nullptr,
        computeQueue, computeQueueFamily, nullptr,
        nullptr,
        vecInstanceExt.data(), vecInstanceExt.size(),
        vecLayers.data(), vecLayers.size(),
        vecDeviceExt.data(), vecDeviceExt.size()));

    if (m_DeviceParams.enableNvrhiValidationLayer)
    {
        validationLayer = nvrhi::RefCountPtr<nvrhi::validation::DeviceWrapper>::Create(new nvrhi::validation::DeviceWrapper(renderer));
    }

    CHECK(createSwapChain());

#undef CHECK

    return true;
}

void DeviceManager_VK::DestroyDeviceAndSwapChain()
{
    destroySwapChain();

    renderer = nullptr;
    validationLayer = nullptr;
    rendererString.clear();

    if (debugReportCallback)
    {
        vulkanInstance.destroyDebugReportCallbackEXT(debugReportCallback);
    }

    if (vulkanDevice)
    {
        vulkanDevice.destroy();
        vulkanDevice = nullptr;
    }

    if (windowSurface)
    {
        assert(vulkanInstance);
        vulkanInstance.destroySurfaceKHR(windowSurface);
        windowSurface = nullptr;
    }

    if (vulkanInstance)
    {
        vulkanInstance.destroy();
        vulkanInstance = nullptr;
    }
}

void DeviceManager_VK::BeginFrame()
{
    vk::Result res;

    nvrhi::vulkan::SemaphoreHandle imageSemaphore = renderer->createSemaphore(vk::PipelineStageFlagBits::eAllCommands);
    vk::Semaphore semaphore = renderer->getVulkanSemaphore(imageSemaphore);

    res = vulkanDevice.acquireNextImageKHR(swapChain,
        std::numeric_limits<uint64_t>::max(),    // timeout
        semaphore,
        vk::Fence(),
        &swapChainIndex);
    assert(res == vk::Result::eSuccess);

    renderer->setTextureReadSemaphore(swapChainImages[swapChainIndex].rhiHandle, imageSemaphore);
    renderer->releaseSemaphore(imageSemaphore);
}

void DeviceManager_VK::Present()
{
    auto rhiImage = GetCurrentBackBuffer();
    renderer->imageBarrier(rhiImage,
        nvrhi::TextureSlice::setMip(0),
        vk::PipelineStageFlagBits::eAllCommands,
        vk::AccessFlagBits::eMemoryRead,
        vk::ImageLayout::ePresentSrcKHR);
    renderer->flushCommandList();

    auto rhiSemaphore = renderer->getTextureReadSemaphore(rhiImage);
    vk::Semaphore waitSemaphore = renderer->getVulkanSemaphore(rhiSemaphore);

    vk::PresentInfoKHR info = vk::PresentInfoKHR()
        .setWaitSemaphoreCount(1)
        .setPWaitSemaphores(&waitSemaphore)
        .setSwapchainCount(1)
        .setPSwapchains(&swapChain)
        .setPImageIndices(&swapChainIndex);

    vk::Result res;
    res = presentQueue.presentKHR(&info);
    assert(res == vk::Result::eSuccess || res == vk::Result::eErrorOutOfDateKHR);

    if (m_DeviceParams.enableDebugRuntime)
    {
        // according to vulkan-tutorial.com, "the validation layer implementation expects
        // the application to explicitly synchronize with the GPU"
        presentQueue.waitIdle();
    }
}

DeviceManager* DeviceManager::CreateVK()
{
    return new DeviceManager_VK();
}