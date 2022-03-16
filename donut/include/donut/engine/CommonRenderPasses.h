#pragma once

#include <memory>
#include <nvrhi/nvrhi.h>
#include <donut/core/math/math.h>
#include <unordered_map>

namespace donut::engine
{

    class ShaderFactory;

    class CommonRenderPasses
    {
    protected:
        nvrhi::DeviceHandle m_Device;

        std::unordered_map<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle> m_BlitPsoCache;
        std::unordered_map<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle> m_SharpenPsoCache;

        struct BlitBindingKey
        {
            nvrhi::TextureHandle texture;
            uint32_t arraySlice;
            uint32_t mipLevel;

            bool operator==(const BlitBindingKey& other) const { return texture == other.texture && arraySlice == other.arraySlice && mipLevel == other.mipLevel; }
            bool operator!=(const BlitBindingKey& other) const { return !(*this == other); }

            struct Hash
            {
                size_t operator ()(const BlitBindingKey& s) const {
                    return (std::hash<nvrhi::ITexture*>()(s.texture) << 2)
                        ^ (std::hash<uint32_t>()(s.arraySlice) << 1)
                        ^ (std::hash<uint32_t>()(s.mipLevel) << 0);
                }
            };
        };

        std::unordered_map<BlitBindingKey, nvrhi::BindingSetHandle, BlitBindingKey::Hash> m_BlitBindingCache;

        nvrhi::IBindingSet* GetCachedBindingSet(nvrhi::ITexture* texture, uint32_t arraySlice, uint32_t mipLevel);

    public:
        nvrhi::ShaderHandle m_FullscreenVS;
        nvrhi::ShaderHandle m_FullscreenAtOneVS;
        nvrhi::ShaderHandle m_RectVS;
        nvrhi::ShaderHandle m_BlitPS;
        nvrhi::ShaderHandle m_SharpenPS;
        nvrhi::BufferHandle m_BlitCB;

        nvrhi::TextureHandle m_BlackTexture;
        nvrhi::TextureHandle m_GrayTexture;
        nvrhi::TextureHandle m_WhiteTexture;
        nvrhi::TextureHandle m_BlackTexture2DArray;
        nvrhi::TextureHandle m_BlackCubeMapArray;
        nvrhi::TextureHandle m_ZeroDepthStencil2DArray;

        nvrhi::SamplerHandle m_PointClampSampler;
        nvrhi::SamplerHandle m_LinearClampSampler;
        nvrhi::SamplerHandle m_LinearWrapSampler;
        nvrhi::SamplerHandle m_AnisotropicWrapSampler;

        nvrhi::BindingLayoutHandle m_BlitBindingLayout;

    public:
        CommonRenderPasses(nvrhi::DeviceHandle device, std::shared_ptr<ShaderFactory> shaderFactory);

        void ResetBindingCache();
        nvrhi::BindingSetHandle CreateBlitBindingSet(nvrhi::ITexture* source, uint32_t sourceArraySlice, uint32_t sourceMip);

        void BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, nvrhi::IBindingSet* source);
        void BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, dm::box2_arg targetBox, nvrhi::IBindingSet* source, dm::box2_arg sourceBox);

        void BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, nvrhi::ITexture* source, uint32_t sourceArraySlice = 0, uint32_t sourceMip = 0);
        void BlitTexture(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, dm::box2_arg targetBox, nvrhi::ITexture* source, dm::box2_arg sourceBox, uint32_t sourceArraySlice = 0, uint32_t sourceMip = 0);

        void BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, nvrhi::IBindingSet* source);
        void BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, dm::box2_arg targetBox, nvrhi::IBindingSet* source, dm::box2_arg sourceBox);

        void BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, nvrhi::ITexture* source, uint32_t sourceArraySlice = 0, uint32_t sourceMip = 0);
        void BlitSharpen(nvrhi::ICommandList* commandList, nvrhi::IFramebuffer* target, const nvrhi::Viewport& viewport, float sharpenFactor, dm::box2_arg targetBox, nvrhi::ITexture* source, dm::box2_arg sourceBox, uint32_t sourceArraySlice = 0, uint32_t sourceMip = 0);

    };

}