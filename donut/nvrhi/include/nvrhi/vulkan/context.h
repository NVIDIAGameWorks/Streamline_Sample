#pragma once

namespace nvrhi 
{
namespace vulkan
{
// underlying vulkan context
struct VulkanContext
{
    VulkanContext(vk::Instance instance,
                  vk::PhysicalDevice physicalDevice,
                  vk::Device device,
                  vk::AllocationCallbacks *allocationCallbacks = nullptr)
        : instance(instance)
        , physicalDevice(physicalDevice)
        , device(device)
        , allocationCallbacks(allocationCallbacks)
        , pipelineCache(nullptr)
        , defaultDescriptorPool(nullptr)
    { }

    vk::Instance instance;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;
    vk::AllocationCallbacks *allocationCallbacks;
    vk::PipelineCache pipelineCache;

    vk::DescriptorPool defaultDescriptorPool;

    struct {
        bool KHR_maintenance1 = false;
        bool EXT_debug_report = false;
        bool EXT_debug_marker = false;
    } extensions;

    vk::PhysicalDeviceProperties physicalDeviceProperties;
};

} // namespace vulkan
} // namespace nvrhi
