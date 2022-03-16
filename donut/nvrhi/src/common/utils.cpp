
#include <nvrhi/utils.h>

namespace nvrhi
{
    namespace utils
    {
        BlendState CreateAddBlendState(
            BlendState::BlendValue srcBlend,
            BlendState::BlendValue dstBlend)
        {
            BlendState blendState;
            blendState.alphaToCoverage = false;
            blendState.blendEnable[0] = true;
            blendState.blendOp[0] = BlendState::BLEND_OP_ADD;
            blendState.srcBlend[0] = srcBlend;
            blendState.destBlend[0] = dstBlend;
            blendState.srcBlendAlpha[0] = BlendState::BLEND_ZERO;
            blendState.destBlendAlpha[0] = BlendState::BLEND_ONE;
            return blendState;
        }

        BufferDesc CreateConstantBufferDesc(
            const uint32_t byteSize,
            const char* const debugName,
            const bool isVolatile)
        {
            BufferDesc constantBufferDesc;
            constantBufferDesc.byteSize = byteSize;
            constantBufferDesc.debugName = debugName;
            constantBufferDesc.isConstantBuffer = true;
            constantBufferDesc.isVolatile = isVolatile;
            return constantBufferDesc;
        }

        bool CreateBindingSetAndLayout(
            nvrhi::IDevice* device,
            const nvrhi::BindingSetDesc& bindingSetDesc,
            nvrhi::BindingLayoutHandle& bindingLayout,
            nvrhi::BindingSetHandle& bindingSet)
        {
            auto convertSetToLayout = [](const StageBindingSetDesc& setDesc, StageBindingLayoutDesc& layoutDesc)
            {
                for (auto& item : setDesc)
                {
                    BindingLayoutItem layoutItem;
                    layoutItem.slot = item.slot;
                    layoutItem.type = item.type;
                    layoutItem.registerSpace = item.registerSpace;
                    layoutDesc.push_back(layoutItem);
                }
            };

            if (!bindingLayout)
            {
                nvrhi::BindingLayoutDesc bindingLayoutDesc;
                convertSetToLayout(bindingSetDesc.VS, bindingLayoutDesc.VS);
                convertSetToLayout(bindingSetDesc.HS, bindingLayoutDesc.HS);
                convertSetToLayout(bindingSetDesc.DS, bindingLayoutDesc.DS);
                convertSetToLayout(bindingSetDesc.GS, bindingLayoutDesc.GS);
                convertSetToLayout(bindingSetDesc.PS, bindingLayoutDesc.PS);
                convertSetToLayout(bindingSetDesc.CS, bindingLayoutDesc.CS);
                convertSetToLayout(bindingSetDesc.ALL, bindingLayoutDesc.ALL);

                bindingLayout = device->createBindingLayout(bindingLayoutDesc);

                if (!bindingLayout)
                    return false;
            }

            if (!bindingSet)
            {
                bindingSet = device->createBindingSet(bindingSetDesc, bindingLayout);

                if (!bindingSet)
                    return false;
            }

            return true;
        }

        void ClearColorAttachment(ICommandList* commandList, IFramebuffer* framebuffer, uint32_t attachmentIndex, Color color)
        {
            const FramebufferAttachment& att = framebuffer->GetDesc().colorAttachments[attachmentIndex];
            if (att.texture)
            {
                commandList->clearTextureFloat(att.texture, att.subresources, color);
            }
        }

        void ClearDepthStencilAttachment(ICommandList* commandList, IFramebuffer* framebuffer, float depth, uint32_t stencil)
        {
            const FramebufferAttachment& att = framebuffer->GetDesc().depthAttachment;
            if (att.texture)
            {
                commandList->clearTextureFloat(att.texture, att.subresources, Color(depth, float(stencil), 0.f, 0.f));
            }
        }

        const char* GraphicsAPIToString(GraphicsAPI api)
        {
            switch (api)
            {
            case nvrhi::GraphicsAPI::D3D11:
                return "D3D11";
            case nvrhi::GraphicsAPI::D3D12:
                return "D3D12";
            case nvrhi::GraphicsAPI::VULKAN:
                return "Vulkan";
            default:
                return "<UNKNOWN>";
            }
        }
    }
}