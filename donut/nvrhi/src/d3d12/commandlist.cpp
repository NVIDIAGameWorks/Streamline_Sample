/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/d3d12/d3d12.h>
#include <nvrhi/d3d12/internals.h>
#include <pix.h>
#include <nvrhi/validation/validation.h>
#include <sstream>

namespace nvrhi
{
namespace d3d12
{
    CommandList::CommandList(Device* device, const CommandListParameters& params)
        : m_device(device)
        , m_upload(device, params.uploadChunkSize)
        , m_dxrScratch(device, params.scratchChunkSize, params.scratchMaxMemory)
    {
        m_device->m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    }

    void CommandList::message(MessageSeverity severity, const char* messageText, const char* file, int line)
    {
        m_device->message(severity, messageText, file, line);
    }

    Object CommandList::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_GraphicsCommandList:
            if (m_ActiveCommandList)
                return Object(m_ActiveCommandList->commandList.Get());
            else
                return nullptr;

        case ObjectTypes::Nvrhi_D3D12_CommandList:
            return this;

        default:
            return nullptr;
        }
    }

    std::shared_ptr<InternalCommandList> CommandList::createInternalCommandList()
    {
        auto commandList = std::make_shared<InternalCommandList>();

        m_device->m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandList->allocator));
        m_device->m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandList->allocator, nullptr, IID_PPV_ARGS(&commandList->commandList));

#if NVRHI_WITH_DXR
        commandList->commandList->QueryInterface(IID_PPV_ARGS(&commandList->commandList4));
#endif

        return commandList;
    }

    void CommandList::requireTextureState(ITexture* _texture, TextureSubresourceSet subresources, uint32_t state)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        if (texture->IsPermanent())
        {
            CHECK_ERROR((texture->permanentState & state) == state, "Permanent texture has incorrect state");
            return;
        }

        subresources = subresources.resolve(texture->desc, false);

        bool anyUavBarrier = false;

        for (int plane = 0; plane < texture->planeCount; plane++)
        {
            for (ArraySlice arrayIndex = subresources.baseArraySlice; arrayIndex < subresources.baseArraySlice + subresources.numArraySlices; arrayIndex++)
            {
                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    uint32_t subresource = calcSubresource(mipLevel, arrayIndex, plane, texture->desc.mipLevels, texture->desc.arraySize);
                    requireTextureState(texture, subresource, state, anyUavBarrier);
                }
            }
        }
    }

    void CommandList::requireTextureState(ITexture* _texture, uint32_t subresource, uint32_t state, bool& anyUavBarrier)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        if (texture->IsPermanent())
        {
            CHECK_ERROR((texture->permanentState & state) == state, "Permanent texture has incorrect state");
            return;
        }

        TextureState* tracking = getTextureStateTracking(texture, true);

        D3D12_RESOURCE_STATES d3dstate = D3D12_RESOURCE_STATES(state);

        if(tracking->subresourceStates[subresource] == RESOURCE_STATE_UNKNOWN)
        {
            char buf[256];

            sprintf_s(buf, "Unknown prior state of subresource %d of texture '%s' (%s, Width = %d, Height = %d, Depth = %d, ArraySize = %d, MipLevels = %d)",
                subresource, 
                texture->desc.debugName ? texture->desc.debugName : "<UNNAMED>", 
                nvrhi::validation::TextureDimensionToString(texture->desc.dimension), 
                texture->desc.width,
                texture->desc.height,
                texture->desc.depth,
                texture->desc.arraySize,
                texture->desc.mipLevels);

            message(MessageSeverity::Error, buf);
        }

        if (tracking->subresourceStates[subresource] != d3dstate)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture->resource;
            barrier.Transition.StateBefore = tracking->subresourceStates[subresource];
            barrier.Transition.StateAfter = d3dstate;
            barrier.Transition.Subresource = subresource;
            m_barrier.push_back(barrier);
        }
        else if (d3dstate == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && !anyUavBarrier && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced))
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = texture->resource;
            m_barrier.push_back(barrier);
            anyUavBarrier = true;
            tracking->firstUavBarrierPlaced = true;
        }

        tracking->subresourceStates[subresource] = d3dstate;
    }

    void CommandList::requireBufferState(IBuffer* _buffer, uint32_t state)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);
        D3D12_RESOURCE_STATES d3dstate = D3D12_RESOURCE_STATES(state);

        if (buffer->desc.isVolatile)
            return;

        if (buffer->IsPermanent())
        {
            CHECK_ERROR((buffer->permanentState & state) == state, "Permanent buffer has incorrect state");
            return;
        }

        if (buffer->desc.cpuAccess == CpuAccessMode::Write || buffer->desc.cpuAccess == CpuAccessMode::Read)
        {
            // cpu-visible buffers can't change state
            return;
        }

        BufferState* tracking = getBufferStateTracking(buffer, true);

        if (tracking->state == RESOURCE_STATE_UNKNOWN)
        {
            std::ostringstream ss;
            ss << "Unknown prior state of buffer '" 
                << (buffer->desc.debugName ? buffer->desc.debugName : "<UNNAMED>") 
                << "' (ByteSize = " 
                << buffer->desc.byteSize;
            if (buffer->desc.isConstantBuffer) ss << ", ConstantBuffer";
            if (buffer->desc.isIndexBuffer) ss << ", IndexBuffer";
            if (buffer->desc.isVertexBuffer) ss << ", VertexBuffer";
            if (buffer->desc.isDrawIndirectArgs) ss << ", DrawIndirectArgs";
            if (buffer->desc.isVolatile) ss << ", Volatile";
            if (buffer->desc.canHaveUAVs) ss << ", UAV";
            ss << ")";
                        
            message(MessageSeverity::Error, ss.str().c_str());
        }

        CHECK_ERROR(tracking->state != RESOURCE_STATE_UNKNOWN, "Unknown prior buffer state");

        if (tracking->state != d3dstate)
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer->resource;
            barrier.Transition.StateBefore = tracking->state;
            barrier.Transition.StateAfter = d3dstate;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_barrier.push_back(barrier);
        }
        else if (d3dstate == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && (tracking->enableUavBarriers || !tracking->firstUavBarrierPlaced))
        {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = buffer->resource;
            m_barrier.push_back(barrier);
            tracking->firstUavBarrierPlaced = true;
        }

        tracking->state = d3dstate;
    }

    void CommandList::commitBarriers()
    {
        if (m_barrier.empty())
            return;

        m_ActiveCommandList->commandList->ResourceBarrier(uint32_t(m_barrier.size()), &m_barrier[0]);
        m_barrier.clear();
    }

    bool CommandList::commitDescriptorHeaps()
    {
        ID3D12DescriptorHeap* heapSRVetc = m_device->m_dhSRVetc.GetShaderVisibleHeap();
        ID3D12DescriptorHeap* heapSamplers = m_device->m_dhSamplers.GetShaderVisibleHeap();

        if (heapSRVetc != m_CurrentHeapSRVetc || heapSamplers != m_CurrentHeapSamplers)
        {
            ID3D12DescriptorHeap* heaps[2] = { heapSRVetc, heapSamplers };
            m_ActiveCommandList->commandList->SetDescriptorHeaps(2, heaps);

            m_CurrentHeapSRVetc = heapSRVetc;
            m_CurrentHeapSamplers = heapSamplers;

            m_Instance->referencedNativeResources.push_back(heapSRVetc);
            m_Instance->referencedNativeResources.push_back(heapSamplers);

            return true;
        }

        return false;
    }

    void CommandList::clearTextureFloat(ITexture* _t, TextureSubresourceSet subresources, const Color & clearColor)
    {
        Texture* t = static_cast<Texture*>(_t);

        if (!t->desc.useClearValue)
        {
            //OutputDebugStringA("WARNING: No clear value passed to createTexture. D3D will issue a warning here.\n");
        }
        else if (t->desc.clearValue != clearColor)
        {
            //OutputDebugStringA("WARNING: Clear value differs from one passed to createTexture. D3D will issue a warning here.\n");
        }

        const auto& formatMapping = GetFormatMapping(t->desc.format);
        subresources = subresources.resolve(t->desc, false);

        m_Instance->referencedResources.push_back(t);

        if (t->desc.isRenderTarget)
        {
            if (formatMapping.isDepthStencil)
            {
                requireTextureState(t, subresources, D3D12_RESOURCE_STATE_DEPTH_WRITE);
                commitBarriers();

                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE DSV = { t->getNativeView(ObjectTypes::D3D12_DepthStencilViewDescriptor, Format::UNKNOWN, subresources).integer };

                    m_ActiveCommandList->commandList->ClearDepthStencilView(
                        DSV,
                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                        clearColor.r, UINT8(clearColor.g),
                        0, nullptr);
                }
            }
            else
            {
                requireTextureState(t, subresources, D3D12_RESOURCE_STATE_RENDER_TARGET);
                commitBarriers();

                for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE RTV = { t->getNativeView(ObjectTypes::D3D12_RenderTargetViewDescriptor, Format::UNKNOWN, subresources).integer };

                    m_ActiveCommandList->commandList->ClearRenderTargetView(
                        RTV,
                        &clearColor.r,
                        0, nullptr);
                }
            }
        }
        else
        {
            CHECK_ERROR(t->desc.isUAV, "texture was created with isUAV = false");

            requireTextureState(t, subresources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commitBarriers();

            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                DescriptorIndex index = t->clearMipLevelUAVs[mipLevel];

                CHECK_ERROR(index != INVALID_DESCRIPTOR_INDEX, "texture has no clear UAV");

                m_ActiveCommandList->commandList->ClearUnorderedAccessViewFloat(
                    m_device->m_dhSRVetc.GetGpuHandle(index),
                    m_device->m_dhSRVetc.GetCpuHandle(index),
                    t->resource, &clearColor.r, 0, nullptr);
            }
        }
    }

    void CommandList::clearDepthStencilTexture(ITexture* _t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!clearDepth && !clearStencil)
        {
            return;
        }

        Texture* t = static_cast<Texture*>(_t);
        const auto& formatMapping = GetFormatMapping(t->desc.format);

        if ((!t->desc.isRenderTarget) || (!formatMapping.isDepthStencil))
        {
            CHECK_ERROR(0, "This resource is not depth/stencil texture");
        }

        if (!t->desc.useClearValue)
        {
            //OutputDebugStringA("WARNING: No clear value passed to createTexture. D3D will issue a warning here.\n");
        }
        else if (t->desc.clearValue != Color(depth, static_cast<float>(stencil), 0.0f, 0.0f))
        {
            //OutputDebugStringA("WARNING: Clear value differs from one passed to createTexture. D3D will issue a warning here.\n");
        }

        subresources = subresources.resolve(t->desc, false);

        m_Instance->referencedResources.push_back(t);

        requireTextureState(t, subresources, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        commitBarriers();

        D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        if (!clearDepth)
        {
            clearFlags = D3D12_CLEAR_FLAG_STENCIL;
        }
        else if (!clearStencil)
        {
            clearFlags = D3D12_CLEAR_FLAG_DEPTH;
        }

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE DSV = { t->getNativeView(ObjectTypes::D3D12_DepthStencilViewDescriptor, Format::UNKNOWN, subresources).integer };

            m_ActiveCommandList->commandList->ClearDepthStencilView(
                DSV,
                clearFlags,
                depth, stencil,
                0, nullptr);
        }
    }

    void CommandList::clearTextureUInt(ITexture* _t, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        Texture* t = static_cast<Texture*>(_t);

        CHECK_ERROR(t->desc.isUAV, "cannot clear a non-UAV texture as uint");

        subresources = subresources.resolve(t->desc, false);

        requireTextureState(t, subresources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commitBarriers();

        uint32_t clearValues[4] = { clearColor, clearColor, clearColor, clearColor };

        m_Instance->referencedResources.push_back(t);

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            DescriptorIndex index = t->clearMipLevelUAVs[mipLevel];

            CHECK_ERROR(index != INVALID_DESCRIPTOR_INDEX, "texture has no clear UAV");

            m_ActiveCommandList->commandList->ClearUnorderedAccessViewUint(
                m_device->m_dhSRVetc.GetGpuHandle(index),
                m_device->m_dhSRVetc.GetCpuHandle(index),
                t->resource, clearValues, 0, nullptr);
        }
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice,
        ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* dst = static_cast<Texture*>(_dst);
        Texture* src = static_cast<Texture*>(_src);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        assert(resolvedDstSlice.width == resolvedSrcSlice.width);
        assert(resolvedDstSlice.height == resolvedSrcSlice.height);

        UINT dstSubresource = calcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, 0, dst->desc.mipLevels, dst->desc.arraySize);
        UINT srcSubresource = calcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, 0, src->desc.mipLevels, src->desc.arraySize);

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = dst->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = dstSubresource;

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = src->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = srcSubresource;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        bool dummy = false;
        requireTextureState(dst, dstSubresource, D3D12_RESOURCE_STATE_COPY_DEST, dummy);
        requireTextureState(src, srcSubresource, D3D12_RESOURCE_STATE_COPY_SOURCE, dummy);
        commitBarriers();

        m_Instance->referencedResources.push_back(dst);
        m_Instance->referencedResources.push_back(src);

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation,
            resolvedDstSlice.x,
            resolvedDstSlice.y,
            resolvedDstSlice.z,
            &srcLocation,
            &srcBox);
    }

    void CommandList::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
    {
        StagingTexture* src = static_cast<StagingTexture*>(_src);
        Texture* dst = static_cast<Texture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        UINT dstSubresource = calcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, 0, dst->desc.mipLevels, dst->desc.arraySize);

        bool dummy = false;
        requireTextureState(dst, dstSubresource, D3D12_RESOURCE_STATE_COPY_DEST, dummy);
        requireBufferState(src->buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commitBarriers();

        m_Instance->referencedResources.push_back(dst);
        m_Instance->referencedStagingTextures.push_back(src);

        auto srcRegion = src->getSliceRegion(m_device->m_pDevice, resolvedSrcSlice);

        D3D12_TEXTURE_COPY_LOCATION dstLocation;
        dstLocation.pResource = dst->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLocation.SubresourceIndex = dstSubresource;

        D3D12_TEXTURE_COPY_LOCATION srcLocation;
        srcLocation.pResource = src->buffer->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLocation.PlacedFootprint = srcRegion.footprint;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation, resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
            &srcLocation, &srcBox);
    }

    void CommandList::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = static_cast<Texture*>(_src);
        StagingTexture* dst = static_cast<StagingTexture*>(_dst);

        auto resolvedDstSlice = dstSlice.resolve(dst->desc);
        auto resolvedSrcSlice = srcSlice.resolve(src->desc);

        UINT srcSubresource = calcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, 0, src->desc.mipLevels, src->desc.arraySize);

        bool dummy = false;
        requireTextureState(src, srcSubresource, D3D12_RESOURCE_STATE_COPY_SOURCE, dummy);
        requireBufferState(dst->buffer, D3D12_RESOURCE_STATE_COPY_DEST);
        commitBarriers();

        m_Instance->referencedResources.push_back(src);
        m_Instance->referencedStagingTextures.push_back(dst);

        auto dstRegion = dst->getSliceRegion(m_device->m_pDevice, resolvedDstSlice);

        D3D12_TEXTURE_COPY_LOCATION dstLocation;
        dstLocation.pResource = dst->buffer->resource;
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint = dstRegion.footprint;

        D3D12_TEXTURE_COPY_LOCATION srcLocation;
        srcLocation.pResource = src->resource;
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = srcSubresource;

        D3D12_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        m_ActiveCommandList->commandList->CopyTextureRegion(&dstLocation, resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
            &srcLocation, &srcBox);
    }

    void CommandList::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        Texture* dest = static_cast<Texture*>(_dest);

        requireTextureState(dest, TextureSubresourceSet(mipLevel, 1, arraySlice, 1), D3D12_RESOURCE_STATE_COPY_DEST);
        commitBarriers();

        uint32_t subresource = calcSubresource(mipLevel, arraySlice, 0, dest->desc.mipLevels, dest->desc.arraySize);

        D3D12_RESOURCE_DESC resourceDesc = dest->resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
        uint32_t numRows;
        uint64_t rowSizeInBytes;
        uint64_t totalBytes;

        m_device->m_pDevice->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

        void* cpuVA;
        ID3D12Resource* uploadBuffer;
        size_t offsetInUploadBuffer;
        if (!m_upload.SuballocateBuffer(static_cast<size_t>(totalBytes), &uploadBuffer, &offsetInUploadBuffer, &cpuVA, nullptr, 
            m_recordingInstanceID, m_completedInstanceID, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
        {
            CHECK_ERROR(false, "Couldn't suballocate an upload buffer");
            return;
        }
        footprint.Offset = uint64_t(offsetInUploadBuffer);

        assert(numRows <= footprint.Footprint.Height);

        for (uint32_t depthSlice = 0; depthSlice < footprint.Footprint.Depth; depthSlice++)
        {
            for (uint32_t row = 0; row < numRows; row++)
            {
                void* destAddress = (char*)cpuVA + footprint.Footprint.RowPitch * (row + depthSlice * numRows);
                void* srcAddress = (char*)data + rowPitch * row + depthPitch * depthSlice;
                memcpy(destAddress, srcAddress, std::min(rowPitch, rowSizeInBytes));
            }
        }

        D3D12_TEXTURE_COPY_LOCATION destCopyLocation = {};
        destCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destCopyLocation.SubresourceIndex = subresource;
        destCopyLocation.pResource = dest->resource;

        D3D12_TEXTURE_COPY_LOCATION srcCopyLocation = {};
        srcCopyLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcCopyLocation.PlacedFootprint = footprint;
        srcCopyLocation.pResource = uploadBuffer;

        m_Instance->referencedResources.push_back(dest);

        if (uploadBuffer != m_CurrentUploadBuffer)
        {
            m_Instance->referencedNativeResources.push_back(uploadBuffer);
            m_CurrentUploadBuffer = uploadBuffer;
        }

        m_ActiveCommandList->commandList->CopyTextureRegion(&destCopyLocation, 0, 0, 0, &srcCopyLocation, nullptr);
    }

    void CommandList::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources)
    {
        Texture* dest = static_cast<Texture*>(_dest);
        Texture* src = static_cast<Texture*>(_src);

        TextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
            // let the validation layer handle the messages
            return;

        requireTextureState(_dest, dstSubresources, ResourceStates::RESOLVE_DEST);
        requireTextureState(_src, srcSubresources, ResourceStates::RESOLVE_SOURCE);
        commitBarriers();

        const FormatMapping& formatMapping = GetFormatMapping(dest->desc.format);

        for (int plane = 0; plane < dest->planeCount; plane++)
        {
            for (ArraySlice arrayIndex = 0; arrayIndex < dstSR.numArraySlices; arrayIndex++)
            {
                for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
                {
                    uint32_t dstSubresource = calcSubresource(mipLevel + dstSR.baseMipLevel, arrayIndex + dstSR.baseArraySlice, plane, dest->desc.mipLevels, dest->desc.arraySize);
                    uint32_t srcSubresource = calcSubresource(mipLevel + srcSR.baseMipLevel, arrayIndex + srcSR.baseArraySlice, plane, src->desc.mipLevels, src->desc.arraySize);
                    m_ActiveCommandList->commandList->ResolveSubresource(dest->resource, dstSubresource, src->resource, srcSubresource, formatMapping.rtvFormat);
                }
            }
        }
    }

    void CommandList::writeBuffer(IBuffer* _b, const void * data, size_t dataSize, size_t destOffsetBytes)
    {
        Buffer* buffer = static_cast<Buffer*>(_b);

        void* cpuVA;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA;
        ID3D12Resource* uploadBuffer;
        size_t offsetInUploadBuffer;
        if (!m_upload.SuballocateBuffer(dataSize, &uploadBuffer, &offsetInUploadBuffer, &cpuVA, &gpuVA, 
            m_recordingInstanceID, m_completedInstanceID, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT))
        {
            CHECK_ERROR(false, "Couldn't suballocate an upload buffer");
            return;
        }

        if (uploadBuffer != m_CurrentUploadBuffer)
        {
            m_Instance->referencedNativeResources.push_back(uploadBuffer);
            m_CurrentUploadBuffer = uploadBuffer;
        }

        memcpy(cpuVA, data, dataSize);

        if (buffer->desc.isVolatile)
        {
            BufferState* tracking = getBufferStateTracking(buffer, true);
            tracking->volatileData = gpuVA;
        }
        else
        {
            requireBufferState(buffer, D3D12_RESOURCE_STATE_COPY_DEST);
            commitBarriers();

            m_Instance->referencedResources.push_back(buffer);

            m_ActiveCommandList->commandList->CopyBufferRegion(buffer->resource, destOffsetBytes, uploadBuffer, offsetInUploadBuffer, dataSize);
        }
    }

    bool CommandList::allocateUploadBuffer(size_t size, void** pCpuAddress, D3D12_GPU_VIRTUAL_ADDRESS* pGpuAddress)
    {
        return m_upload.SuballocateBuffer(size, nullptr, nullptr, pCpuAddress, pGpuAddress,
            m_recordingInstanceID, m_completedInstanceID, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    D3D12_GPU_VIRTUAL_ADDRESS CommandList::getBufferGpuVA(IBuffer* _buffer)
    {
        if (!_buffer)
            return 0;

        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (buffer->desc.isVolatile)
        {
            BufferState* tracking = getBufferStateTracking(buffer, false);
            
            if (!tracking)
                return 0;

            return tracking->volatileData;
        }

        return buffer->gpuVA;
    }

    nvrhi::IDevice* CommandList::getDevice()
    {
        return m_device.Get();
    }

    void CommandList::clearBufferUInt(IBuffer* _b, uint32_t clearValue)
    {
        Buffer* b = static_cast<Buffer*>(_b);

        CHECK_ERROR(b->desc.canHaveUAVs, "buffer was created with canHaveUAVs = false");
        CHECK_ERROR(b->clearUAV != INVALID_DESCRIPTOR_INDEX, "buffer has no clear UAV");

        requireBufferState(b, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commitBarriers();

        m_Instance->referencedResources.push_back(b);

        const uint32_t values[4] = { clearValue, clearValue, clearValue, clearValue };
        m_ActiveCommandList->commandList->ClearUnorderedAccessViewUint(
            m_device->m_dhSRVetc.GetGpuHandle(b->clearUAV),
            m_device->m_dhSRVetc.GetCpuHandle(b->clearUAV),
            b->resource, values, 0, nullptr);
    }

    void CommandList::copyBuffer(IBuffer* _dest, uint32_t destOffsetBytes, IBuffer* _src, uint32_t srcOffsetBytes, size_t dataSizeBytes)
    {
        Buffer* dest = static_cast<Buffer*>(_dest);
        Buffer* src = static_cast<Buffer*>(_src);

        requireBufferState(dest, D3D12_RESOURCE_STATE_COPY_DEST);
        requireBufferState(src, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commitBarriers();

        if(src->desc.cpuAccess != CpuAccessMode::None)
            m_Instance->referencedStagingBuffers.push_back(src);
        else
            m_Instance->referencedResources.push_back(src);

        if (dest->desc.cpuAccess != CpuAccessMode::None)
            m_Instance->referencedStagingBuffers.push_back(dest);
        else
            m_Instance->referencedResources.push_back(dest);

        m_ActiveCommandList->commandList->CopyBufferRegion(dest->resource, destOffsetBytes, src->resource, srcOffsetBytes, dataSizeBytes);
    }

    void CommandList::beginTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = static_cast<TimerQuery*>(_query);

        m_Instance->referencedTimerQueries.push_back(query);

        m_ActiveCommandList->commandList->EndQuery(m_device->m_timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, query->beginQueryIndex);

        // two timestamps within the same command list are always reliably comparable, so we avoid kicking off here
        // (note: we don't call SetStablePowerState anymore)
    }

    void CommandList::endTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = static_cast<TimerQuery*>(_query);

        m_Instance->referencedTimerQueries.push_back(query);

        m_ActiveCommandList->commandList->EndQuery(m_device->m_timerQueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, query->endQueryIndex);

        m_ActiveCommandList->commandList->ResolveQueryData(m_device->m_timerQueryHeap,
            D3D12_QUERY_TYPE_TIMESTAMP,
            query->beginQueryIndex,
            2,
            static_cast<Buffer*>(m_device->m_timerQueryResolveBuffer)->resource,
            query->beginQueryIndex * 8);
    }

    void CommandList::beginMarker(const char* name)
    {
        PIXBeginEvent(m_ActiveCommandList->commandList, 0, name);
    }

    void CommandList::endMarker()
    {
        PIXEndEvent(m_ActiveCommandList->commandList);
    }

    D3D_PRIMITIVE_TOPOLOGY convertPrimitiveType(PrimitiveType::Enum pt)
    {
        switch (pt)
        {
        case PrimitiveType::POINT_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
        case PrimitiveType::LINE_LIST:
            return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            break;
        case PrimitiveType::TRIANGLE_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case PrimitiveType::TRIANGLE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        case PrimitiveType::PATCH_1_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
            break;
        case PrimitiveType::PATCH_3_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
            break;
        case PrimitiveType::PATCH_4_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
            break;
        default:
            return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
            break;
        }
    }

    void CommandList::bindGraphicsPipeline(GraphicsPipeline *pso)
    {
        const auto& state = pso->desc;

        m_ActiveCommandList->commandList->SetPipelineState(pso->pipelineState);
        m_ActiveCommandList->commandList->SetGraphicsRootSignature(pso->rootSignature->handle);

        m_ActiveCommandList->commandList->IASetPrimitiveTopology(convertPrimitiveType(state.primType));

        if (pso->viewportState.numViewports)
        {
            m_ActiveCommandList->commandList->RSSetViewports(pso->viewportState.numViewports, pso->viewportState.viewports);
        }

        if (pso->viewportState.numScissorRects)
        {
            m_ActiveCommandList->commandList->RSSetScissorRects(pso->viewportState.numViewports, pso->viewportState.scissorRects);
        }

        if (state.renderState.depthStencilState.stencilEnable)
        {
            m_ActiveCommandList->commandList->OMSetStencilRef(state.renderState.depthStencilState.stencilRefValue);
        }

        if (pso->requiresBlendFactors)
        {
            m_ActiveCommandList->commandList->OMSetBlendFactor(&state.renderState.blendState.blendFactor.r);
        }
    }

    void CommandList::bindFramebuffer(GraphicsPipeline *pso, Framebuffer *fb)
    {
        const auto& state = pso->desc;

        for (const auto& attachment : fb->desc.colorAttachments)
        {
            requireTextureState(attachment.texture, attachment.subresources,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        if (fb->desc.depthAttachment.valid())
        {
            const auto& attachment = fb->desc.depthAttachment;

            D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_DEPTH_READ;
            if (state.renderState.depthStencilState.depthWriteMask == DepthStencilState::DEPTH_WRITE_MASK_ALL ||
                state.renderState.depthStencilState.stencilWriteMask != 0)
                resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

            requireTextureState(attachment.texture, attachment.subresources, resourceState);
        }

        static_vector<D3D12_CPU_DESCRIPTOR_HANDLE, 16> RTVs;
        for (uint32_t rtIndex = 0; rtIndex < fb->RTVs.size(); rtIndex++)
        {
            RTVs.push_back(m_device->m_dhRTV.GetCpuHandle(fb->RTVs[rtIndex]));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE DSV = {};
        if (fb->desc.depthAttachment.valid())
            DSV = m_device->m_dhDSV.GetCpuHandle(fb->DSV);

        m_ActiveCommandList->commandList->OMSetRenderTargets(UINT(RTVs.size()), RTVs.data(), false, fb->desc.depthAttachment.valid() ? &DSV : nullptr);
    }

    void CommandList::setGraphicsState(const GraphicsState& state)
    {
        GraphicsPipeline* pso = static_cast<GraphicsPipeline*>(state.pipeline);
        Framebuffer* framebuffer = static_cast<Framebuffer*>(state.framebuffer);

        bool updateFramebuffer = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.framebuffer != state.framebuffer;
        bool updatePipeline = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.pipeline != state.pipeline;
        bool updateBindings = !m_CurrentGraphicsStateValid || arraysAreDifferent(m_CurrentGraphicsState.bindings, state.bindings);
        bool updateIndirectParams = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.indirectParams != state.indirectParams;

        bool updateDynamicViewports = false;
        bool previousViewportsWereDynamic = m_CurrentGraphicsStateValid && m_CurrentGraphicsState.viewport.viewports.size();
        if (state.viewport.viewports.size())
        {
            if (previousViewportsWereDynamic)
            {
                updateDynamicViewports =
                    arraysAreDifferent(m_CurrentGraphicsState.viewport.viewports, state.viewport.viewports) ||
                    arraysAreDifferent(m_CurrentGraphicsState.viewport.scissorRects, state.viewport.scissorRects);
            }
            else
            {
                updateDynamicViewports = true;
            }
        }
        else
        {
            if (previousViewportsWereDynamic)
            {
                updatePipeline = true; // which sets the static viewports
            }
        }

        bool updateIndexBuffer = !m_CurrentGraphicsStateValid || m_CurrentGraphicsState.indexBuffer != state.indexBuffer;
        bool updateVertexBuffers = !m_CurrentGraphicsStateValid || arraysAreDifferent(m_CurrentGraphicsState.vertexBuffers, state.vertexBuffers);

        if (commitDescriptorHeaps())
        {
            updateBindings = true;
        }

        if (updatePipeline)
        {
            bindGraphicsPipeline(pso);
            m_Instance->referencedResources.push_back(pso);
        }

        if (updateFramebuffer)
        {
            bindFramebuffer(pso, framebuffer);
            m_Instance->referencedResources.push_back(framebuffer);
        }

        bool indirectParamsTransitioned = false;

        if (updateBindings)
        {
            m_currentGraphicsVolatileCBs.resize(0);

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < uint32_t(state.bindings.size()); bindingSetIndex++)
            {
                IBindingSet* _bindingSet = state.bindings[bindingSetIndex];

                if (!_bindingSet)
                    continue;

                BindingSet* bindingSet = static_cast<BindingSet*>(_bindingSet);
                const std::pair<RefCountPtr<BindingLayout>, RootParameterIndex>& layoutAndOffset = pso->rootSignature->pipelineLayouts[bindingSetIndex];

                CHECK_ERROR(layoutAndOffset.first == bindingSet->layout, "This binding set has been created for a different layout. Out-of-order binding?");
                RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                for (uint32_t stage = ShaderType::SHADER_VERTEX; stage <= ShaderType::SHADER_ALL_GRAPHICS; stage++)
                {
                    // Bind the volatile constant buffers
                    for (const std::pair<RootParameterIndex, IBuffer*>& parameter : bindingSet->rootParametersVolatileCB[stage])
                    {
                        RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                        if (parameter.second)
                        {
                            Buffer* buffer = static_cast<Buffer*>(parameter.second);

                            if (buffer->desc.isVolatile)
                            {
                                const BufferState* bufferState = getBufferStateTracking(buffer, true);

                                CHECK_ERROR(bufferState->volatileData, "Attempted use of a volatile buffer before it was written into");

                                m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, bufferState->volatileData);

                                m_currentGraphicsVolatileCBs.push_back(VolatileConstantBufferBinding{ rootParameterIndex, bufferState, bufferState->volatileData });
                            }
                            else
                            {
                                assert(buffer->gpuVA != 0);

                                m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                            }
                        }
                        else
                        {
                            // This can only happen as a result of an improperly built binding set. 
                            // Such binding set should fail to create.
                            m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(rootParameterIndex, 0);
                        }
                    }

                    if (bindingSet->descriptorTablesValidSamplers[stage])
                    {
                        m_ActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                            rootParameterOffset + bindingSet->rootParameterIndicesSamplers[stage],
                            m_device->m_dhSamplers.GetGpuHandle(bindingSet->descriptorTablesSamplers[stage]));
                    }

                    if (bindingSet->descriptorTablesValidSRVetc[stage])
                    {
                        m_ActiveCommandList->commandList->SetGraphicsRootDescriptorTable(
                            rootParameterOffset + bindingSet->rootParameterIndicesSRVetc[stage],
                            m_device->m_dhSRVetc.GetGpuHandle(bindingSet->descriptorTablesSRVetc[stage]));
                    }
                }

                if(bindingSet->desc.trackLiveness)
                    m_Instance->referencedResources.push_back(bindingSet);

                for (auto& setup : bindingSet->barrierSetup)
                    setup(this, state.indirectParams, indirectParamsTransitioned);
            }
        }
        else
        {
            updateGraphicsVolatileConstantBuffers();
        }

        if (updateIndexBuffer)
        {
            m_currentVolatileIndexBuffer.bufferState = nullptr;

            D3D12_INDEX_BUFFER_VIEW IBV = {};

            if (state.indexBuffer.handle)
            {
                Buffer* buffer = static_cast<Buffer*>(state.indexBuffer.handle);

                IBV.Format = GetFormatMapping(state.indexBuffer.format).srvFormat;
                IBV.SizeInBytes = buffer->desc.byteSize - state.indexBuffer.offset;

                if (buffer->desc.isVolatile)
                {
                    const BufferState* bufferState = getBufferStateTracking(buffer, true);

                    CHECK_ERROR(bufferState->volatileData, "Attempted use of a volatile buffer before it was written into");

                    IBV.BufferLocation = bufferState->volatileData;

                    m_currentVolatileIndexBuffer = VolatileIndexBufferBinding{ 0, bufferState, IBV };
                    m_currentVolatileIndexBufferHandle = buffer;
                }
                else
                {
                    requireBufferState(state.indexBuffer.handle, D3D12_RESOURCE_STATE_INDEX_BUFFER);

                    IBV.BufferLocation = buffer->gpuVA + state.indexBuffer.offset;

                    m_Instance->referencedResources.push_back(state.indexBuffer.handle);
                    m_currentVolatileIndexBufferHandle = nullptr;
                }
            }
            else
            {
                m_currentVolatileIndexBufferHandle = nullptr;
            }

            m_ActiveCommandList->commandList->IASetIndexBuffer(&IBV);
        }
        else
        {
            updateGraphicsVolatileIndexBuffer();
        }

        if (updateVertexBuffers)
        {
            m_currentVolatileVertexBufferHandles.resize(state.vertexBuffers.size());
            m_currentVolatileVertexBuffers.resize(0);

            D3D12_VERTEX_BUFFER_VIEW VBVs[16] = {};

            InputLayout* inputLayout = static_cast<InputLayout*>(pso->desc.inputLayout.Get());

            for (size_t i = 0; i < state.vertexBuffers.size(); i++)
            {
                const VertexBufferBinding& binding = state.vertexBuffers[i];

                Buffer* buffer = static_cast<Buffer*>(binding.buffer);

                VBVs[binding.slot].StrideInBytes = inputLayout->elementStrides[binding.slot];
                VBVs[binding.slot].SizeInBytes = buffer->desc.byteSize - binding.offset;

                if (buffer->desc.isVolatile)
                {
                    const BufferState* bufferState = getBufferStateTracking(buffer, true);

                    CHECK_ERROR(bufferState->volatileData, "Attempted use of a volatile buffer before it was written into");

                    VBVs[binding.slot].BufferLocation = bufferState->volatileData;

                    m_currentVolatileVertexBuffers.push_back(VolatileVertexBufferBinding{ binding.slot, bufferState, VBVs[binding.slot] });
                    m_currentVolatileVertexBufferHandles[i] = buffer;
                }
                else
                {
                    requireBufferState(binding.buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

                    VBVs[binding.slot].BufferLocation = buffer->gpuVA + binding.offset;

                    m_Instance->referencedResources.push_back(buffer);
                    m_currentVolatileVertexBufferHandles[i] = nullptr;
                }

            }

            uint32_t numVertexBuffers = uint32_t(state.vertexBuffers.size());
            if (m_CurrentGraphicsStateValid)
                numVertexBuffers = std::max(numVertexBuffers, uint32_t(m_CurrentGraphicsState.vertexBuffers.size()));

            for (uint32_t i = 0; i < numVertexBuffers; i++)
            {
                m_ActiveCommandList->commandList->IASetVertexBuffers(i, 1, VBVs[i].BufferLocation != 0 ? &VBVs[i] : nullptr);
            }
        }
        else
        {
            updateGraphicsVolatileVertexBuffers();
        }

        if (state.indirectParams && updateIndirectParams && !indirectParamsTransitioned)
        {
            requireBufferState(state.indirectParams, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            m_Instance->referencedResources.push_back(state.indirectParams);
        }
        
        commitBarriers();

        if (updateDynamicViewports)
        {
            DX12_ViewportState vpState = convertViewportState(pso, state.viewport);

            if (vpState.numViewports)
            {
                assert(pso->viewportState.numViewports == 0);
                m_ActiveCommandList->commandList->RSSetViewports(vpState.numViewports, vpState.viewports);
            }

            if (vpState.numScissorRects)
            {
                assert(pso->viewportState.numScissorRects == 0);
                m_ActiveCommandList->commandList->RSSetScissorRects(vpState.numScissorRects, vpState.scissorRects);
            }
        }

#if NVRHI_D3D12_WITH_NVAPI
        bool updateSPS = m_CurrentSinglePassStereoState != pso->desc.renderState.singlePassStereo;

        if (updateSPS)
        {
            const SinglePassStereoState& spsState = pso->desc.renderState.singlePassStereo;

            NvAPI_Status Status = NvAPI_D3D12_SetSinglePassStereoMode(m_ActiveCommandList->commandList, spsState.enabled ? 2 : 1, spsState.renderTargetIndexOffset, spsState.independentViewportMask);

            CHECK_ERROR(Status == NVAPI_OK, "NvAPI_D3D12_SetSinglePassStereoMode call failed");

            m_CurrentSinglePassStereoState = spsState;
        }
#endif

        m_CurrentGraphicsStateValid = true;
        m_CurrentComputeStateValid = false;
#if NVRHI_WITH_DXR
        m_CurrentRayTracingStateValid = false;
#endif

        if (updatePipeline || updateFramebuffer || updateBindings || updateDynamicViewports || updateVertexBuffers || updateIndexBuffer || updateIndirectParams)
        {
            m_CurrentGraphicsState = state;
        }
    }

    void CommandList::updateGraphicsVolatileConstantBuffers()
    {
        for (VolatileConstantBufferBinding& parameter : m_currentGraphicsVolatileCBs)
        {
            if (parameter.bufferState->volatileData != parameter.view)
            {
                m_ActiveCommandList->commandList->SetGraphicsRootConstantBufferView(parameter.bindingPoint, parameter.bufferState->volatileData);

                parameter.view = parameter.bufferState->volatileData;
            }
        }
    }

    void CommandList::updateGraphicsVolatileIndexBuffer()
    {
        if (m_currentVolatileIndexBuffer.bufferState)
        {
            if (m_currentVolatileIndexBuffer.bufferState->volatileData != m_currentVolatileIndexBuffer.view.BufferLocation)
            {
                m_currentVolatileIndexBuffer.view.BufferLocation = m_currentVolatileIndexBuffer.bufferState->volatileData;

                m_ActiveCommandList->commandList->IASetIndexBuffer(&m_currentVolatileIndexBuffer.view);
            }
        }
    }

    void CommandList::updateGraphicsVolatileVertexBuffers()
    {
        for (VolatileBufferBinding<D3D12_VERTEX_BUFFER_VIEW>& parameter : m_currentVolatileVertexBuffers)
        {
            if (parameter.bufferState->volatileData != parameter.view.BufferLocation)
            {
                parameter.view.BufferLocation = parameter.bufferState->volatileData;
                m_ActiveCommandList->commandList->IASetVertexBuffers(parameter.bindingPoint, 1, &parameter.view);
            }
        }
    }

    void CommandList::updateGraphicsVolatileBuffers()
    {
        // If there are some volatile buffers bound, and they have been written into since the last draw or setGraphicsState, patch their views
        updateGraphicsVolatileConstantBuffers();
        updateGraphicsVolatileIndexBuffer();
        updateGraphicsVolatileVertexBuffers();
    }

    void CommandList::draw(const DrawArguments& args)
    {
        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->DrawInstanced(args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndexed(const DrawArguments& args)
    {
        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->DrawIndexedInstanced(args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
    }

    void CommandList::drawIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = static_cast<Buffer*>(m_CurrentGraphicsState.indirectParams);
        CHECK_ERROR(indirectParams, "DrawIndirect parameters buffer is not set");

        updateGraphicsVolatileBuffers();

        m_ActiveCommandList->commandList->ExecuteIndirect(m_device->m_drawIndirectSignature, 1, indirectParams->resource, offsetBytes, nullptr, 0);
    }

    void CommandList::setComputeState(const ComputeState& state)
    {
        ComputePipeline* pso = static_cast<ComputePipeline*>(state.pipeline);

        bool updatePipeline = !m_CurrentComputeStateValid || m_CurrentComputeState.pipeline != state.pipeline;
        bool updateBindings = updatePipeline || arraysAreDifferent(m_CurrentComputeState.bindings, state.bindings);
        bool updateIndirectParams = !m_CurrentComputeStateValid || m_CurrentComputeState.indirectParams != state.indirectParams;

        if (commitDescriptorHeaps())
        {
            updateBindings = true;
        }

        if (updatePipeline)
        {
            m_ActiveCommandList->commandList->SetPipelineState(pso->pipelineState);
            m_ActiveCommandList->commandList->SetComputeRootSignature(pso->rootSignature->handle);

            m_Instance->referencedResources.push_back(pso);
        }

        bool indirectParamsTransitioned = false;

        if (updateBindings)
        {
            // TODO: verify that all layouts have corresponding binding sets

            for (uint32_t bindingSetIndex = 0; bindingSetIndex < uint32_t(state.bindings.size()); bindingSetIndex++)
            {
                IBindingSet* _bindingSet = state.bindings[bindingSetIndex];

                m_currentComputeVolatileCBs.resize(0);

                if (!_bindingSet)
                    continue;

                BindingSet* bindingSet = static_cast<BindingSet*>(_bindingSet);
                const std::pair<RefCountPtr<BindingLayout>, RootParameterIndex>& layoutAndOffset = pso->rootSignature->pipelineLayouts[bindingSetIndex];

                CHECK_ERROR(layoutAndOffset.first == bindingSet->layout, "This binding set has been created for a different layout. Out-of-order binding?");
                RootParameterIndex rootParameterOffset = layoutAndOffset.second;

                // Bind the volatile constant buffers
                for (const std::pair<RootParameterIndex, IBuffer*>& parameter : bindingSet->rootParametersVolatileCB[ShaderType::SHADER_COMPUTE])
                {
                    RootParameterIndex rootParameterIndex = rootParameterOffset + parameter.first;

                    if (parameter.second)
                    {
                        Buffer* buffer = static_cast<Buffer*>(parameter.second);

                        if (buffer->desc.isVolatile)
                        {
                            const BufferState* bufferState = getBufferStateTracking(buffer, true);

                            CHECK_ERROR(bufferState->volatileData, "Attempted use of a volatile buffer before it was written into");

                            m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, bufferState->volatileData);

                            m_currentComputeVolatileCBs.push_back(VolatileConstantBufferBinding{ rootParameterIndex, bufferState, bufferState->volatileData });
                        }
                        else
                        {
                            assert(buffer->gpuVA != 0);

                            m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, buffer->gpuVA);
                        }
                    }
                    else
                    {
                        // This can only happen as a result of an improperly built binding set. 
                        // Such binding set should fail to create.
                        m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(rootParameterIndex, 0);
                    }
                }

                if (bindingSet->descriptorTablesValidSamplers[ShaderType::SHADER_COMPUTE])
                {
                    m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                        rootParameterOffset + bindingSet->rootParameterIndicesSamplers[ShaderType::SHADER_COMPUTE],
                        m_device->m_dhSamplers.GetGpuHandle(bindingSet->descriptorTablesSamplers[ShaderType::SHADER_COMPUTE]));
                }

                if (bindingSet->descriptorTablesValidSRVetc[ShaderType::SHADER_COMPUTE])
                {
                    m_ActiveCommandList->commandList->SetComputeRootDescriptorTable(
                        rootParameterOffset + bindingSet->rootParameterIndicesSRVetc[ShaderType::SHADER_COMPUTE],
                        m_device->m_dhSRVetc.GetGpuHandle(bindingSet->descriptorTablesSRVetc[ShaderType::SHADER_COMPUTE]));
                }

                if(bindingSet->desc.trackLiveness)
                    m_Instance->referencedResources.push_back(bindingSet);

                for (auto& setup : bindingSet->barrierSetup)
                    setup(this, state.indirectParams, indirectParamsTransitioned);
            }
        }

        if (state.indirectParams && updateIndirectParams && !indirectParamsTransitioned)
        {
            requireBufferState(state.indirectParams, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            m_Instance->referencedResources.push_back(state.indirectParams);
        }

        m_CurrentComputeStateValid = true;
        m_CurrentGraphicsStateValid = false;
#if NVRHI_WITH_DXR
        m_CurrentRayTracingStateValid = false;
#endif
        if (updatePipeline || updateBindings || updateIndirectParams)
        {
            m_CurrentComputeState = state;
        }

        commitBarriers();
    }

    void CommandList::updateComputeVolatileBuffers()
    {
        // If there are some volatile buffers bound, and they have been written into since the last dispatch or setComputeState, patch their views

        for (VolatileConstantBufferBinding& parameter : m_currentComputeVolatileCBs)
        {
            if (parameter.bufferState->volatileData != parameter.view)
            {
                m_ActiveCommandList->commandList->SetComputeRootConstantBufferView(parameter.bindingPoint, parameter.bufferState->volatileData);

                parameter.view = parameter.bufferState->volatileData;
            }
        }
    }

    void CommandList::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        updateComputeVolatileBuffers();

        m_ActiveCommandList->commandList->Dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandList::dispatchIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = static_cast<Buffer*>(m_CurrentComputeState.indirectParams);
        CHECK_ERROR(indirectParams, "DispatchIndirect parameters buffer is not set");

        updateComputeVolatileBuffers();

        m_ActiveCommandList->commandList->ExecuteIndirect(m_device->m_dispatchIndirectSignature, 1, indirectParams->resource, offsetBytes, nullptr, 0);
    }

    void CommandList::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)
    {
        TextureState* tracking = getTextureStateTracking(texture, true);

        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandList::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);
        
        tracking->enableUavBarriers = enableBarriers;
        tracking->firstUavBarrierPlaced = false;
    }

    void CommandList::open()
    {
        ++m_recordingInstanceID;

        m_completedInstanceID = m_fence->GetCompletedValue();

        std::shared_ptr<InternalCommandList> chunk;

        if (!m_CommandListPool.empty())
        {
            chunk = m_CommandListPool.front();

            if (chunk->lastInstanceID <= m_completedInstanceID)
            {
                chunk->allocator->Reset();
                chunk->commandList->Reset(chunk->allocator, nullptr);
                m_CommandListPool.pop_front();
            }
            else
            {
                chunk = nullptr;
            }
        }

        if (chunk == nullptr)
        {
            chunk = createInternalCommandList();
        }

        m_ActiveCommandList = chunk;
        m_ActiveCommandList->lastInstanceID = m_recordingInstanceID;

        m_Instance = std::make_shared<CommandListInstance>();
        m_Instance->commandAllocator = m_ActiveCommandList->allocator;
        m_Instance->commandList = m_ActiveCommandList->commandList;
        m_Instance->instanceID = m_recordingInstanceID;
        m_Instance->fence = m_fence;
    }

    void CommandList::keepBufferInitialStates()
    {
        for (auto& it : m_bufferStates)
        {
            Buffer* buffer = static_cast<Buffer*>(it.first);
            BufferState* tracking = it.second.get();

            if (buffer->desc.keepInitialState && !buffer->IsPermanent() && !tracking->permanentTransition && !buffer->desc.isVolatile)
            {
                D3D12_RESOURCE_STATES d3dstate = TranslateResourceStates(buffer->desc.initialState);

                if (tracking->state != d3dstate)
                {
                    D3D12_RESOURCE_BARRIER barrier = {};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = buffer->resource;
                    barrier.Transition.StateBefore = tracking->state;
                    barrier.Transition.StateAfter = d3dstate;
                    barrier.Transition.Subresource = 0;
                    m_barrier.push_back(barrier);
                }
            }
        }
    }

    void CommandList::keepTextureInitialStates()
    {
        for (auto& it : m_textureStates)
        {
            Texture* texture = static_cast<Texture*>(it.first);
            TextureState* tracking = it.second.get();

            if (texture->desc.keepInitialState && !texture->IsPermanent() && !tracking->permanentTransition)
            {
                D3D12_RESOURCE_STATES d3dstate = TranslateResourceStates(texture->desc.initialState);

                for (uint32_t subresource = 0; subresource < static_cast<uint32_t>(tracking->subresourceStates.size()); subresource++)
                {
                    if (tracking->subresourceStates[subresource] != d3dstate)
                    {
                        D3D12_RESOURCE_BARRIER barrier = {};
                        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        barrier.Transition.pResource = texture->resource;
                        barrier.Transition.StateBefore = tracking->subresourceStates[subresource];
                        barrier.Transition.StateAfter = d3dstate;
                        barrier.Transition.Subresource = subresource;
                        m_barrier.push_back(barrier);
                    }
                }
            }
        }
    }

    void CommandList::clearStateCache()
    {
        m_CurrentGraphicsStateValid = false;
        m_CurrentComputeStateValid = false;
#if NVRHI_WITH_DXR
        m_CurrentRayTracingStateValid = false;
#endif
        m_CurrentHeapSRVetc = nullptr;
        m_CurrentHeapSamplers = nullptr;
        m_currentGraphicsVolatileCBs.resize(0);
        m_currentComputeVolatileCBs.resize(0);
        m_currentVolatileIndexBuffer.bufferState = nullptr;
        m_currentVolatileVertexBuffers.resize(0);
        m_CurrentSinglePassStereoState = SinglePassStereoState();

        // Release the strong references to pipeline objects
        m_currentVolatileVertexBufferHandles.resize(0);
        m_currentVolatileIndexBufferHandle = nullptr;
    }

    void CommandList::clearState()
    {
        m_ActiveCommandList->commandList->ClearState(nullptr);

#if NVRHI_D3D12_WITH_NVAPI
        if (m_CurrentGraphicsStateValid && m_CurrentSinglePassStereoState.enabled)
        {
            NvAPI_Status Status = NvAPI_D3D12_SetSinglePassStereoMode(m_ActiveCommandList->commandList, 1, 0, false);

            CHECK_ERROR(Status == NVAPI_OK, "NvAPI_D3D12_SetSinglePassStereoMode call failed");
        }
#endif

        clearStateCache();

        commitDescriptorHeaps();
    }

    void CommandList::close()
    {
        keepTextureInitialStates();
        keepBufferInitialStates();

        commitBarriers();

        m_ActiveCommandList->commandList->Close();

        clearStateCache();

        m_CurrentUploadBuffer = nullptr;

        m_textureStates.clear();
        m_bufferStates.clear();
#if NVRHI_WITH_DXR
        m_shaderTableStates.clear();
#endif
    }

    std::shared_ptr<CommandListInstance> CommandList::execute(ID3D12CommandQueue* pQueue)
    {
        ID3D12CommandList* pCommandList = m_ActiveCommandList->commandList;
        pQueue->ExecuteCommandLists(1, &pCommandList);
        pQueue->Signal(m_fence, m_Instance->instanceID);
        
        std::shared_ptr<CommandListInstance> instance = m_Instance;

        m_CommandListPool.push_back(m_ActiveCommandList);
        m_ActiveCommandList.reset();
        m_Instance.reset();

        return instance;
    }

    TextureState* CommandList::getTextureStateTracking(ITexture* _texture, bool allowCreate)
    {
        auto it = m_textureStates.find(_texture);

        if (it != m_textureStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;

        Texture* texture = static_cast<Texture*>(_texture);

        const TextureDesc& d = texture->desc;
        uint32_t numSubresources = d.mipLevels * d.arraySize * texture->planeCount;

        std::unique_ptr<TextureState> trackingRef = std::make_unique<TextureState>(numSubresources);

        TextureState* tracking = trackingRef.get();
        m_textureStates.insert(std::make_pair(texture, std::move(trackingRef)));

        if (texture->desc.keepInitialState)
        {
            D3D12_RESOURCE_STATES d3dState = TranslateResourceStates(texture->desc.initialState);

            for (auto& subresourceState : tracking->subresourceStates)
                subresourceState = d3dState;
        }

        return tracking;
    }

    BufferState* CommandList::getBufferStateTracking(IBuffer* _buffer, bool allowCreate)
    {
        auto it = m_bufferStates.find(_buffer);

        if (it != m_bufferStates.end())
        {
            return it->second.get();
        }

        if (!allowCreate)
            return nullptr;

        std::unique_ptr<BufferState> trackingRef = std::make_unique<BufferState>();
        
        BufferState* tracking = trackingRef.get();
        m_bufferStates.insert(std::make_pair(_buffer, std::move(trackingRef)));

        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (buffer->desc.keepInitialState)
        {
            D3D12_RESOURCE_STATES d3dState = TranslateResourceStates(buffer->desc.initialState);

            tracking->state = d3dState;
        }

        return tracking;
    }

#if NVRHI_WITH_DXR
    dxr::ShaderTableState* CommandList::getShaderTableStateTracking(rt::IShaderTable* shaderTable)
    {
        auto it = m_shaderTableStates.find(shaderTable);

        if (it != m_shaderTableStates.end())
        {
            return it->second.get();
        }

        std::unique_ptr<dxr::ShaderTableState> trackingRef = std::make_unique<dxr::ShaderTableState>();

        dxr::ShaderTableState* tracking = trackingRef.get();
        m_shaderTableStates.insert(std::make_pair(shaderTable, std::move(trackingRef)));

        return tracking;
    }
#endif

    D3D12_RESOURCE_STATES TranslateResourceStates(ResourceStates::Enum stateBits)
    {
        if (stateBits == ResourceStates::COMMON)
            return D3D12_RESOURCE_STATE_COMMON;

        D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON; // also 0

        if (stateBits & ResourceStates::CONSTANT_BUFFER) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if (stateBits & ResourceStates::VERTEX_BUFFER) result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if (stateBits & ResourceStates::INDEX_BUFFER) result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
        if (stateBits & ResourceStates::INDIRECT_ARGUMENT) result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        if (stateBits & ResourceStates::SHADER_RESOURCE) result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (stateBits & ResourceStates::UNORDERED_ACCESS) result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (stateBits & ResourceStates::RENDER_TARGET) result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (stateBits & ResourceStates::DEPTH_WRITE) result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
        if (stateBits & ResourceStates::DEPTH_READ) result |= D3D12_RESOURCE_STATE_DEPTH_READ;
        if (stateBits & ResourceStates::STREAM_OUT) result |= D3D12_RESOURCE_STATE_STREAM_OUT;
        if (stateBits & ResourceStates::COPY_DEST) result |= D3D12_RESOURCE_STATE_COPY_DEST;
        if (stateBits & ResourceStates::COPY_SOURCE) result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (stateBits & ResourceStates::RESOLVE_DEST) result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
        if (stateBits & ResourceStates::RESOLVE_SOURCE) result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        if (stateBits & ResourceStates::PRESENT) result |= D3D12_RESOURCE_STATE_PRESENT;
#if NVRHI_WITH_DXR
        if (stateBits & ResourceStates::RAY_TRACING_AS) result |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
#endif

        return result;
    }

    ResourceStates::Enum TranslateResourceStatesFromD3D(D3D12_RESOURCE_STATES stateBits)
    {
        if (stateBits == D3D12_RESOURCE_STATE_COMMON)
            return ResourceStates::COMMON;

        ResourceStates::Enum result = ResourceStates::COMMON; // also 0

        if (stateBits & D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) result = ResourceStates::Enum(result | ResourceStates::CONSTANT_BUFFER | ResourceStates::VERTEX_BUFFER);
        if (stateBits & D3D12_RESOURCE_STATE_INDEX_BUFFER) result = ResourceStates::Enum(result | ResourceStates::INDEX_BUFFER);
        if (stateBits & D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) result = ResourceStates::Enum(result | ResourceStates::INDIRECT_ARGUMENT);
        if (stateBits & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) result = ResourceStates::Enum(result | ResourceStates::SHADER_RESOURCE);
        if (stateBits & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) result = ResourceStates::Enum(result | ResourceStates::SHADER_RESOURCE);
        if (stateBits & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) result = ResourceStates::Enum(result | ResourceStates::UNORDERED_ACCESS);
        if (stateBits & D3D12_RESOURCE_STATE_RENDER_TARGET) result = ResourceStates::Enum(result | ResourceStates::RENDER_TARGET);
        if (stateBits & D3D12_RESOURCE_STATE_DEPTH_WRITE) result = ResourceStates::Enum(result | ResourceStates::DEPTH_WRITE);
        if (stateBits & D3D12_RESOURCE_STATE_DEPTH_READ) result = ResourceStates::Enum(result | ResourceStates::DEPTH_READ);
        if (stateBits & D3D12_RESOURCE_STATE_STREAM_OUT) result = ResourceStates::Enum(result | ResourceStates::STREAM_OUT);
        if (stateBits & D3D12_RESOURCE_STATE_COPY_DEST) result = ResourceStates::Enum(result | ResourceStates::COPY_DEST);
        if (stateBits & D3D12_RESOURCE_STATE_COPY_SOURCE) result = ResourceStates::Enum(result | ResourceStates::COPY_SOURCE);
        if (stateBits & D3D12_RESOURCE_STATE_RESOLVE_DEST) result = ResourceStates::Enum(result | ResourceStates::RESOLVE_DEST);
        if (stateBits & D3D12_RESOURCE_STATE_RESOLVE_SOURCE) result = ResourceStates::Enum(result | ResourceStates::RESOLVE_SOURCE);
        if (stateBits & D3D12_RESOURCE_STATE_PRESENT) result = ResourceStates::Enum(result | ResourceStates::PRESENT);
#if NVRHI_WITH_DXR
        if (stateBits & D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) result = ResourceStates::Enum(result | ResourceStates::RAY_TRACING_AS);
#endif

        return result;
    }

    void CommandList::beginTrackingTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        TextureState* tracking = getTextureStateTracking(texture, true);
        D3D12_RESOURCE_STATES d3dstate = TranslateResourceStates(stateBits);
        const TextureDesc& desc = texture->desc;

        subresources = subresources.resolve(desc, false);

        for (int plane = 0; plane < texture->planeCount; plane++)
        {
            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
                {
                    uint32_t subresource = calcSubresource(mipLevel, arraySlice, plane, desc.mipLevels, desc.arraySize);
                    tracking->subresourceStates[subresource] = d3dstate;
                }
            }
        }
    }

    void CommandList::beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        tracking->state = TranslateResourceStates(stateBits);
    }

    void CommandList::endTrackingTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        D3D12_RESOURCE_STATES d3dstate = TranslateResourceStates(stateBits);
        const TextureDesc& desc = texture->desc;

        subresources = subresources.resolve(desc, false);

        if (permanent)
        {
            CHECK_ERROR(subresources.isEntireTexture(desc), "Permanent transitions are only possible on entire resources");
        }

        bool anyUavBarrier = false;

        for (int plane = 0; plane < texture->planeCount; plane++)
        {
            for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
            {
                for (ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < subresources.baseArraySlice + subresources.numArraySlices; arraySlice++)
                {
                    uint32_t subresource = calcSubresource(mipLevel, arraySlice, plane, desc.mipLevels, desc.arraySize);
                    requireTextureState(texture, subresource, d3dstate, anyUavBarrier);
                }
            }
        }

        if (permanent)
        {
            m_permanentTextureStates.push_back(std::make_pair(texture, d3dstate));
            getTextureStateTracking(_texture, true)->permanentTransition = true;
        }

        commitBarriers();
    }

    void CommandList::endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent)
    {
        D3D12_RESOURCE_STATES d3dstate = TranslateResourceStates(stateBits);
        requireBufferState(buffer, d3dstate);

        if (permanent)
        {
            m_permanentBufferStates.push_back(std::make_pair(buffer, d3dstate));
        }

        commitBarriers();
    }

    ResourceStates::Enum CommandList::getTextureSubresourceState(ITexture* _texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        TextureState* tracking = getTextureStateTracking(texture, true);

        if (!tracking)
            return ResourceStates::COMMON;

        uint32_t subresource = calcSubresource(mipLevel, arraySlice, 0, texture->desc.mipLevels, texture->desc.arraySize);
        return TranslateResourceStatesFromD3D(tracking->subresourceStates[subresource]);
    }

    ResourceStates::Enum CommandList::getBufferState(IBuffer* buffer)
    {
        BufferState* tracking = getBufferStateTracking(buffer, true);

        if (!tracking)
            return ResourceStates::COMMON;

        return TranslateResourceStatesFromD3D(tracking->state);
    }

} // namespace d3d12
} // namespace nvrhi