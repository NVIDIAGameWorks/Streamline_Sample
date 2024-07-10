/*
* Copyright (c) 2021 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "Suballocator.h"
#include <vulkan/vulkan.hpp>

namespace rtxmu
{
    constexpr uint32_t DefaultBlockAlignment = 65536;

    struct Allocator
    {
        vk::Instance       instance;
        vk::Device         device;
        vk::PhysicalDevice physicalDevice;
    };

    class VkBlock
    {
    public:
        static vk::DispatchLoaderDynamic& getDispatchLoader();

        static uint32_t getMemoryIndex(const vk::PhysicalDevice& physicalDevice,
                                       uint32_t memoryTypeBits,
                                       vk::MemoryPropertyFlags propFlags,
                                       vk::MemoryHeapFlags heapFlags);

        static vk::DeviceMemory getMemory(VkBlock block);

        static vk::DeviceAddress getDeviceAddress(const vk::Device& device,
                                                  VkBlock block,
                                                  uint64_t offset);

        void allocate(Allocator*              allocator,
                      vk::DeviceSize          size,
                      vk::BufferUsageFlags    usageFlags,
                      vk::MemoryPropertyFlags propFlags,
                      vk::MemoryHeapFlags     heapflags);

        void free(Allocator* allocator);

        uint64_t getVMA();

        vk::Buffer getBuffer();

    private:
        static vk::DispatchLoaderDynamic m_dispatchLoader;
        vk::DeviceMemory                 m_memory = nullptr;
        vk::Buffer                       m_buffer = nullptr;
    };

    class VkScratchBlock : public VkBlock
    {
    public:
        static constexpr vk::BufferUsageFlags    usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        static constexpr vk::MemoryPropertyFlags propertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        static constexpr vk::MemoryHeapFlags     heapFlags = vk::MemoryHeapFlagBits::eDeviceLocal;
        static constexpr uint32_t                alignment = DefaultBlockAlignment;

        uint32_t getAlignment() { return alignment; }

        void allocate(Allocator* allocator, vk::DeviceSize size)
        {
            VkBlock::allocate(allocator, size, usageFlags, propertyFlags, heapFlags);
        }
    };

    class VkAccelStructBlock : public VkBlock
    {
    public:
        static constexpr vk::BufferUsageFlags    usageFlags = vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        static constexpr vk::MemoryPropertyFlags propertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        static constexpr vk::MemoryHeapFlags     heapFlags = vk::MemoryHeapFlagBits::eDeviceLocal;
        static constexpr uint32_t                alignment = DefaultBlockAlignment;

        vk::AccelerationStructureKHR             m_asHandle;

        uint32_t getAlignment() { return alignment; }

        void allocate(Allocator* allocator, vk::DeviceSize size)
        {
            VkBlock::allocate(allocator, size, usageFlags, propertyFlags, heapFlags);
        }
    };

    class VkReadBackBlock : public VkBlock
    {
    public:
        static constexpr vk::BufferUsageFlags    usageFlags = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
        static constexpr vk::MemoryPropertyFlags propertyFlags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached;
        static constexpr vk::MemoryHeapFlags     heapFlags = vk::MemoryHeapFlagBits::eDeviceLocal;
        static constexpr uint32_t                alignment = DefaultBlockAlignment;

        uint32_t getAlignment() { return alignment; }

        void allocate(Allocator* allocator, vk::DeviceSize size)
        {
            VkBlock::allocate(allocator, size, usageFlags, propertyFlags, heapFlags);
        }
    };

    class VkCompactionWriteBlock : public VkBlock
    {
    public:
        static constexpr vk::BufferUsageFlags    usageFlags = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer;
        static constexpr vk::MemoryPropertyFlags propertyFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
        static constexpr vk::MemoryHeapFlags     heapFlags = vk::MemoryHeapFlagBits::eDeviceLocal;
        static constexpr uint32_t                alignment = DefaultBlockAlignment;

        uint32_t getAlignment() { return alignment; }

        void allocate(Allocator* allocator, vk::DeviceSize size)
        {
            VkBlock::allocate(allocator, size, usageFlags, propertyFlags, heapFlags);
        }
    };

    class VkQueryBlock : public VkBlock
    {
    public:
        static constexpr uint32_t alignment = 8;
        vk::QueryPool queryPool = nullptr;

        uint32_t getAlignment() { return alignment; }

        void allocate(Allocator* allocator, vk::DeviceSize size)
        {
            auto queryPoolInfo = vk::QueryPoolCreateInfo()
                .setQueryType(vk::QueryType::eAccelerationStructureCompactedSizeKHR)
                .setQueryCount((uint32_t)size);

            queryPool = allocator->device.createQueryPool(queryPoolInfo, nullptr, VkBlock::getDispatchLoader());
        }

        void free(Allocator* allocator)
        {
            if (queryPool)
            {
                allocator->device.destroyQueryPool(queryPool, nullptr, VkBlock::getDispatchLoader());
            }
        }
    };
}
