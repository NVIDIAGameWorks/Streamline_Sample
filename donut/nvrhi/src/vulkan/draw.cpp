#include <nvrhi/vulkan.h>


namespace nvrhi 
{
namespace vulkan
{

void BarrierTracker::update(Buffer *buffer,
                            vk::PipelineStageFlags dstStageFlags,
                            vk::AccessFlags dstAccessMask)
{
    auto& barrierInfo = bufferBarrierInfo[buffer];

    barrierInfo.stageFlags |= dstStageFlags;
    barrierInfo.accessMask |= dstAccessMask;
}

void BarrierTracker::update(TextureSubresourceView *view,
                            vk::PipelineStageFlags dstStageFlags,
                            vk::AccessFlags dstAccessMask,
                            vk::ImageLayout dstLayout)
{
    auto iter = imageBarrierInfo.find(view);

    if (iter != imageBarrierInfo.end())
    {
        // we've already done this before --- layouts must match
        assert(iter->second.layout == dstLayout);
    } else {
        // haven't seen this view yet, create new barrier info slot
        iter = std::get<0>(imageBarrierInfo.emplace(view, ImageBarrierInfo()));
    }

    auto& barrierInfo = iter->second;

    barrierInfo.stageFlags |= dstStageFlags;
    barrierInfo.accessMask |= dstAccessMask;
    barrierInfo.layout = dstLayout;
}

void BarrierTracker::execute(TrackedCommandBuffer *cmd)
{
    for(const auto& b : bufferBarrierInfo)
    {
        Buffer *buf = b.first;
        const MemoryBarrierInfo& barrier = b.second;

        buf->barrier(cmd,
                     barrier.stageFlags,
                     barrier.accessMask);
    }

    for(const auto& i : imageBarrierInfo)
    {
        TextureSubresourceView& view = *i.first;
        const ImageBarrierInfo& barrier = i.second;

        view.texture.barrier(cmd,
                             view,
                             barrier.stageFlags,
                             barrier.accessMask,
                             barrier.layout);
    }
}

TrackedCommandBuffer *Device::getCmdBuf(QueueID queueId)
{
    assert(queueId < QueueID::Count);

    if (internalCmd)
    {
        if (internalCmd->targetQueueID != queueId)
        {
            // switching queues, submit previous cmd
            flushCommandList();
            assert(internalCmd == nullptr);
        }
    }

    if (!internalCmd)
    {
        // allocate a new command buffer
        internalCmd = queues[queueId].createOneShotCmdBuf();
    }

    return internalCmd;
}

TrackedCommandBuffer *Device::getAnyCmdBuf()
{
    if (internalCmd)
    {
        return internalCmd;
    } else {
        return getCmdBuf(QueueID::Graphics);
    }
}

TrackedCommandBuffer *Device::pollAnyCmdBuf()
{
    return internalCmd;
}

void Device::trackResourcesAndBarriers(TrackedCommandBuffer *cmd,
                                                        BarrierTracker& barrierTracker,
                                                        const ResourceBindingMap& bindingMap,
                                                        const StageBindingSetDesc& bindings,
                                                        vk::PipelineStageFlags stageFlags)
{
    for(const auto& binding : bindings)
    {
        const auto& layoutIter = bindingMap.find({ binding.slot, binding.type });
        assert(layoutIter != bindingMap.end());
        const auto& layout = layoutIter->second;

        // We cast twice since the types in binding.resourceHandle can have multiple inheritance,
        // and static_cast is only guaranteed to give us the right pointer if we go down the
        // right inheritance chain...
        auto pResource = static_cast<IResource *>(binding.resourceHandle);

        switch(layout.type)
        {
            case ResourceType::Texture_SRV:
                {
                    auto texture = static_cast<Texture *>(pResource);

                    auto& view = texture->getSubresourceView(TextureSubresource(binding.subresources.resolve(texture->desc, false)));

                    barrierTracker.update(&view,
                                          stageFlags,
                                          vk::AccessFlagBits::eShaderRead,
                                          vk::ImageLayout::eShaderReadOnlyOptimal);
                    cmd->markRead(texture);
                }

                break;

            case ResourceType::Texture_UAV:
                {
                    auto texture = static_cast<Texture *>(pResource);

                    auto& view = texture->getSubresourceView(TextureSubresource(binding.subresources.resolve(texture->desc, true)));

                    barrierTracker.update(&view,
                                          stageFlags,
                                          vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                                          vk::ImageLayout::eGeneral);
                    cmd->markRead(texture);
                    cmd->markWrite(texture);
                }

                break;

            case ResourceType::Buffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
                {
                    auto buffer = static_cast<Buffer *>(pResource);
                    barrierTracker.update(buffer, stageFlags, vk::AccessFlagBits::eShaderRead);
                    cmd->markRead(buffer);
                }

                break;

            case ResourceType::Buffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
                {
                    auto buffer = static_cast<Buffer *>(pResource);
                    barrierTracker.update(buffer,
                                        stageFlags,
                                        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
                    cmd->markRead(buffer);
                    cmd->markWrite(buffer);
                }

                break;

            case ResourceType::ConstantBuffer:
            case ResourceType::VolatileConstantBuffer:
                {
                    auto buffer = static_cast<Buffer *>(pResource);
                    barrierTracker.update(buffer, stageFlags, vk::AccessFlagBits::eShaderRead);
                    cmd->markRead(buffer);
                }

                break;

            case ResourceType::Sampler:
                // do nothing
                break;
        }
    }
}

void Device::trackResourcesAndBarriers(TrackedCommandBuffer *cmd, const GraphicsState& state)
{
    GraphicsPipeline* pso = static_cast<GraphicsPipeline*>(state.pipeline);
    Framebuffer* fb = static_cast<Framebuffer*>(state.framebuffer);
    assert(fb);

    BarrierTracker barrierTracker;

    cmd->referencedResources.push_back(pso);

    if (state.indexBuffer.handle)
    {
        Buffer* indexBuffer = static_cast<Buffer*>(state.indexBuffer.handle);
        cmd->referencedResources.push_back(indexBuffer);

        barrierTracker.update(indexBuffer,
                              vk::PipelineStageFlagBits::eVertexInput,
                              vk::AccessFlagBits::eIndexRead);
        cmd->markRead(indexBuffer);
    }

    // track shader resources for all stages
    assert(pso->pipelineBindingLayouts.size() == state.bindings.size());

    for(size_t i = 0; i < state.bindings.size(); i++)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(pso->pipelineBindingLayouts[i].Get());
        cmd->referencedResources.push_back(layout);

        ResourceBindingSet* bindingSet = static_cast<ResourceBindingSet*>(state.bindings[i]);
        cmd->referencedResources.push_back(bindingSet);

        trackResourcesAndBarriers(cmd,
            barrierTracker,
            layout->bindingMap_VS,
            bindingSet->desc.VS,
            vk::PipelineStageFlagBits::eVertexShader);

        trackResourcesAndBarriers(cmd,
            barrierTracker,
            layout->bindingMap_HS,
            bindingSet->desc.HS,
            vk::PipelineStageFlagBits::eTessellationControlShader);

        trackResourcesAndBarriers(cmd,
            barrierTracker,
            layout->bindingMap_DS,
            bindingSet->desc.DS,
            vk::PipelineStageFlagBits::eTessellationEvaluationShader);

        trackResourcesAndBarriers(cmd,
            barrierTracker,
            layout->bindingMap_GS,
            bindingSet->desc.GS,
            vk::PipelineStageFlagBits::eGeometryShader);

        trackResourcesAndBarriers(cmd,
            barrierTracker,
            layout->bindingMap_PS,
            bindingSet->desc.PS,
            vk::PipelineStageFlagBits::eFragmentShader);
    }

    for(const auto& vb : state.vertexBuffers)
    {
        Buffer* vertexBuffer = static_cast<Buffer*>(vb.buffer);
        cmd->referencedResources.push_back(vertexBuffer);

        barrierTracker.update(vertexBuffer,
                              vk::PipelineStageFlagBits::eVertexInput,
                              vk::AccessFlagBits::eVertexAttributeRead);
        cmd->markRead(vertexBuffer);
    }

    cmd->referencedResources.push_back(fb);

    for(const auto& attachment : fb->desc.colorAttachments)
    {
        Texture* texture = static_cast<Texture*>(attachment.texture);
        cmd->referencedResources.push_back(texture);

        auto& view = texture->getSubresourceView(attachment.subresources.resolve(texture->desc, true));

        barrierTracker.update(&view,
                              vk::PipelineStageFlagBits::eColorAttachmentOutput,
                              vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                              vk::ImageLayout::eColorAttachmentOptimal);

        cmd->markRead(texture);
        cmd->markWrite(texture);
    }

    if (fb->desc.depthAttachment.valid())
    {
        auto& attachment = fb->desc.depthAttachment;
        Texture* texture = static_cast<Texture*>(attachment.texture);
        cmd->referencedResources.push_back(texture);

        auto& view = texture->getSubresourceView(attachment.subresources.resolve(texture->desc, true));

        barrierTracker.update(&view,
                              vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
                              vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                              vk::ImageLayout::eDepthStencilAttachmentOptimal);

        cmd->markRead(texture);
        cmd->markWrite(texture);
    }

    if (state.indirectParams)
    {
        Buffer* indirectParams = static_cast<Buffer*>(state.indirectParams);
        cmd->referencedResources.push_back(indirectParams);

        // include the indirect params buffer in the barrier tracker state
        barrierTracker.update(indirectParams,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eIndirectCommandRead);
        cmd->markRead(indirectParams);
    }

    barrierTracker.execute(cmd);
}

void Device::bindGraphicsPipeline(TrackedCommandBuffer *cmd, GraphicsPipeline *pso, Framebuffer *fb)
{
    cmd->bindPSO(vk::PipelineBindPoint::eGraphics, pso->pipeline);
    cmd->bindFB(fb);
}

void Device::bindGraphicsState(TrackedCommandBuffer *cmd, const GraphicsState& state)
{
    GraphicsPipeline* pso = static_cast<GraphicsPipeline*>(state.pipeline);
    Framebuffer* fb = static_cast<Framebuffer*>(state.framebuffer);

    cmd->unbindFB();

    trackResourcesAndBarriers(cmd, state);

    bindGraphicsPipeline(cmd, pso, fb);

    BindingVector<vk::DescriptorSet> descriptorSets;
    for(const auto& bindingSet : state.bindings)
    {
        descriptorSets.push_back(static_cast<ResourceBindingSet*>(bindingSet)->descriptorSet);
    }

    cmd->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pso->pipelineLayout, descriptorSets);

    if (state.viewport.viewports.size())
    {
        assert(pso->viewportStateDynamic);

        nvrhi::static_vector<vk::Viewport, ViewportState::MAX_VIEWPORTS> viewports;
        for(const auto& vp : state.viewport.viewports)
        {
            viewports.push_back(VKViewportWithDXCoords(vp));
        }

        cmd->cmdBuf.setViewport(0, uint32_t(viewports.size()), viewports.data());
    }

    if (state.viewport.scissorRects.size())
    {
        assert(pso->scissorStateDynamic);

        nvrhi::static_vector<vk::Rect2D, ViewportState::MAX_VIEWPORTS> scissors;
        for(const auto& sc : state.viewport.scissorRects)
        {
            scissors.push_back(vk::Rect2D(vk::Offset2D(sc.minX, sc.minY),
                                          vk::Extent2D(std::abs(sc.maxX - sc.minX), std::abs(sc.maxY - sc.minY))));
        }

        cmd->cmdBuf.setScissor(0, uint32_t(scissors.size()), scissors.data());
    }

    if (state.indexBuffer.handle)
    {
        cmd->cmdBuf.bindIndexBuffer(static_cast<Buffer*>(state.indexBuffer.handle)->buffer,
                                    state.indexBuffer.offset,
                                    state.indexBuffer.format == Format::R16_UINT ?
                                        vk::IndexType::eUint16 : vk::IndexType::eUint32);
    }

    if (state.vertexBuffers.size())
    {
        static_vector<vk::Buffer, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> vertexBuffers;
        static_vector<vk::DeviceSize, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> vertexBufferOffsets;

        for(const auto& vb : state.vertexBuffers)
        {
            vertexBuffers.push_back(static_cast<Buffer*>(vb.buffer)->buffer);
            vertexBufferOffsets.push_back(vk::DeviceSize(vb.offset));
        }

        cmd->cmdBuf.bindVertexBuffers(0, uint32_t(vertexBuffers.size()), vertexBuffers.data(), vertexBufferOffsets.data());
    }

    m_CurrentDrawIndirectBuffer = state.indirectParams;
}

void Device::setGraphicsState(const GraphicsState& state)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    bindGraphicsState(cmd, state);
}

void Device::draw(const DrawArguments& args)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    cmd->cmdBuf.draw(args.vertexCount,
        args.instanceCount,
        args.startVertexLocation,
        args.startInstanceLocation);
}

void Device::drawIndexed(const DrawArguments& args)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    cmd->cmdBuf.drawIndexed(args.vertexCount,
        args.instanceCount,
        args.startIndexLocation,
        args.startVertexLocation,
        args.startInstanceLocation);
}

void Device::drawIndirect(uint32_t offsetBytes)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Graphics);
    assert(cmd);

    Buffer* indirectParams = static_cast<Buffer*>(m_CurrentDrawIndirectBuffer.Get());
    assert(indirectParams);

    // TODO: is this right?
    cmd->cmdBuf.drawIndirect(indirectParams->buffer, offsetBytes, 1, 0);
}

void Device::flushCommandList()
{
    if (internalCmd)
    {
        if (internalCmd->targetQueueID == QueueID::Graphics)
        {
            internalCmd->unbindFB();
        }

        queues[internalCmd->targetQueueID].submit(internalCmd);
        internalCmd = nullptr;
    }

    // update queue status
    for (uint32_t i = 0; i < queues.size(); i++)
    {
        queues[i].retireCommandBuffers();
    }
}

} // namespace vulkan
} // namespace nvrhi
