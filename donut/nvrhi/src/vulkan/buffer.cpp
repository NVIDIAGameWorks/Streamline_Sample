#include <nvrhi/vulkan.h>

namespace nvrhi 
{
namespace vulkan
{

void Buffer::barrier(TrackedCommandBuffer *cmd,
                     vk::PipelineStageFlags dstStageFlags,
                     vk::AccessFlags dstAccessMask)
{
    if (barrierState.stageFlags != dstStageFlags ||
        barrierState.accessMask != dstAccessMask)
    {
        if (barrierState.stageFlags == vk::PipelineStageFlagBits())
        {
            barrierState.stageFlags = vk::PipelineStageFlagBits::eBottomOfPipe;
        }

        cmd->cmdBuf.pipelineBarrier(barrierState.stageFlags,
                                    dstStageFlags,
                                    vk::DependencyFlags(),
                                    { },
                                    {
                                        vk::BufferMemoryBarrier()
                                            .setSrcAccessMask(barrierState.accessMask)
                                            .setDstAccessMask(dstAccessMask)
                                            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                            .setBuffer(buffer)
                                            .setOffset(0)
                                            .setSize(desc.byteSize)
                                    },
                                    { });

        barrierState.stageFlags = dstStageFlags;
        barrierState.accessMask = dstAccessMask;

        // XXX[ljm] investigate further, prior to commit 0e2f7786 the following
        // lines were written as-is, that commit moved them to be conditional
        // behind dstStageFlags having Host flag set, but that seems to cause
        // lockups on Linux. Likely what's wrong is that we're not properly
        // tagging barrier calls with host like we should be.
        cmd->markRead(this);
        cmd->markWrite(this);
    }
}

BufferHandle Device::createBuffer(const BufferDesc& desc)
{
    vk::Result res;

    Buffer *buffer = new (nvrhi::HeapAlloc) Buffer(this);
    buffer->desc = desc;

    vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eTransferSrc |
                                      vk::BufferUsageFlagBits::eTransferDst;

    if (desc.isVertexBuffer)
    {
        usageFlags |= vk::BufferUsageFlagBits::eVertexBuffer;
    } else if (desc.isIndexBuffer)
    {
        usageFlags |= vk::BufferUsageFlagBits::eIndexBuffer;
    } else if (desc.isDrawIndirectArgs)
    {
        usageFlags |= vk::BufferUsageFlagBits::eIndirectBuffer;
    } else if (desc.isConstantBuffer)
    {
        usageFlags |= vk::BufferUsageFlagBits::eUniformBuffer;
    } else if (desc.structStride != 0)
    {
        usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
    } else if (desc.canHaveUAVs)
    {
        usageFlags |= vk::BufferUsageFlagBits::eStorageTexelBuffer;
        usageFlags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
    } else
    {
        usageFlags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
    }

    uint32_t size = desc.byteSize;
    if (desc.byteSize < 65536)
    {
        // vulkan allows for <= 64kb buffer updates to be done inline via vkCmdUpdateBuffer,
        // but the data size must always be a multiple of 4
        // enlarge the buffer slightly to allow for this
        size += size % 4;
    }

    buffer->bufferInfo = vk::BufferCreateInfo()
                            .setSize(size)
                            .setUsage(usageFlags)
                            .setSharingMode(vk::SharingMode::eExclusive);

    res = context.device.createBuffer(&buffer->bufferInfo, context.allocationCallbacks, &buffer->buffer);
    CHECK_VK_FAIL(res);
    nameVKObject(buffer->buffer, desc.debugName);

    res = allocator.allocateBufferMemory(buffer);
    CHECK_VK_FAIL(res);
    
    return BufferHandle::Create(buffer);
}

BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)
{
    // TODO: implement this
    (void)objectType;
    (void)buffer;
    (void)desc;

    return nullptr;
}

void Device::copyBuffer(IBuffer* _dest, uint32_t destOffsetBytes,
                                         IBuffer* _src, uint32_t srcOffsetBytes,
                                         size_t dataSizeBytes)
{
    Buffer* dest = static_cast<Buffer*>(_dest);
    Buffer* src = static_cast<Buffer*>(_src);

    assert(destOffsetBytes + dataSizeBytes <= dest->desc.byteSize);
    assert(srcOffsetBytes + dataSizeBytes <= src->desc.byteSize);

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Transfer);
    assert(cmd);

    cmd->referencedResources.push_back(dest);
    cmd->referencedResources.push_back(src);

    src->barrier(cmd,
                 vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferRead);

    dest->barrier(cmd,
                  vk::PipelineStageFlagBits::eTransfer,
                  vk::AccessFlagBits::eTransferWrite);

    auto copyRegion = vk::BufferCopy()
        .setSize(dataSizeBytes)
        .setSrcOffset(srcOffsetBytes)
        .setDstOffset(destOffsetBytes);
    cmd->cmdBuf.copyBuffer(src->buffer, dest->buffer, { copyRegion });

    cmd->markRead(src);
    cmd->markWrite(dest);

    // do an early barrier transition for CPU source buffers, under the assumption that
    // they will be used again on the CPU side
    if (src->desc.cpuAccess != CpuAccessMode::None)
    {
        vk::AccessFlags flags;

        switch(src->desc.cpuAccess)
        {
            case CpuAccessMode::Read:
                flags = vk::AccessFlagBits::eHostRead;
                break;

            case CpuAccessMode::Write:
                flags = vk::AccessFlagBits::eHostWrite;
                break;
                
            default: assert(!"unreachable");
        }

        src->barrier(cmd, vk::PipelineStageFlagBits::eHost, flags);
    }
}

void Device::writeBuffer(IBuffer* _buffer, const void *data, size_t dataSize, size_t destOffsetBytes)
{
    Buffer* buffer = static_cast<Buffer*>(_buffer);

    assert(dataSize <= buffer->desc.byteSize);

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Transfer);
    assert(cmd);

    cmd->referencedResources.push_back(buffer);

    const bool forceSlowSafeImplementation = false; // to help debug writeBuffer issues

    if (dataSize <= 65535 || forceSlowSafeImplementation)
    {
        buffer->barrier(cmd,
            vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eTransferWrite);

        int64_t remaining = dataSize;
        const char* base = (const char*)data;
        while (remaining > 0)
        {
            // vulkan allows <= 64kb transfers via VkCmdUpdateBuffer
            int64_t thisGulpSize = std::min(remaining, int64_t(65536));
            // we bloat the read size here past the incoming buffer since the transfer must be a multiple of 4; the extra garbage should never be used anywhere
            thisGulpSize += thisGulpSize % 4;
            cmd->cmdBuf.updateBuffer(buffer->buffer, destOffsetBytes + dataSize - remaining, thisGulpSize, &base[dataSize - remaining]);
            remaining -= thisGulpSize;
        }

        cmd->markWrite(buffer);
    }
    else
    {
        // TODO(jstpierre): Pool existing buffers rather than creating and destroying them.

        bool useStagingBuffer = false;
        if (buffer->desc.cpuAccess != CpuAccessMode::Write)
            useStagingBuffer = true;
        if (buffer->writeFence != nullptr && !buffer->writeFence->check(context))
            useStagingBuffer = true;

        if (useStagingBuffer)
        {
            // grab a CPU-visible buffer
            BufferDesc tmpDesc;
            tmpDesc.byteSize = uint32_t(dataSize);
            tmpDesc.debugName = "writeBuffer staging buffer";
            tmpDesc.cpuAccess = CpuAccessMode::Write;

            BufferHandle tempBufferRef = createBuffer(tmpDesc);
            RefCountPtr<Buffer> tempBuffer = static_cast<Buffer*>(tempBufferRef.Get());
            assert(tempBuffer);

            void *writePtr = mapBuffer(tempBuffer, nvrhi::CpuAccessMode::Write, 0, dataSize);
            memcpy(writePtr, data, dataSize);
            context.device.unmapMemory(tempBuffer->memory);

            // blit from temp buffer to target buffer
            copyBuffer(buffer, uint32_t(destOffsetBytes), tempBuffer, 0, dataSize);

            tempBuffer->barrier(cmd,
                                vk::PipelineStageFlagBits::eHost,
                                vk::AccessFlagBits::eHostWrite);
        }
        else
        {
            // map the buffer directly
            void *writePtr = mapBuffer(buffer, nvrhi::CpuAccessMode::Write, off_t(destOffsetBytes), dataSize);
            memcpy(writePtr, data, dataSize);
            context.device.unmapMemory(buffer->memory);

            buffer->barrier(cmd,
                            vk::PipelineStageFlagBits::eHost,
                            vk::AccessFlagBits::eHostWrite);
        }
    }
}

unsigned long Buffer::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyBuffer(this); 
    return result; 
}

Object Buffer::getNativeObject(ObjectType objectType)
{
    switch (objectType)
    {
    case ObjectTypes::VK_Buffer:
        return Object(buffer);
    default:
        return nullptr;
    }
}

void Device::destroyBuffer(IBuffer* _buffer)
{
    Buffer* buffer = static_cast<Buffer*>(_buffer);

    if (buffer->managed)
    {
        assert(buffer->buffer != vk::Buffer());

        context.device.destroyBuffer(buffer->buffer, context.allocationCallbacks);
        buffer->buffer = vk::Buffer();

        allocator.freeBufferMemory(buffer);
    }

    for (auto&& iter : buffer->viewCache)
    {
        context.device.destroyBufferView(iter.second, context.allocationCallbacks);
    }
    buffer->viewCache.clear();

    buffer->setReadSemaphore(syncObjectPool, nullptr);
    buffer->setWriteSemaphore(syncObjectPool, nullptr);

    heapDelete(buffer);
}

void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags, off_t offset, size_t size)
{
    Buffer* buffer = static_cast<Buffer*>(_buffer);

    assert(flags != CpuAccessMode::None);

    TrackedCommandBuffer *cmd = getAnyCmdBuf();
    vk::AccessFlags accessFlags;

    switch(flags)
    {
        case CpuAccessMode::Read:
            accessFlags = vk::AccessFlagBits::eHostRead;
            break;

        case CpuAccessMode::Write:
            accessFlags = vk::AccessFlagBits::eHostWrite;
            break;
            
        default: assert(!"unreachable");
    }

    cmd->unbindFB();
    buffer->barrier(cmd, vk::PipelineStageFlagBits::eHost, accessFlags);
    flushCommandList();

    if (buffer->writeFence)
    {
        buffer->writeFence->wait(context);
    }

    if (flags == CpuAccessMode::Write)
    {
        if (buffer->readFence)
        {
            buffer->readFence->wait(context);
        }
    }

    vk::Result res;
    void *ptr;

    res = context.device.mapMemory(buffer->memory, offset, size, vk::MemoryMapFlags(), &ptr);
    assert(res == vk::Result::eSuccess);

    return ptr;
}

void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags)
{
    Buffer* buffer = static_cast<Buffer*>(_buffer);

    return mapBuffer(buffer, flags, 0, buffer->desc.byteSize);
}

void Device::unmapBuffer(IBuffer* _buffer)
{
    Buffer* buffer = static_cast<Buffer*>(_buffer);

    context.device.unmapMemory(buffer->memory);

    TrackedCommandBuffer *cmd = getAnyCmdBuf();
    buffer->barrier(cmd, vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferRead);
}

} // namespace vulkan
} // namespace nvrhi
