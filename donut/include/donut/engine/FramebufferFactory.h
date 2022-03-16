#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>
#include <unordered_map>

namespace donut::engine
{
    class IView;

    class FramebufferFactory
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::unordered_map<nvrhi::TextureSubresourceSet, nvrhi::FramebufferHandle> m_FramebufferCache;

    public:
        FramebufferFactory(nvrhi::IDevice* device) : m_Device(device) {}

        std::vector<nvrhi::TextureHandle> RenderTargets;
        nvrhi::TextureHandle DepthTarget;

        nvrhi::IFramebuffer* GetFramebuffer(const nvrhi::TextureSubresourceSet& subresources);
        nvrhi::IFramebuffer* GetFramebuffer(const IView& view);
    };
}