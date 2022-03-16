#pragma once

#include <nvrhi/nvrhi.h>

namespace nvrhi
{
    namespace utils
    {
        BlendState CreateAddBlendState(
            BlendState::BlendValue srcBlend,
            BlendState::BlendValue dstBlend);


        BufferDesc CreateConstantBufferDesc(
            const uint32_t byteSize,
            const char* const debugName,
            const bool isVolatile = true);

        bool CreateBindingSetAndLayout(
            IDevice* device, 
            const BindingSetDesc& bindingSetDesc, 
            BindingLayoutHandle& bindingLayout, 
            BindingSetHandle& bindingSet);

        void ClearColorAttachment(
            ICommandList* commandList,
            IFramebuffer* framebuffer,
            uint32_t attachmentIndex,
            Color color
        );

        void ClearDepthStencilAttachment(
            ICommandList* commandList,
            IFramebuffer* framebuffer,
            float depth,
            uint32_t stencil
        );

        const char* GraphicsAPIToString(GraphicsAPI api);
    }
}
