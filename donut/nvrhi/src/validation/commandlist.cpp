/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <iterator>

#include <nvrhi/validation/validation.h>

namespace nvrhi 
{
namespace validation
{

    CommandListWrapper::CommandListWrapper(DeviceWrapper* device, ICommandList* commandList, bool isImmediate)
        : m_Device(device)
        , m_CommandList(commandList)
        , m_MessageCallback(device->getMessageCallback())
        , m_IsImmediate(isImmediate)
    {
    }

    void CommandListWrapper::message(MessageSeverity severity, const char* messageText, const char* file /*= nullptr*/, int line /*= 0*/)
    {
        m_MessageCallback->message(severity, messageText, file, line);
    }

    static const char* CommandListStateToString(CommandListState state)
    {
        switch (state)
        {
        case nvrhi::validation::CommandListState::INITIAL:
            return "INITIAL";
        case nvrhi::validation::CommandListState::OPEN:
            return "OPEN";
        case nvrhi::validation::CommandListState::CLOSED:
            return "CLOSED";
        default:
            return "<INVALID>";
        }
    }

    bool CommandListWrapper::requireOpenState()
    {
        if (m_State == CommandListState::OPEN)
            return true;

        char buf[256];
        const char* actualState = CommandListStateToString(m_State);
        snprintf(buf, std::size(buf), "A command list must be opened before any rendering commands can be executed. Actual state: %s", actualState);
        message(MessageSeverity::Error, buf);

        return false;
    }

    bool CommandListWrapper::requireExecuteState()
    {
        switch (m_State)
        {
        case nvrhi::validation::CommandListState::INITIAL:
            message(MessageSeverity::Error, "Cannot execute a command list before it is opened and then closed");
            return false;
        case nvrhi::validation::CommandListState::OPEN:
            message(MessageSeverity::Error, "Cannot execute a command list before it is closed");
            return false;
        default:;
        }

        m_State = CommandListState::INITIAL;
        return true;
    }

    Object CommandListWrapper::getNativeObject(ObjectType objectType)
    {
        return m_CommandList->getNativeObject(objectType);
    }

    void CommandListWrapper::open()
    {
        switch (m_State)
        {
        case nvrhi::validation::CommandListState::OPEN:
            message(MessageSeverity::Error, "Cannot open a command list that is already open");
            return;
        case nvrhi::validation::CommandListState::CLOSED:
            if (m_IsImmediate)
            {
                message(MessageSeverity::Error, "An immediate command list cannot be abandoned and must be executed before it is re-opened");
                return;
            }
            else
            {
                message(MessageSeverity::Warning, "A command list should be executed before it is reopened");
                break;
            }
        default:;
        }

        if (m_IsImmediate)
        {
            if (++m_Device->m_NumOpenImmediateCommandLists > 1)
            {
                message(MessageSeverity::Error, "Two or more immediate command lists cannot be open at the same time");
                --m_Device->m_NumOpenImmediateCommandLists;
                return;
            }
        }

        m_CommandList->open();

        m_State = CommandListState::OPEN;
        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
    }

    void CommandListWrapper::close()
    {
        switch (m_State)
        {
        case nvrhi::validation::CommandListState::INITIAL:
            message(MessageSeverity::Error, "Cannot close a command list before it is opened");
            return;
        case nvrhi::validation::CommandListState::CLOSED:
            message(MessageSeverity::Error, "Cannot close a command list that is already closed");
            return;
        default:;
        }

        if (m_IsImmediate)
        {
            --m_Device->m_NumOpenImmediateCommandLists;
        }

        m_CommandList->close();

        m_State = CommandListState::CLOSED;
        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;
    }

    void CommandListWrapper::clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor)
    {
        if (!requireOpenState())
            return;

        m_CommandList->clearTextureFloat(t, subresources, clearColor);
    }

    void CommandListWrapper::clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!requireOpenState())
            return;

        m_CommandList->clearDepthStencilTexture(t, subresources, clearDepth, depth, clearStencil, stencil);
    }

    void CommandListWrapper::clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        if (!requireOpenState())
            return;

        m_CommandList->clearTextureUInt(t, subresources, clearColor);
    }

    void CommandListWrapper::copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyTexture(dest, destSlice, src, srcSlice);
    }

    void CommandListWrapper::writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        if (!requireOpenState())
            return;

        if (dest->GetDesc().height > 1 && rowPitch == 0)
        {
            message(MessageSeverity::Error, "writeTexture: rowPitch is 0 but dest has multiple rows");
        }

        m_CommandList->writeTexture(dest, arraySlice, mipLevel, data, rowPitch, depthPitch);
    }

    void CommandListWrapper::resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources)
    {
        bool anyErrors = false;

        if (!dest)
        {
            message(MessageSeverity::Error, "resolveTexture: dest is NULL");
            anyErrors = true;
        }

        if (!src)
        {
            message(MessageSeverity::Error, "resolveTexture: src is NULL");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        const TextureDesc& dstDesc = dest->GetDesc();
        const TextureDesc& srcDesc = src->GetDesc();

        TextureSubresourceSet dstSR = dstSubresources.resolve(dstDesc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(srcDesc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
        {
            message(MessageSeverity::Error, "resolveTexture: source and destination subresource sets must resolve to sets of the same size");
            anyErrors = true;
        }

        if (dstDesc.width >> dstSR.baseMipLevel != srcDesc.width >> srcSR.baseMipLevel || dstDesc.height >> dstSR.baseMipLevel != srcDesc.height >> srcSR.baseMipLevel)
        {
            message(MessageSeverity::Error, "resolveTexture: referenced mip levels of source and destination textures must have the same dimensions");
            anyErrors = true;
        }

        if (dstDesc.sampleCount != 1)
        {
            message(MessageSeverity::Error, "resolveTexture: destination texture must not be multi-sampled");
            anyErrors = true;
        }

        if (srcDesc.sampleCount <= 1)
        {
            message(MessageSeverity::Error, "resolveTexture: source texture must be multi-sampled");
            anyErrors = true;
        }

        if (srcDesc.format != dstDesc.format)
        {
            message(MessageSeverity::Error, "resolveTexture: source and destination textures must have the same format");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        m_CommandList->resolveTexture(dest, dstSubresources, src, srcSubresources);
    }

    void CommandListWrapper::writeBuffer(IBuffer* b, const void* data, size_t dataSize, size_t destOffsetBytes)
    {
        if (!requireOpenState())
            return;

        if (dataSize + destOffsetBytes > b->GetDesc().byteSize)
        {
            message(MessageSeverity::Error, "writeBuffer: dataSize + destOffsetBytes is greater than the buffer size");
            return;
        }

        if (destOffsetBytes > 0 && b->GetDesc().isVolatile)
        {
            message(MessageSeverity::Error, "writeBuffer: cannot write into volatile buffers with an offset");
            return;
        }

        if (dataSize > 0xFFFF && b->GetDesc().isVolatile)
        {
            message(MessageSeverity::Error, "writeBuffer: cannot write more than 65535 bytes into volatile buffers");
            return;
        }

        m_CommandList->writeBuffer(b, data, dataSize, destOffsetBytes);
    }

    void CommandListWrapper::clearBufferUInt(IBuffer* b, uint32_t clearValue)
    {
        if (!requireOpenState())
            return;

        m_CommandList->clearBufferUInt(b, clearValue);
    }

    void CommandListWrapper::copyBuffer(IBuffer* dest, uint32_t destOffsetBytes, IBuffer* src, uint32_t srcOffsetBytes, size_t dataSizeBytes)
    {
        if (!requireOpenState())
            return;

        m_CommandList->copyBuffer(dest, destOffsetBytes, src, srcOffsetBytes, dataSizeBytes);
    }

    bool ValidateBindingSetsAgainstLayouts(IMessageCallback* messageCallback, const static_vector<BindingLayoutHandle, MaxBindingLayouts>& layouts, const static_vector<IBindingSet*, MaxBindingLayouts>& sets)
    {
        char messageBuffer[256];

        if (layouts.size() != sets.size())
        {
            snprintf(messageBuffer, std::size(messageBuffer), "Number of binding sets provided (%d) does not match the number of binding layouts in the pipeline (%d)",
                int(sets.size()), int(layouts.size()));

            messageCallback->message(MessageSeverity::Error, messageBuffer);
            return false;
        }

        bool anyErrors = false;

        for (int index = 0; index < int(layouts.size()); index++)
        {
            if (sets[index] == nullptr)
            {
                snprintf(messageBuffer, std::size(messageBuffer), "Binding set in slot %d is NULL", index);
                messageCallback->message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
                continue;
            }

            if (sets[index]->GetLayout() != layouts[index])
            {
                snprintf(messageBuffer, std::size(messageBuffer), "Binding set in slot %d does not match the layout in pipeline slot %d", index, index);
                messageCallback->message(MessageSeverity::Error, messageBuffer);
                anyErrors = true;
            }
        }

        return !anyErrors;
    }

    void CommandListWrapper::setGraphicsState(const GraphicsState& state)
    {
        if (!requireOpenState())
            return;

        bool anyErrors = false;
        if (!state.pipeline)
        {
            message(MessageSeverity::Error, "GraphicsState::pipeline is NULL");
            anyErrors = true;
        }

        if (!state.framebuffer)
        {
            message(MessageSeverity::Error, "GraphicsState::framebuffer is NULL");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        if (!ValidateBindingSetsAgainstLayouts(m_MessageCallback, state.pipeline->GetDesc().bindingLayouts, state.bindings))
            anyErrors = true;

        if (state.framebuffer->GetFramebufferInfo() != state.pipeline->GetFramebufferInfo())
        {
            message(MessageSeverity::Error, "The framebuffer used in the draw call does not match the framebuffer used to create the pipeline.\n"
                "Width, height, and formats of the framebuffers must match.");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        m_CommandList->setGraphicsState(state);

        m_GraphicsStateSet = true;
        m_ComputeStateSet = false;
        m_CurrentGraphicsState = state;
    }

    void CommandListWrapper::draw(const DrawArguments& args)
    {
        if (!requireOpenState())
            return;

        if (!m_GraphicsStateSet)
        {
            message(MessageSeverity::Error, "Graphics state is not set before a draw call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        m_CommandList->draw(args);
    }

    void CommandListWrapper::drawIndexed(const DrawArguments& args)
    {
        if (!requireOpenState())
            return;

        if (!m_GraphicsStateSet)
        {
            message(MessageSeverity::Error, "Graphics state is not set before a drawIndexed call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (m_CurrentGraphicsState.indexBuffer.handle == nullptr)
        {
            message(MessageSeverity::Error, "Index buffer is not set before a drawIndexed call");
            return;
        }

        m_CommandList->drawIndexed(args);
    }

    void CommandListWrapper::drawIndirect(uint32_t offsetBytes)
    {
        if (!requireOpenState())
            return;

        if (!m_GraphicsStateSet)
        {
            message(MessageSeverity::Error, "Graphics state is not set before a drawIndirect call.\n"
                "Note that setting compute state invalidates the graphics state.");
            return;
        }

        if (m_CurrentGraphicsState.indirectParams)
        {
            message(MessageSeverity::Error, "Indirect params buffer is not set before a drawIndirect call.");
            return;
        }

        m_CommandList->drawIndirect(offsetBytes);
    }

    void CommandListWrapper::setComputeState(const ComputeState& state)
    {
        if (!requireOpenState())
            return;

        bool anyErrors = false;
        if (!state.pipeline)
        {
            message(MessageSeverity::Error, "ComputeState::pipeline is NULL");
            anyErrors = true;
        }

        if (anyErrors)
            return;

        if (!ValidateBindingSetsAgainstLayouts(m_MessageCallback, state.pipeline->GetDesc().bindingLayouts, state.bindings))
            anyErrors = true;

        if (anyErrors)
            return;

        m_CommandList->setComputeState(state);

        m_ComputeStateSet = true;
        m_GraphicsStateSet = false;
        m_CurrentComputeState = state;
    }

    void CommandListWrapper::dispatch(uint32_t groupsX, uint32_t groupsY /*= 1*/, uint32_t groupsZ /*= 1*/)
    {
        if (!requireOpenState())
            return;

        if (!m_ComputeStateSet)
        {
            message(MessageSeverity::Error, "Compute state is not set before a dispatch call.\n"
                "Note that setting graphics state invalidates the compute state.");
            return;
        }

        m_CommandList->dispatch(groupsX, groupsY, groupsZ);
    }

    void CommandListWrapper::dispatchIndirect(uint32_t offsetBytes)
    {
        if (!requireOpenState())
            return;

        if (!m_ComputeStateSet)
        {
            message(MessageSeverity::Error, "Compute state is not set before a dispatchIndirect call.\n"
                "Note that setting graphics state invalidates the compute state.");
            return;
        }

        if (!m_CurrentComputeState.indirectParams)
        {
            message(MessageSeverity::Error, "Indirect params buffer is not set before a dispatchIndirect call.");
            return;
        }

        m_CommandList->dispatchIndirect(offsetBytes);
    }


    void CommandListWrapper::beginTimerQuery(ITimerQuery* query)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTimerQuery(query);
    }

    void CommandListWrapper::endTimerQuery(ITimerQuery* query)
    {
        if (!requireOpenState())
            return;

        m_CommandList->endTimerQuery(query);
    }

    void CommandListWrapper::beginMarker(const char *name)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginMarker(name);
    }

    void CommandListWrapper::endMarker()
    {
        if (!requireOpenState())
            return;

        m_CommandList->endMarker();
    }

    void CommandListWrapper::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setEnableUavBarriersForTexture(texture, enableBarriers);
    }

    void CommandListWrapper::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setEnableUavBarriersForBuffer(buffer, enableBarriers);
    }

    void CommandListWrapper::beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTrackingTextureState(texture, subresources, stateBits);
    }

    void CommandListWrapper::beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits)
    {
        if (!requireOpenState())
            return;

        m_CommandList->beginTrackingBufferState(buffer, stateBits);
    }

    void CommandListWrapper::endTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent /*= false*/)
    {
        if (!requireOpenState())
            return;

        m_CommandList->endTrackingTextureState(texture, subresources, stateBits, permanent);
    }

    void CommandListWrapper::endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent /*= false*/)
    {
        if (!requireOpenState())
            return;

        m_CommandList->endTrackingBufferState(buffer, stateBits, permanent);
    }

    ResourceStates::Enum CommandListWrapper::getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)
    {
        if (!requireOpenState())
            return ResourceStates::COMMON;

        return m_CommandList->getTextureSubresourceState(texture, arraySlice, mipLevel);
    }

    ResourceStates::Enum CommandListWrapper::getBufferState(IBuffer* buffer)
    {
        if (!requireOpenState())
            return ResourceStates::COMMON;

        return m_CommandList->getBufferState(buffer);
    }

    void CommandListWrapper::clearState()
    {
        if (!requireOpenState())
            return;

        m_GraphicsStateSet = false;
        m_ComputeStateSet = false;

        m_CommandList->clearState();
    }

    IDevice* CommandListWrapper::getDevice()
    {
        return m_Device;
    }

    void CommandListWrapper::setRayTracingState(const rt::State& state)
    {
        if (!requireOpenState())
            return;

        m_CommandList->setRayTracingState(state);
    }

    void CommandListWrapper::dispatchRays(const rt::DispatchRaysArguments& args)
    {
        if (!requireOpenState())
            return;

        m_CommandList->dispatchRays(args);
    }

    void CommandListWrapper::buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::BottomLevelAccelStructDesc& desc)
    {
        if (!requireOpenState())
            return;

        m_CommandList->buildBottomLevelAccelStruct(as, desc);
    }

    void CommandListWrapper::buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::TopLevelAccelStructDesc& desc)
    {
        if (!requireOpenState())
            return;

        m_CommandList->buildTopLevelAccelStruct(as, desc);
    }


} // namespace validation
} // namespace nvrhi
