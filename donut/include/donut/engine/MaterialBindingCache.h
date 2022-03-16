#pragma once

#include <donut/engine/SceneTypes.h>
#include <nvrhi/nvrhi.h>
#include <unordered_map>
#include <memory>

namespace donut::engine
{
    enum class MaterialResource
    {
        CONSTANT_BUFFER,
        DIFFUSE_TEXTURE,
        SPECULAR_TEXTURE,
        NORMALS_TEXTURE,
        EMISSIVE_TEXTURE,
        SAMPLER
    };

    struct MaterialResourceBinding
    {
        MaterialResource resource;
        uint32_t slot; // type depends on resource
        uint32_t registerSpace;
    };

    class MaterialBindingCache
    {
    private:
        nvrhi::DeviceHandle m_Device;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        std::unordered_map<const Material*, nvrhi::BindingSetHandle> m_BindingSets;
        nvrhi::ShaderType::Enum m_ShaderType;
        std::vector<MaterialResourceBinding> m_BindingDesc;
        nvrhi::TextureHandle m_FallbackDiffuseTexture;
        nvrhi::TextureHandle m_FallbackOtherTexture;
        nvrhi::SamplerHandle m_Sampler;

        nvrhi::BindingSetHandle CreateMaterialBindingSet(const Material* material);

    public:
        MaterialBindingCache(
            nvrhi::IDevice* device, 
            nvrhi::ShaderType::Enum shaderType, 
            const std::vector<MaterialResourceBinding>& bindings,
            nvrhi::ISampler* sampler,
            nvrhi::ITexture* fallbackDiffuseTexture,
            nvrhi::ITexture* fallbackOtherTexture);

        nvrhi::IBindingLayout* GetLayout() const;
        nvrhi::IBindingSet* GetMaterialBindingSet(const Material* material);
        void Clear();
    };
}