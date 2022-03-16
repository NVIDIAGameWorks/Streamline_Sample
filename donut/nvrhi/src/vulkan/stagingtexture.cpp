#include <nvrhi/vulkan.h>


namespace nvrhi 
{
namespace vulkan
{

extern vk::ImageAspectFlags guessImageAspectFlags(vk::Format format);

// we follow DX conventions when mapping slices and mip levels:
// for a 3D or array texture, array layers / 3d depth slices for a given mip slice
// are consecutive in memory, with padding in between for alignment
// https://msdn.microsoft.com/en-us/library/windows/desktop/dn705766(v=vs.85).aspx

// compute the size of a mip level slice
// this is the size of a single slice of a 3D texture / array for the given mip level
size_t StagingTexture::computeSliceSize(uint32_t mipLevel)
{
    auto blockSize = formatBlockSize(desc.format);
    auto bitsPerBlock = formatElementSizeBits(desc.format);

    auto wInBlocks = (desc.width >> mipLevel) / blockSize;
    auto hInBlocks = (desc.height >> mipLevel) / blockSize;

    auto blockPitchBytes = ((wInBlocks >> mipLevel) * bitsPerBlock + 7) / 8;
    return blockPitchBytes * hInBlocks;
}

static off_t alignBufferOffset(off_t off)
{
    static constexpr off_t bufferAlignmentBytes = 4;
    return ((off + (bufferAlignmentBytes - 1)) / bufferAlignmentBytes) * bufferAlignmentBytes;
}

const StagingTextureRegion& StagingTexture::getSliceRegion(uint32_t mipLevel, uint32_t arraySlice, uint32_t z)
{
    if (desc.depth != 1)
    {
        // Hard case, since each mip level has half the slices as the previous one.
        assert(arraySlice == 0);
        assert(z < desc.depth);

        uint32_t mipDepth = desc.depth;
        uint32_t index = 0;
        while (mipLevel-- > 0)
        {
            index += mipDepth;
            mipDepth = std::max(mipDepth, uint32_t(1));
        }
        return sliceRegions[index + z];
    }
    else if (desc.arraySize != 1)
    {
        // Easy case, since each mip level has a consistent number of slices.
        assert(z == 0);
        assert(arraySlice < desc.arraySize);
        assert(sliceRegions.size() == desc.mipLevels * desc.arraySize);
        return sliceRegions[mipLevel * desc.arraySize + arraySlice];
    }
    else
    {
        assert(arraySlice == 0);
        assert(z == 0);
        assert(sliceRegions.size() == 1);
        return sliceRegions[0];
    }
}

void StagingTexture::populateSliceRegions()
{
    off_t curOffset = 0;

    sliceRegions.clear();

    for(uint32_t mip = 0; mip < desc.mipLevels; mip++)
    {
        auto sliceSize = computeSliceSize(mip);

        uint32_t depth = std::max(desc.depth >> mip, uint32_t(1));
        uint32_t numSlices = desc.arraySize * depth;

        for (uint32_t slice = 0; slice < numSlices; slice++)
        {
            sliceRegions.push_back({ curOffset, sliceSize });

            // update offset for the next region
            curOffset = alignBufferOffset(off_t(curOffset + sliceSize));
        }
    }
}

StagingTextureHandle Device::createStagingTexture(const TextureDesc& desc, CpuAccessMode cpuAccess)
{
    assert(cpuAccess != CpuAccessMode::None);

    StagingTexture *tex = new (nvrhi::HeapAlloc) StagingTexture(this);
    tex->desc = desc;
    tex->populateSliceRegions();

    BufferDesc bufDesc;
    bufDesc.byteSize = uint32_t(tex->getBufferSize());
    assert(bufDesc.byteSize > 0);
    bufDesc.debugName = desc.debugName;
    bufDesc.cpuAccess = cpuAccess;

    BufferHandle internalBuffer = createBuffer(bufDesc);
    tex->buffer = static_cast<Buffer*>(internalBuffer.Get());

    if (!tex->buffer)
    {
        heapDelete(tex);
        return nullptr;
    }

    return StagingTextureHandle::Create(tex);
}

void *Device::mapStagingTexture(IStagingTexture* _tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
{
    assert(slice.x == 0);
    assert(slice.y == 0);
    assert(cpuAccess != CpuAccessMode::None);

    StagingTexture* tex = static_cast<StagingTexture*>(_tex);

    auto resolvedSlice = slice.resolve(tex->desc);

    auto region = tex->getSliceRegion(resolvedSlice.mipLevel, resolvedSlice.arraySlice, resolvedSlice.z);

    assert((region.offset & 0x3) == 0); // per vulkan spec
    assert(region.size > 0);

    auto blockSize = formatBlockSize(tex->desc.format);
    auto bitsPerBlock = formatElementSizeBits(tex->desc.format);
    assert(((resolvedSlice.width * bitsPerBlock) & 7) == 0 && "Non-byte-sized pitches are probably user-error."); // only compressed images (effectively) have sub-byte element sizes but they also stipulate image dimensions which end up with whole-byte pitches
    assert(outRowPitch);

    auto wInBlocks = resolvedSlice.width / blockSize;

    *outRowPitch = (wInBlocks * bitsPerBlock + 7) / 8;

    return mapBuffer(tex->buffer, cpuAccess, region.offset, region.size);
}

void Device::unmapStagingTexture(IStagingTexture* _tex)
{
    StagingTexture* tex = static_cast<StagingTexture*>(_tex);

    unmapBuffer(tex->buffer);
}

unsigned long StagingTexture::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyStagingTexture(this); 
    return result; 
}

void Device::destroyStagingTexture(IStagingTexture* _tex)
{
    StagingTexture* tex = static_cast<StagingTexture*>(_tex);

    heapDelete(tex);
}

void Device::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
{
    Texture* src = static_cast<Texture*>(_src);
    StagingTexture* dst = static_cast<StagingTexture*>(_dst);

    auto resolvedDstSlice = dstSlice.resolve(dst->desc);
    auto resolvedSrcSlice = srcSlice.resolve(src->desc);

    assert(resolvedDstSlice.depth == 1);

    vk::Extent3D srcMipSize = src->imageInfo.extent;
    srcMipSize.width = std::max(srcMipSize.width >> resolvedDstSlice.mipLevel, 1u);
    srcMipSize.height = std::max(srcMipSize.height >> resolvedDstSlice.mipLevel, 1u);

    auto dstRegion = dst->getSliceRegion(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, resolvedDstSlice.z);
    assert((dstRegion.offset % 0x3) == 0); // per Vulkan spec

    TextureSubresource srcSubresource = TextureSubresource(
        resolvedSrcSlice.mipLevel, 1,
        resolvedSrcSlice.arraySlice, 1
    );

    auto imageCopy = vk::BufferImageCopy()
                        .setBufferOffset(dstRegion.offset)
                        .setBufferRowLength(resolvedDstSlice.width)
                        .setBufferImageHeight(resolvedDstSlice.height)
                        .setImageSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(guessImageAspectFlags(src->imageInfo.format))
                                                .setMipLevel(resolvedSrcSlice.mipLevel)
                                                .setBaseArrayLayer(resolvedSrcSlice.arraySlice)
                                                .setLayerCount(1))
                        .setImageOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
                        .setImageExtent(srcMipSize);

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Transfer);
    assert(cmd);

    dst->buffer->barrier(cmd,
                         vk::PipelineStageFlagBits::eTransfer,
                         vk::AccessFlagBits::eTransferWrite);

    src->barrier(cmd,
                 srcSubresource,
                 vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferRead,
                 vk::ImageLayout::eTransferSrcOptimal);

    cmd->cmdBuf.copyImageToBuffer(src->image, vk::ImageLayout::eTransferSrcOptimal,
                                  dst->buffer->buffer, 1, &imageCopy);
    cmd->markRead(dst->buffer);
    cmd->markRead(src);
}

void Device::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
{
    StagingTexture* src = static_cast<StagingTexture*>(_src);
    Texture* dst = static_cast<Texture*>(_dst);

    auto resolvedDstSlice = dstSlice.resolve(dst->desc);
    auto resolvedSrcSlice = srcSlice.resolve(src->desc);

    vk::Extent3D dstMipSize = dst->imageInfo.extent;
    dstMipSize.width = std::max(dstMipSize.width >> resolvedDstSlice.mipLevel, 1u);
    dstMipSize.height = std::max(dstMipSize.height >> resolvedDstSlice.mipLevel, 1u);

    auto srcRegion = src->getSliceRegion(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, resolvedSrcSlice.z);

    assert((srcRegion.offset & 0x3) == 0); // per vulkan spec
    assert(srcRegion.size > 0);

    TextureSubresource dstSubresource = TextureSubresource(
        resolvedDstSlice.mipLevel, 1,
        resolvedDstSlice.arraySlice, 1
    );

    vk::Offset3D dstOffset(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z);

    auto imageCopy = vk::BufferImageCopy()
                        .setBufferOffset(srcRegion.offset)
                        .setBufferRowLength(resolvedSrcSlice.width)
                        .setBufferImageHeight(resolvedSrcSlice.height)
                        .setImageSubresource(vk::ImageSubresourceLayers()
                                                .setAspectMask(guessImageAspectFlags(dst->imageInfo.format))
                                                .setMipLevel(resolvedDstSlice.mipLevel)
                                                .setBaseArrayLayer(resolvedDstSlice.arraySlice)
                                                .setLayerCount(1))
                        .setImageOffset(dstOffset)
                        .setImageExtent(dstMipSize);

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Transfer);
    assert(cmd);

    src->buffer->barrier(cmd,
                         vk::PipelineStageFlagBits::eTransfer,
                         vk::AccessFlagBits::eTransferRead);
    dst->barrier(cmd,
                 dstSubresource,
                 vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferWrite,
                 vk::ImageLayout::eTransferDstOptimal);

    cmd->markRead(src->buffer);
    cmd->markWrite(dst);
    cmd->referencedResources.push_back(src);
    cmd->referencedResources.push_back(dst);

    cmd->cmdBuf.copyBufferToImage(src->buffer->buffer,
                                  dst->image, vk::ImageLayout::eTransferDstOptimal,
                                  1, &imageCopy);
}

} // namespace vulkan
} // namespace nvrhi
