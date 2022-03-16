
#include <donut/engine/MaterialBindingCache.h>
#include <donut/core/log.h>

using namespace donut::engine;

MaterialBindingCache::MaterialBindingCache(
    nvrhi::IDevice* device, 
    nvrhi::ShaderType::Enum shaderType, 
    const std::vector<MaterialResourceBinding>& bindings,
    nvrhi::ISampler* sampler,
    nvrhi::ITexture* fallbackDiffuseTexture,
    nvrhi::ITexture* fallbackOtherTexture)
    : m_Device(device)
    , m_ShaderType(shaderType)
    , m_BindingDesc(bindings)
    , m_Sampler(sampler)
    , m_FallbackDiffuseTexture(fallbackDiffuseTexture)
    , m_FallbackOtherTexture(fallbackOtherTexture)
{
    nvrhi::StageBindingLayoutDesc stageDesc;

    for (const auto& item : bindings)
    {
        nvrhi::BindingLayoutItem layoutItem;
        layoutItem.slot = item.slot;
        layoutItem.registerSpace = item.registerSpace;

        switch (item.resource)
        {
        case MaterialResource::CONSTANT_BUFFER:
            layoutItem.type = nvrhi::ResourceType::ConstantBuffer;
            break;
        case MaterialResource::DIFFUSE_TEXTURE:
        case MaterialResource::SPECULAR_TEXTURE:
        case MaterialResource::NORMALS_TEXTURE:
        case MaterialResource::EMISSIVE_TEXTURE:
            layoutItem.type = nvrhi::ResourceType::Texture_SRV;
            break;
        case MaterialResource::SAMPLER:
            layoutItem.type = nvrhi::ResourceType::Sampler;
            break;
        default:
            log::error("MaterialBindingCache: unknown MaterialResource value (%d)", item.resource);
            return;
        }

        stageDesc.push_back(layoutItem);
    }

    nvrhi::BindingLayoutDesc layoutDesc;
    switch (shaderType)
    {
    case nvrhi::ShaderType::SHADER_VERTEX:
        layoutDesc.VS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_HULL:
        layoutDesc.HS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_DOMAIN:
        layoutDesc.DS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_GEOMETRY:
        layoutDesc.GS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_PIXEL:
        layoutDesc.PS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_ALL_GRAPHICS:
        layoutDesc.ALL = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_COMPUTE:
        layoutDesc.CS = stageDesc;
        break;
    default:
        log::error("MaterialBindingCache: unknown ShaderType value (%d)", shaderType);
        return;
    }

    m_BindingLayout = m_Device->createBindingLayout(layoutDesc);
}

nvrhi::IBindingLayout* donut::engine::MaterialBindingCache::GetLayout() const
{
    return m_BindingLayout;
}

nvrhi::ITexture* TextureOrFallback(const std::shared_ptr<LoadedTexture>& main, nvrhi::ITexture* fallback)
{
    return main && main->texture ? main->texture.Get() : fallback;
}

nvrhi::IBindingSet* donut::engine::MaterialBindingCache::GetMaterialBindingSet(const Material* material)
{
    nvrhi::BindingSetHandle& bindingSet = m_BindingSets[material];

    if (bindingSet)
        return bindingSet;

    bindingSet = CreateMaterialBindingSet(material);

    return bindingSet;
}

void donut::engine::MaterialBindingCache::Clear()
{
    m_BindingSets.clear();
}

nvrhi::BindingSetHandle donut::engine::MaterialBindingCache::CreateMaterialBindingSet(const Material* material)
{
    nvrhi::StageBindingSetDesc stageDesc;

    for (const auto& item : m_BindingDesc)
    {
        nvrhi::BindingSetItem setItem;

        switch (item.resource)
        {
        case MaterialResource::CONSTANT_BUFFER:
            setItem = nvrhi::BindingSetItem::ConstantBuffer(
                item.slot, 
                material->materialConstants, 
                item.registerSpace);
            break;

        case MaterialResource::DIFFUSE_TEXTURE:
            setItem = nvrhi::BindingSetItem::Texture_SRV(
                item.slot,
                TextureOrFallback(material->diffuseTexture, m_FallbackDiffuseTexture),
                nvrhi::Format::UNKNOWN,
                nvrhi::AllSubresources,
                item.registerSpace);
            break;

        case MaterialResource::SPECULAR_TEXTURE:
            setItem = nvrhi::BindingSetItem::Texture_SRV(
                item.slot,
                TextureOrFallback(material->specularTexture, m_FallbackOtherTexture),
                nvrhi::Format::UNKNOWN,
                nvrhi::AllSubresources,
                item.registerSpace);
            break;

        case MaterialResource::NORMALS_TEXTURE:
            setItem = nvrhi::BindingSetItem::Texture_SRV(
                item.slot,
                TextureOrFallback(material->normalsTexture, m_FallbackOtherTexture),
                nvrhi::Format::UNKNOWN,
                nvrhi::AllSubresources,
                item.registerSpace);
            break;

        case MaterialResource::EMISSIVE_TEXTURE:
            setItem = nvrhi::BindingSetItem::Texture_SRV(
                item.slot,
                TextureOrFallback(material->emissiveTexture, m_FallbackOtherTexture),
                nvrhi::Format::UNKNOWN,
                nvrhi::AllSubresources,
                item.registerSpace);
            break;

        case MaterialResource::SAMPLER:
            setItem = nvrhi::BindingSetItem::Sampler(
                item.slot,
                m_Sampler,
                item.registerSpace);
            break;

        default:
            log::error("MaterialBindingCache: unknown MaterialResource value (%d)", item.resource);
            return nullptr;
        }

        stageDesc.push_back(setItem);
    }

    nvrhi::BindingSetDesc bindingSetDesc;
    switch (m_ShaderType)
    {
    case nvrhi::ShaderType::SHADER_VERTEX:
        bindingSetDesc.VS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_HULL:
        bindingSetDesc.HS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_DOMAIN:
        bindingSetDesc.DS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_GEOMETRY:
        bindingSetDesc.GS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_PIXEL:
        bindingSetDesc.PS = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_ALL_GRAPHICS:
        bindingSetDesc.ALL = stageDesc;
        break;
    case nvrhi::ShaderType::SHADER_COMPUTE:
        bindingSetDesc.CS = stageDesc;
        break;
    default:
        log::error("MaterialBindingCache: unknown ShaderType value (%d)", m_ShaderType);
        return nullptr;
    }

    return m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
}
