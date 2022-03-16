#pragma once

#include <donut/app/vr/VrSystem.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <vector>

namespace donut::engine
{
    class CommonRenderPasses;
}

namespace donut::app
{
    class VrSystemNvrhi
    {
    private:
        std::unique_ptr<VrSystem> m_vrSystem;
        nvrhi::DeviceHandle m_Device;
        nvrhi::CommandListHandle m_CommandList;
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::vector<nvrhi::TextureHandle> m_SwapTextures;
        std::vector<nvrhi::FramebufferHandle> m_SwapFramebuffers;
        bool m_IsFirstFrame = true;

        void CreateSwapChainTextures();
    public:
        static VrResult Create(nvrhi::IDevice* device, std::shared_ptr<engine::CommonRenderPasses> commonPasses, VrSystemNvrhi** ppSystem);

        VrApi GetApi() const;

        VrResult AcquirePose() const;
        VrResult Present(
            nvrhi::ITexture* sourceTexture,
            nvrhi::ResourceStates::Enum preTextureState,
            nvrhi::ResourceStates::Enum postTextureState);
        void SetOculusPerfHudMode(int mode) const;
        void Recenter() const;

        dm::int2 GetSwapChainSize() const;
        dm::float4x4 GetProjectionMatrix(int eye, float zNear, float zFar) const;
        dm::float4x4 GetReverseProjectionMatrix(int eye, float zNear) const;
        dm::affine3 GetEyeToOriginTransform(int eye) const;
    };
}