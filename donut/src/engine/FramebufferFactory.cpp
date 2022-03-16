#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/View.h>

using namespace donut::engine;

nvrhi::IFramebuffer* FramebufferFactory::GetFramebuffer(const nvrhi::TextureSubresourceSet& subresources)
{
    nvrhi::FramebufferHandle& item = m_FramebufferCache[subresources];

    if (!item)
    {
        nvrhi::FramebufferDesc desc;
        for (auto renderTarget : RenderTargets)
            desc.addColorAttachment(renderTarget, subresources);

        if (DepthTarget)
            desc.setDepthAttachment(DepthTarget, subresources);

        item = m_Device->createFramebuffer(desc);
    }
    
    return item;
}

nvrhi::IFramebuffer* FramebufferFactory::GetFramebuffer(const IView& view)
{
    return GetFramebuffer(view.GetSubresources());
}
