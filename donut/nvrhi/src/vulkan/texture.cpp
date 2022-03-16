#include <algorithm>

#include <nvrhi/vulkan.h>


namespace nvrhi
{
namespace vulkan
{

static vk::ImageType pickImageType(const TextureDesc& d)
{
    switch (d.dimension)
    {
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        return vk::ImageType::e1D;

    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        return vk::ImageType::e2D;

    case TextureDimension::Texture3D:
        return vk::ImageType::e3D;

    default:
        assert(!"unknown texture dimension");
        return vk::ImageType::e2D;
    }
}


static vk::ImageViewType pickEntireImageViewType(const TextureDesc& d)
{
    switch (d.dimension)
    {
    case TextureDimension::Texture1D:
        return vk::ImageViewType::e1D;

    case TextureDimension::Texture1DArray:
        return vk::ImageViewType::e1DArray;

    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
        return vk::ImageViewType::e2D;

    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
        return vk::ImageViewType::e2DArray;

    case TextureDimension::TextureCube:
        return vk::ImageViewType::eCube;

    case TextureDimension::TextureCubeArray:
        return vk::ImageViewType::eCubeArray;

    case TextureDimension::Texture3D:
        return vk::ImageViewType::e3D;

    default:
        assert(!"unknown texture dimension");
        return vk::ImageViewType::e2D;
    }
}

// xxxnsubtil: this is dumb, fix!
static vk::ImageViewType pickImageViewType(const TextureDesc& d, const TextureSubresource& subresource)
{
    switch (d.dimension)
    {
    case TextureDimension::Texture1D:
        return vk::ImageViewType::e1D;

    case TextureDimension::Texture1DArray:
        if (subresource.numArraySlices > 1)
            return vk::ImageViewType::e1DArray;
        else
            return vk::ImageViewType::e1D;

    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
        return vk::ImageViewType::e2D;

    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
        if(subresource.numArraySlices > 1)
            return vk::ImageViewType::e2DArray;
        else
            return vk::ImageViewType::e2D;

    case TextureDimension::TextureCube:
        if (subresource.numArraySlices == 6)
            return vk::ImageViewType::eCube;
        else if (subresource.numArraySlices == 1)
            return vk::ImageViewType::e2D;
        else
        {
            assert(!"peculiar number of cube face subresources requested");
            return vk::ImageViewType::e2D;
        }

    case TextureDimension::TextureCubeArray:
        if (subresource.numArraySlices == 6)
            return vk::ImageViewType::eCube;
        else if (subresource.numArraySlices % 6 == 0)
            return vk::ImageViewType::eCubeArray;
        else if (subresource.numArraySlices == 1)
            return vk::ImageViewType::e2D;
        else
        {
            assert(!"peculiar number of cubearray face subresources requested");
            return vk::ImageViewType::e2D;
        }

    case TextureDimension::Texture3D: // XXX UNTESTED SO FAR!
        if (subresource.numArraySlices == 1)
            return vk::ImageViewType::e2D;
        else
            return vk::ImageViewType::e3D;

    default:
        assert(!"unknown texture dimension");
        return vk::ImageViewType::e2D;
    }
}

static vk::Extent3D pickImageExtent(const TextureDesc& d)
{
    return vk::Extent3D(d.width, d.height, d.depth);
}

static uint32_t pickImageLayers(const TextureDesc& d)
{
    return d.arraySize;
}

static vk::ImageUsageFlags pickImageUsage(const TextureDesc& d)
{
    // xxxnsubtil: may want to consider exposing this through nvrhi instead
    vk::ImageUsageFlags ret = vk::ImageUsageFlagBits::eTransferSrc |
                              vk::ImageUsageFlagBits::eTransferDst |
                              vk::ImageUsageFlagBits::eSampled;

    if (d.isRenderTarget)
    {
        if (nvrhi::FormatIsDepthStencil(d.format))
        {
            ret |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
        } else {
            ret |= vk::ImageUsageFlagBits::eColorAttachment;
        }
    }

    if (d.isUAV)
        ret |= vk::ImageUsageFlagBits::eStorage;

    return ret;
}

static vk::SampleCountFlagBits pickImageSampleCount(const TextureDesc& d)
{
    switch(d.sampleCount)
    {
        case 1:
            return vk::SampleCountFlagBits::e1;

        case 2:
            return vk::SampleCountFlagBits::e2;

        case 4:
            return vk::SampleCountFlagBits::e4;

        case 8:
            return vk::SampleCountFlagBits::e8;

        case 16:
            return vk::SampleCountFlagBits::e16;

        case 32:
            return vk::SampleCountFlagBits::e32;

        case 64:
            return vk::SampleCountFlagBits::e64;

        default:
            assert(!"unsupported sample count");
            return vk::SampleCountFlagBits::e1;
    }
}

// infer aspect flags for a given image format
vk::ImageAspectFlags guessImageAspectFlags(vk::Format format)
{
    switch(format)
    {
        case vk::Format::eD16Unorm:
        case vk::Format::eX8D24UnormPack32:
        case vk::Format::eD32Sfloat:
            return vk::ImageAspectFlagBits::eDepth;

        case vk::Format::eS8Uint:
            return vk::ImageAspectFlagBits::eStencil;

        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

        default:
            return vk::ImageAspectFlagBits::eColor;
    }
}

// a subresource usually shouldn't have both stencil and depth aspect flag bits set; this enforces that depending on viewType param
vk::ImageAspectFlags guessSubresourceImageAspectFlags(vk::Format format, Texture::TextureSubresourceViewType viewType)
{
    vk::ImageAspectFlags flags = guessImageAspectFlags(format);
    if ((flags & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
        == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
    {
        if (viewType == Texture::TextureSubresourceViewType::DepthOnly)
        {
            flags = flags & (~vk::ImageAspectFlagBits::eStencil);
        }
        else if(viewType == Texture::TextureSubresourceViewType::StencilOnly)
        {
            flags = flags & (~vk::ImageAspectFlagBits::eDepth);
        }
    }
    return flags;
}

vk::ImageCreateFlagBits pickImageFlags(const TextureDesc& d)
{
    switch (d.dimension)
    {
    default:
        assert(!"unknown texture dimension");
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
      return vk::ImageCreateFlagBits::eCubeCompatible;
    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture1D:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture3D:
    case TextureDimension::Texture2DMS:
        return (vk::ImageCreateFlagBits)0;
    }
}

// fills out all info fields in Texture based on a TextureDesc
static void fillTextureInfo(Texture *texture, const TextureDesc& desc)
{
    texture->desc = desc;

    vk::ImageType type = pickImageType(desc);
    vk::Extent3D extent = pickImageExtent(desc);
    uint32_t numLayers = pickImageLayers(desc);
    vk::Format format = vk::Format(nvrhi::vulkan::convertFormat(desc.format));
    vk::ImageUsageFlags usage = pickImageUsage(desc);
    vk::SampleCountFlagBits sampleCount = pickImageSampleCount(desc);
    vk::ImageCreateFlagBits flags = pickImageFlags(desc);

    texture->imageInfo = vk::ImageCreateInfo()
                            .setImageType(type)
                            .setExtent(extent)
                            .setMipLevels(desc.mipLevels)
                            .setArrayLayers(numLayers)
                            .setFormat(format)
                            .setInitialLayout(vk::ImageLayout::eUndefined)
                            .setUsage(usage)
                            .setSharingMode(vk::SharingMode::eExclusive)
                            .setSamples(sampleCount)
                            .setFlags(flags);

    texture->subresourceBarrierStates.resize(texture->getNumSubresources());
}

// Note that numRowsOut != height in the case of block-compressed formats, similarly pitch != bytesperpixel*width, so use this function rather than trying to calculate pitch manually:
// calculate pitch/etc according to implicit NVAPI interface host data format
void Texture::hostDataGetPitchAndRows(nvrhi::Format format, size_t width, size_t height, size_t depth, size_t* rowPitchBytesOut, size_t *numRowsInSliceOut, size_t* slicePitchBytesOut)
{
    size_t rowBytes = 0;
    size_t numRows = 0;

    (void)depth;

    // Block size calculation lifted from DDSTextureLoader
    bool blockCompressed = false;
    size_t bytesPerBlock = 0;
    switch (format)
    {
    case nvrhi::Format::BC1_UNORM:
    case nvrhi::Format::BC1_UNORM_SRGB:
    case nvrhi::Format::BC4_UNORM:
    case nvrhi::Format::BC4_SNORM:
        blockCompressed = true;
        bytesPerBlock = 8;
        break;

    case nvrhi::Format::BC2_UNORM:
    case nvrhi::Format::BC2_UNORM_SRGB:
    case nvrhi::Format::BC3_UNORM:
    case nvrhi::Format::BC3_UNORM_SRGB:
    case nvrhi::Format::BC5_UNORM:
    case nvrhi::Format::BC5_SNORM:
    case nvrhi::Format::BC6H_UFLOAT:
    case nvrhi::Format::BC6H_SFLOAT:
    case nvrhi::Format::BC7_UNORM:
    case nvrhi::Format::BC7_UNORM_SRGB:
        blockCompressed = true;
        bytesPerBlock = 16;
        break;

    default:
        break;
    }

    if (blockCompressed)
    {
        size_t numBlocksWide = 0;
        if (width > 0)
        {
            numBlocksWide = std::max<size_t>(1, (width + 3) / 4);
        }
        size_t numBlocksHigh = 0;
        if (height > 0)
        {
            numBlocksHigh = std::max<size_t>(1, (height + 3) / 4);
        }
        rowBytes = numBlocksWide * bytesPerBlock;
        numRows = numBlocksHigh;
    }
    else
    {
        rowBytes = (width * formatElementSizeBits(format) + 7) / 8; // round up to nearest byte
        numRows = height;
    }

    if (rowPitchBytesOut)
    {
        *rowPitchBytesOut = rowBytes;
    }

    if (numRowsInSliceOut)
    {
        *numRowsInSliceOut = numRows;
    }

    if (slicePitchBytesOut)
    {
        size_t slicePitchBytes = rowBytes * numRows;
        *slicePitchBytesOut = slicePitchBytes;
    }
}

void Texture::barrier(TrackedCommandBuffer *cmd,
                      TextureSubresourceView& view,
                      vk::PipelineStageFlags dstStageFlags,
                      vk::AccessFlags dstAccessMask,
                      vk::ImageLayout dstLayout)
{
    // collect info on required barrier transitions
    bool needBarrier = false;
    vk::PipelineStageFlags srcStageFlags = vk::PipelineStageFlags();
    nvrhi::vector<vk::ImageMemoryBarrier> barriers;

    const uint32_t mipStart = view.subresource.baseMipLevel;
    const uint32_t mipEnd = view.subresource.baseMipLevel + view.subresource.numMipLevels;
    const uint32_t layerStart = view.subresource.baseArraySlice;
    const uint32_t layerEnd = view.subresource.baseArraySlice + view.subresource.numArraySlices;

    auto vkformat = nvrhi::vulkan::convertFormat(desc.format);
    vk::ImageAspectFlags aspectMask = guessSubresourceImageAspectFlags(vkformat, Texture::TextureSubresourceViewType::AllAspects);

    for (uint32_t mipLevel = mipStart; mipLevel < mipEnd; mipLevel++)
    {
        for(uint32_t layer = layerStart; layer < layerEnd; layer++)
        {
            const auto index = getSubresourceIndex(mipLevel, layer);
            auto& state = subresourceBarrierStates[index];

            if (state.stageFlags != dstStageFlags ||
                state.accessMask != dstAccessMask ||
                state.layout != dstLayout)
            {
                needBarrier = true;
                srcStageFlags |= state.stageFlags;

                auto subresourceRange = vk::ImageSubresourceRange()
                    .setBaseArrayLayer(layer)
                    .setLayerCount(1)
                    .setBaseMipLevel(mipLevel)
                    .setLevelCount(1)
                    .setAspectMask(aspectMask);

                barriers.push_back(vk::ImageMemoryBarrier()
                                    .setSrcAccessMask(state.accessMask)
                                    .setDstAccessMask(dstAccessMask)
                                    .setOldLayout(state.layout)
                                    .setNewLayout(dstLayout)
                                    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                    .setImage(image)
                                    .setSubresourceRange(subresourceRange));

                state.stageFlags = dstStageFlags;
                state.accessMask = dstAccessMask;
                state.layout = dstLayout;
            }
        }
    }

    if (needBarrier)
    {
        if (srcStageFlags == vk::PipelineStageFlags())
            srcStageFlags = vk::PipelineStageFlagBits::eAllCommands;

        cmd->cmdBuf.pipelineBarrier(srcStageFlags,
                                    dstStageFlags,
                                    vk::DependencyFlags(),
                                    0, nullptr,
                                    0, nullptr,
                                    uint32_t(barriers.size()), barriers.data());

        cmd->markRead(this);
        cmd->markWrite(this);
    }
}

TextureSubresourceView& Texture::getSubresourceView(const TextureSubresourceSet& subresource, TextureSubresourceViewType viewtype)
{
    auto cachekey = std::make_pair(subresource,viewtype);
    auto iter = subresourceViews.find(cachekey);

    if (iter != subresourceViews.end())
    {
        return iter->second;
    }

    auto iter_pair = subresourceViews.emplace(cachekey, *this);
    auto& view = std::get<0>(iter_pair)->second;

    view.subresource = subresource;

    auto vkformat = nvrhi::vulkan::convertFormat(desc.format);
    vk::ImageAspectFlags aspectflags = guessSubresourceImageAspectFlags(vkformat, viewtype);
    view.subresourceRange = vk::ImageSubresourceRange()
                                .setAspectMask(aspectflags)
                                .setBaseMipLevel(subresource.baseMipLevel)
                                .setLevelCount(subresource.numMipLevels)
                                .setBaseArrayLayer(subresource.baseArraySlice)
                                .setLayerCount(subresource.numArraySlices);

    vk::ImageViewType imageViewType;
    if (subresource.isEntireTexture(desc))
    {
        imageViewType = pickEntireImageViewType(desc);
    }
    else
    {
        imageViewType = pickImageViewType(desc, subresource);
    }

    auto viewInfo = vk::ImageViewCreateInfo()
                        .setImage(image)
                        .setViewType(imageViewType)
                        .setFormat(vkformat)
                        .setSubresourceRange(view.subresourceRange);

    if (viewtype == TextureSubresourceViewType::StencilOnly)
    {
        // D3D / HLSL puts stencil values in the second component to keep the illusion of combined depth/stencil.
        // Set a component swizzle so we appear to do the same.
        viewInfo.components.setG(vk::ComponentSwizzle::eR);
    }

    vk::Result res;
    res = context.device.createImageView(&viewInfo, context.allocationCallbacks, &view.view);
    ASSERT_VK_OK(res);
    parent->nameVKObject(view.view, (std::string("ImageView for: ") + std::string(desc.debugName? desc.debugName: "(?)")).c_str());

    return view;
}

TextureHandle Device::createTexture(const TextureDesc& desc)
{
    vk::Result res;

    Texture *texture = new (nvrhi::HeapAlloc) Texture(context, this);
    assert(texture);
    fillTextureInfo(texture, desc);

    res = context.device.createImage(&texture->imageInfo, context.allocationCallbacks, &texture->image);
    ASSERT_VK_OK(res);
    CHECK_VK_FAIL(res);
    nameVKObject(texture->image, desc.debugName);

    res = allocator.allocateTextureMemory(texture);
    ASSERT_VK_OK(res);
    CHECK_VK_FAIL(res);

    // assume the image is going to be copied into, move into transfer destination
    // this initializes the layout as well
    TrackedCommandBuffer *cmd = getAnyCmdBuf();
    assert(cmd);

    for(uint32_t mip = 0; mip < desc.mipLevels; mip++)
    {
        for(uint32_t layer = 0; layer < desc.arraySize; layer++)
        {
            TextureSubresource subresource = TextureSubresource(mip, 1, layer, 1);
 
            texture->barrier(cmd,
                             subresource,
                             vk::PipelineStageFlagBits::eTransfer,
                             vk::AccessFlagBits::eTransferWrite,
                             vk::ImageLayout::eTransferDstOptimal);
        }
    }

    return TextureHandle::Create(texture);
}

void Device::copyTexture(ITexture* _dst, const TextureSlice& dstSlice,
                                          ITexture* _src, const TextureSlice& srcSlice)
{
    Texture* dst = static_cast<Texture*>(_dst);
    Texture* src = static_cast<Texture*>(_src);

    auto resolvedDstSlice = dstSlice.resolve(dst->desc);
    auto resolvedSrcSlice = srcSlice.resolve(src->desc);

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Transfer);
    assert(cmd);

    cmd->referencedResources.push_back(dst);
    cmd->referencedResources.push_back(src);

    TextureSubresource srcSubresource = TextureSubresource(
        resolvedSrcSlice.mipLevel, 1,
        resolvedSrcSlice.arraySlice, 1
    );

    const auto& srcSubresourceView = src->getSubresourceView(srcSubresource, Texture::TextureSubresourceViewType::AllAspects);

    TextureSubresource dstSubresource = TextureSubresource(
        resolvedDstSlice.mipLevel, 1,
        resolvedDstSlice.arraySlice, 1
    );

    const auto& dstSubresourceView = dst->getSubresourceView(dstSubresource, Texture::TextureSubresourceViewType::AllAspects);

    auto imageCopy = vk::ImageCopy()
                        .setSrcSubresource(vk::ImageSubresourceLayers()
                                            .setAspectMask(srcSubresourceView.subresourceRange.aspectMask)
                                            .setMipLevel(srcSubresource.baseMipLevel)
                                            .setBaseArrayLayer(srcSubresource.baseArraySlice)
                                            .setLayerCount(srcSubresource.numArraySlices))
                        .setSrcOffset(vk::Offset3D(resolvedSrcSlice.x, resolvedSrcSlice.y, resolvedSrcSlice.z))
                        .setDstSubresource(vk::ImageSubresourceLayers()
                                            .setAspectMask(dstSubresourceView.subresourceRange.aspectMask)
                                            .setMipLevel(dstSubresource.baseMipLevel)
                                            .setBaseArrayLayer(dstSubresource.baseArraySlice)
                                            .setLayerCount(dstSubresource.numArraySlices))
                        .setDstOffset(vk::Offset3D(resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z))
                        .setExtent(vk::Extent3D(resolvedDstSlice.width, resolvedDstSlice.height, resolvedDstSlice.depth));

    dst->barrier(cmd,
                 dstSubresource,
                 vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferWrite,
                 vk::ImageLayout::eTransferDstOptimal);

    src->barrier(cmd,
                 srcSubresource,
                 vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferRead,
                 vk::ImageLayout::eTransferSrcOptimal);

    cmd->markRead(src);
    cmd->markWrite(dst);

    cmd->cmdBuf.copyImage(src->image, vk::ImageLayout::eTransferSrcOptimal,
                          dst->image, vk::ImageLayout::eTransferDstOptimal,
                          { imageCopy });
}

static void checkTextureDescInvariants(TextureDesc desc)
{
    assert(desc.width > 0);
    assert(desc.height > 0);
    assert(desc.depth > 0);
    assert(desc.arraySize > 0);

    // 1D textures should have a height of 1.
    switch (desc.dimension)
    {
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        assert(desc.height == 1);
    }

    // 1D / 2D textures should have a depth of 1.
    switch (desc.dimension)
    {
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        assert(desc.depth == 1);
    }

    // Non-array textures should have an arraySize of 1.
    // Special case: cubemaps should have an arraySize of 6,
    // and cubemap arrays need to be a multiple of 6.
    switch (desc.dimension)
    {
    case TextureDimension::Texture1D:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture3D:
        assert(desc.arraySize == 1);
        break;
    case TextureDimension::TextureCube:
        assert(desc.arraySize == 6);
        break;
    case TextureDimension::TextureCubeArray:
        assert((desc.arraySize % 6) == 0);
        break;
    }
}

static void computeMipLevelInformation(TextureDesc desc, uint32_t mipLevel, uint32_t* widthOut, uint32_t* heightOut, uint32_t* depthOut)
{
    // TODO(jstpierre): This isn't quite right for NPOT textures.
    uint32_t width = std::max(desc.width >> mipLevel, uint32_t(1));
    uint32_t height = std::max(desc.height >> mipLevel, uint32_t(1));
    uint32_t depth = std::max(desc.depth >> mipLevel, uint32_t(1));

    if (widthOut)
        *widthOut = width;
    if (heightOut)
        *heightOut = height;
    if (depthOut)
        *depthOut = depth;
}

void Device::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
{
    Texture* dest = static_cast<Texture*>(_dest);

    // TODO: this is a copy of the "utility" function writeTexture that creates a staging texture.
    // It's super inefficient, but I'm not familiar enough with the Vulkan backend to quickly make a proper implementation.

    // SEE Mauka's DownloadBuffer::imageCopy2D() FOR PROBABLY-BETTER IMPLEMENTATION
    (void)depthPitch;
    (void)rowPitch;

    TextureDesc desc = dest->GetDesc();
    checkTextureDescInvariants(desc);

    uint32_t mipWidth, mipHeight, mipDepth;
    computeMipLevelInformation(desc, mipLevel, &mipWidth, &mipHeight, &mipDepth);

    size_t dataRowPitchBytes, dataNumRowsInSlice, dataSlicePitchBytes;
    dest->hostDataGetPitchAndRows(desc.format, mipWidth, mipHeight, mipDepth, &dataRowPitchBytes, &dataNumRowsInSlice, &dataSlicePitchBytes);
    assert(dataNumRowsInSlice == 1 || rowPitch == dataRowPitchBytes);
    assert(dataNumRowsInSlice <= desc.height);

    size_t dataNumRows = dataNumRowsInSlice * mipDepth;

    TextureDesc stagingDesc = desc;
    stagingDesc.width = mipWidth;
    stagingDesc.height = mipHeight;
    stagingDesc.depth = mipDepth;
    stagingDesc.arraySize = 1;
    stagingDesc.mipLevels = 1;

    StagingTextureHandle stagingTex = createStagingTexture(stagingDesc, CpuAccessMode::Write);

    size_t mappedRowPitch;
    uint8_t *mappedPtr = (uint8_t *)mapStagingTexture(stagingTex, TextureSlice::setMip(0), CpuAccessMode::Write, &mappedRowPitch);
    uint8_t *dataPtr = (uint8_t *)data;

    for (uint32_t i = 0; i < dataNumRows; i++)
    {
        memcpy(mappedPtr, dataPtr, dataRowPitchBytes);
        mappedPtr += mappedRowPitch;
        dataPtr += dataRowPitchBytes;
    }

    unmapStagingTexture(stagingTex);

    TextureSlice texSlice;
    texSlice.mipLevel = mipLevel;
    texSlice.arraySlice = arraySlice;
    TextureSlice resolvedTexSlice = texSlice.resolve(desc);

    copyTexture(dest, resolvedTexSlice, stagingTex, TextureSlice::setMip(0));
}

void Device::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources)
{
    Texture* dest = static_cast<Texture*>(_dest);
    Texture* src = static_cast<Texture*>(_src);

    TextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
    TextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

    if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
        // let the validation layer handle the messages
        return;

    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    std::vector<vk::ImageResolve> regions;

    for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
    {
        vk::ImageSubresourceLayers dstLayers(vk::ImageAspectFlagBits::eColor, mipLevel + dstSR.baseMipLevel, dstSR.baseArraySlice, dstSR.numArraySlices);
        vk::ImageSubresourceLayers srcLayers(vk::ImageAspectFlagBits::eColor, mipLevel + srcSR.baseMipLevel, srcSR.baseArraySlice, srcSR.numArraySlices);

        regions.push_back(vk::ImageResolve()
            .setSrcSubresource(srcLayers)
            .setDstSubresource(dstLayers)
            .setExtent(vk::Extent3D(
                dest->desc.width >> dstLayers.mipLevel, 
                dest->desc.height >> dstLayers.mipLevel, 
                dest->desc.depth >> dstLayers.mipLevel)));
    }

    auto& srcView = src->getSubresourceView(srcSR, Texture::TextureSubresourceViewType::AllAspects);

    src->barrier(cmd,
        srcView,
        vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eTransferRead,
        vk::ImageLayout::eTransferSrcOptimal);

    auto& dstView = dest->getSubresourceView(dstSR, Texture::TextureSubresourceViewType::AllAspects);

    src->barrier(cmd,
        dstView,
        vk::PipelineStageFlagBits::eTransfer,
        vk::AccessFlagBits::eTransferWrite,
        vk::ImageLayout::eTransferDstOptimal);

    cmd->cmdBuf.resolveImage(src->image, vk::ImageLayout::eTransferSrcOptimal, dest->image, vk::ImageLayout::eTransferDstOptimal, regions);
}

void Device::clearTextureFloat(ITexture* _texture, TextureSubresourceSet subresources, const Color& clearColor)
{
    Texture* texture = static_cast<Texture*>(_texture);
    assert(texture);
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    nvrhi::vector<vk::ImageSubresourceRange> subresourceRanges;

    cmd->unbindFB();

    subresources = subresources.resolve(texture->desc, false);

    for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
    {
        for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
        {
            auto& view = texture->getSubresourceView(TextureSubresourceSet(mipLevel, 1, arraySlice, 1), Texture::TextureSubresourceViewType::AllAspects);

            texture->barrier(cmd,
                view,
                vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal);

            subresourceRanges.push_back(view.subresourceRange);
        }
    }

    if (nvrhi::FormatIsDepthStencil(texture->desc.format))
    {
        auto clearValue = vk::ClearDepthStencilValue(clearColor.r, uint32_t(clearColor.g));
        cmd->cmdBuf.clearDepthStencilImage(texture->image,
                                           vk::ImageLayout::eTransferDstOptimal,
                                           &clearValue,
                                           texture->getNumSubresources(),
                                           subresourceRanges.data());
    } else {
        auto clearValue = vk::ClearColorValue()
                            .setFloat32({ clearColor.r, clearColor.g, clearColor.b, clearColor.a });

        cmd->cmdBuf.clearColorImage(texture->image,
                                    vk::ImageLayout::eTransferDstOptimal,
                                    &clearValue,
                                    texture->getNumSubresources(),
                                    subresourceRanges.data());
    }

    cmd->markWrite(texture);
}

void Device::clearDepthStencilTexture(ITexture* _texture, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
{
    if (!clearDepth && !clearStencil)
    {
        return;
    }

    Texture* texture = static_cast<Texture*>(_texture);
    assert(texture);
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    if (!nvrhi::FormatIsDepthStencil(texture->desc.format))
    {
        assert(!"this resource is not depth/stencil texture");
    }

    nvrhi::vector<vk::ImageSubresourceRange> subresourceRanges;

    cmd->unbindFB();

    subresources = subresources.resolve(texture->desc, false);

    Texture::TextureSubresourceViewType aspects = Texture::TextureSubresourceViewType::AllAspects;
    if (!clearDepth)
    {
        aspects = Texture::TextureSubresourceViewType::StencilOnly;
    }
    else if (!clearStencil)
    {
        aspects = Texture::TextureSubresourceViewType::DepthOnly;
    }

    for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
    {
        for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
        {
            auto& view = texture->getSubresourceView(TextureSubresourceSet(mipLevel, 1, arraySlice, 1), aspects);

            texture->barrier(cmd,
                view,
                vk::PipelineStageFlagBits::eTransfer,
                vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal);

            subresourceRanges.push_back(view.subresourceRange);
        }
    }

    auto clearValue = vk::ClearDepthStencilValue(depth, uint32_t(stencil));
    cmd->cmdBuf.clearDepthStencilImage(texture->image,
        vk::ImageLayout::eTransferDstOptimal,
        &clearValue,
        texture->getNumSubresources(),
        subresourceRanges.data());

    cmd->markWrite(texture);
}

void Device::clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, uint32_t clearColor)
{
    clearTextureFloat(texture, subresources, Color(clearColor / 255.f));
}

void Device::imageBarrier(ITexture* _image,
                                           const TextureSlice& slice,
                                           vk::PipelineStageFlags dstStageFlags,
                                           vk::AccessFlags dstAccessMask,
                                           vk::ImageLayout dstLayout)
{
    Texture* image = static_cast<Texture*>(_image);

    auto resolvedSlice = slice.resolve(image->desc);
    TextureSubresource subresource = TextureSubresource(
        resolvedSlice.mipLevel, 1,
        resolvedSlice.arraySlice, 1
    );

    TrackedCommandBuffer *cmd = getAnyCmdBuf();
    assert(cmd);

    cmd->unbindFB();
    image->barrier(cmd,
                   image->getSubresourceView(subresource, Texture::TextureSubresourceViewType::AllAspects),
                   dstStageFlags, dstAccessMask, dstLayout);
}

unsigned long Texture::Release()
{
    assert(refCount > 0);
    unsigned long result = --refCount;
    if (result == 0) parent->destroyTexture(this);
    return result;
}

Object Texture::getNativeObject(ObjectType objectType)
{
    switch (objectType)
    {
    case ObjectTypes::VK_Image:
        return Object(image);
    default:
        return nullptr;
    }
}

Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV)
{
    switch (objectType)
    {
    case nvrhi::ObjectTypes::VK_ImageView:
    {
        Texture::TextureSubresourceViewType viewtype = Texture::TextureSubresourceViewType::AllAspects;
        if (isReadOnlyDSV == true)
        {
            if (FormatIsStencil(format))
            {
                viewtype = Texture::TextureSubresourceViewType::StencilOnly;
            }
            else
            {
                viewtype = Texture::TextureSubresourceViewType::DepthOnly;
            }
        }
        return Object(getSubresourceView(subresources, viewtype).view);
    }
    default:
        return nullptr;
    }
}

void Device::destroyTexture(ITexture* _texture)
{
    Texture* texture = static_cast<Texture*>(_texture);

    auto *cmd = pollAnyCmdBuf();

    if (cmd && cmd->isResourceMarked(texture))
    {
        // xxxnsubtil: should do finer-grained wait here, though this is better than nothing
        waitForIdle();
    }

    for (auto& viewIter : texture->subresourceViews)
    {
        auto& view = viewIter.second.view;
        context.device.destroyImageView(view, context.allocationCallbacks);
        view = vk::ImageView();
    }

    if (texture->managed)
    {
        assert(texture->image != vk::Image(nullptr));

        if (texture->memory != vk::DeviceMemory(nullptr))
        {
            allocator.freeTextureMemory(texture);
        }

        context.device.destroyImage(texture->image, context.allocationCallbacks);
        texture->image = vk::Image();
    }

    texture->setReadSemaphore(syncObjectPool, nullptr);
    texture->setWriteSemaphore(syncObjectPool, nullptr);

    heapDelete(texture);
}

TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
{
    if (_texture.integer == 0)
        return nullptr;

    if (objectType != ObjectTypes::VK_Image)
        return nullptr;

    vk::Image image(VkImage(_texture.integer));

    Texture *texture = new (nvrhi::HeapAlloc) Texture(context, this);
    fillTextureInfo(texture, desc);

    texture->image = image;
    for(auto& state : texture->subresourceBarrierStates)
    {
        state.layout = vk::ImageLayout::eUndefined;
    }

    texture->managed = false;

    return TextureHandle::Create(texture);
}

static vk::BorderColor pickSamplerBorderColor(const SamplerDesc& d)
{
    if (d.borderColor.r == 0.f && d.borderColor.g == 0.f && d.borderColor.b == 0.f)
    {
        if (d.borderColor.a == 0.f)
        {
            return vk::BorderColor::eFloatTransparentBlack;
        }

        if (d.borderColor.a == 1.f)
        {
            return vk::BorderColor::eFloatOpaqueBlack;
        }
    }

    if (d.borderColor.r == 1.f && d.borderColor.g == 1.f && d.borderColor.b == 1.f)
    {
        if (d.borderColor.a == 1.f)
        {
            return vk::BorderColor::eFloatOpaqueWhite;
        }
    }

    assert(!"unsupported border color");
    return vk::BorderColor::eFloatOpaqueBlack;
}

SamplerHandle Device::createSampler(const SamplerDesc& desc)
{
    Sampler *ret = new (nvrhi::HeapAlloc) Sampler(this);

    bool anisotropy = desc.anisotropy > 1.0f;

    ret->desc = desc;
    ret->samplerInfo = vk::SamplerCreateInfo()
                        .setMagFilter(desc.magFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                        .setMinFilter(desc.minFilter ? vk::Filter::eLinear : vk::Filter::eNearest)
                        .setMipmapMode(desc.mipFilter ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest)
                        .setAddressModeU(convertSamplerAddressMode(desc.wrapMode[0]))
                        .setAddressModeV(convertSamplerAddressMode(desc.wrapMode[1]))
                        .setAddressModeW(convertSamplerAddressMode(desc.wrapMode[2]))
                        .setMipLodBias(desc.mipBias)
                        .setAnisotropyEnable(anisotropy)
                        .setMaxAnisotropy(anisotropy ? desc.anisotropy : 1.f)
                        .setCompareEnable(desc.reductionType == SamplerDesc::REDUCTION_COMPARISON)
                        .setCompareOp(vk::CompareOp::eLess)
                        .setMinLod(0.f)
                        .setMaxLod(std::numeric_limits<float>::max())
                        .setBorderColor(pickSamplerBorderColor(desc));

    vk::SamplerReductionModeCreateInfoEXT samplerReductionCreateInfo;
    if (desc.reductionType == SamplerDesc::REDUCTION_MAXIMUM || desc.reductionType == SamplerDesc::REDUCTION_MINIMUM)
    {
        vk::SamplerReductionModeEXT reductionMode =
            desc.reductionType == SamplerDesc::REDUCTION_MAXIMUM ? vk::SamplerReductionModeEXT::eMax : vk::SamplerReductionModeEXT::eMin;
        samplerReductionCreateInfo.setReductionMode(reductionMode);

        ret->samplerInfo.setPNext(&samplerReductionCreateInfo);
    }

    vk::Result res;
    res = context.device.createSampler(&ret->samplerInfo, context.allocationCallbacks, &ret->sampler);
    CHECK_VK_FAIL(res);
    nameVKObject(ret->sampler);

    return SamplerHandle::Create(ret);
}

unsigned long Sampler::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroySampler(this); 
    return result; 
}

void Device::destroySampler(ISampler* _sampler)
{
    Sampler* sampler = static_cast<Sampler*>(_sampler);
    context.device.destroySampler(sampler->sampler);
    heapDelete(sampler);
}

} // namespace vulkan
} // namespace nvrhi
