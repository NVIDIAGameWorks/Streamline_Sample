#include <nvrhi/d3d11/d3d11.h>
#include <nvrhi/common/containers.h>

namespace nvrhi 
{
namespace d3d11 
{

extern const FormatMapping& GetFormatMapping(Format abstractFormat);

void Device::setupStageBindings(PipelineBindingSet* bindingSet, const StageBindingSetDesc& bindings, PipelineBindingSet::StageResourceBindings& target)
{
    for(const BindingSetItem& binding : bindings)
    {
        const uint32_t& slot = binding.slot;

        IResource* pResource = nullptr;

        switch(binding.type)
        {
            case ResourceType::Texture_SRV:
                {
                    const auto texture = static_cast<Texture *>(binding.resourceHandle);

                    assert(target.SRVs[slot] == nullptr);
                    target.SRVs[slot] = getSRVForTexture(texture, binding.format, binding.subresources);

                    target.minSRVSlot = std::min(target.minSRVSlot, slot);
                    target.maxSRVSlot = std::max(target.maxSRVSlot, slot);

                    pResource = texture;
                }

                break;

            case ResourceType::Texture_UAV:
                {
                    const auto texture = static_cast<Texture *>(binding.resourceHandle);

                    target.UAVs[slot] = getUAVForTexture(texture, binding.format, binding.subresources);

                    target.minUAVSlot = std::min(target.minUAVSlot, slot);
                    target.maxUAVSlot = std::max(target.maxUAVSlot, slot);

                    pResource = texture;
            }

                break;

            case ResourceType::Buffer_SRV:
            case ResourceType::StructuredBuffer_SRV:
                {
                    const auto buffer = static_cast<Buffer *>(binding.resourceHandle);

                    assert(target.SRVs[slot] == nullptr);
                    target.SRVs[slot] = getSRVForBuffer(buffer, binding.format, binding.range);

                    target.minSRVSlot = std::min(target.minSRVSlot, slot);
                    target.maxSRVSlot = std::max(target.maxSRVSlot, slot);

                    pResource = buffer;
                }

                break;

            case ResourceType::Buffer_UAV:
            case ResourceType::StructuredBuffer_UAV:
                {
                    const auto buffer = static_cast<Buffer *>(binding.resourceHandle);
                    target.UAVs[slot] = getUAVForBuffer(buffer, binding.format, binding.range);

                    target.minUAVSlot = std::min(target.minUAVSlot, slot);
                    target.maxUAVSlot = std::max(target.maxUAVSlot, slot);

                    pResource = buffer;
                }

                break;

            // DX11 makes no distinction between regular and volatile CBs
            case ResourceType::ConstantBuffer:
            case ResourceType::VolatileConstantBuffer:
                {
                    assert(target.constantBuffers[slot] == nullptr);

                    const auto buffer = static_cast<Buffer *>(binding.resourceHandle);
                    target.constantBuffers[slot] = buffer->resource.Get();

                    target.minConstantBufferSlot = std::min(target.minConstantBufferSlot, slot);
                    target.maxConstantBufferSlot = std::max(target.maxConstantBufferSlot, slot);

                    pResource = buffer;
                }

                break;

            case ResourceType::Sampler:
                {
                    assert(target.samplers[slot] == nullptr);

                    const auto sampler = static_cast<Sampler *>(binding.resourceHandle);
                    target.samplers[slot] = sampler->sampler.Get();

                    target.minSamplerSlot = std::min(target.minSamplerSlot, slot);
                    target.maxSamplerSlot = std::max(target.maxSamplerSlot, slot);

                    pResource = sampler;
                }

                break;
        }

        if (pResource)
        {
            bindingSet->resources.push_back(pResource);
        }
    }
}

BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc)
{
    PipelineBindingLayout* layout = new PipelineBindingLayout();
    layout->desc = desc;
    return BindingLayoutHandle::Create(layout);

    // TODO: verify that none of the layout items have registerSpace != 0
}

BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)
{
    PipelineBindingSet *ret = new PipelineBindingSet(this);
    ret->desc = desc;
    ret->layout = layout;

    setupStageBindings(ret, desc.VS, ret->VS);
    setupStageBindings(ret, desc.HS, ret->HS);
    setupStageBindings(ret, desc.DS, ret->DS);
    setupStageBindings(ret, desc.GS, ret->GS);
    setupStageBindings(ret, desc.PS, ret->PS);
    setupStageBindings(ret, desc.CS, ret->CS);

    // TODO: add support for desc.ALL

    return BindingSetHandle::Create(ret);
}

static ID3D11Buffer *NullCBs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = { nullptr };
static ID3D11ShaderResourceView *NullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
static ID3D11SamplerState *NullSamplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = { nullptr };
static ID3D11UnorderedAccessView *NullUAVs[D3D11_PS_CS_UAV_REGISTER_COUNT] = { 0 };
static UINT NullUAVInitialCounts[D3D11_PS_CS_UAV_REGISTER_COUNT] = { 0 };

bool PipelineBindingSet::StageResourceBindings::IsSupersetOf(const StageResourceBindings& other) const
{
    return minSRVSlot <= other.minSRVSlot && maxSRVSlot >= other.maxSRVSlot
        && minUAVSlot <= other.minUAVSlot && maxUAVSlot >= other.maxUAVSlot
        && minSamplerSlot <= other.minSamplerSlot && maxSamplerSlot >= other.maxSamplerSlot
        && minConstantBufferSlot <= other.minConstantBufferSlot && maxConstantBufferSlot >= other.maxConstantBufferSlot;
}

bool PipelineBindingSet::IsSupersetOf(const PipelineBindingSet& other) const
{
    return VS.IsSupersetOf(other.VS)
        && HS.IsSupersetOf(other.HS)
        && DS.IsSupersetOf(other.DS)
        && GS.IsSupersetOf(other.GS)
        && PS.IsSupersetOf(other.PS);
}

#define D3D11_SET_ARRAY(method, min, max, array) \
        if ((max) >= (min)) \
            context->method(min, (max - min + 1), &array[min])

void Device::prepareToBindGraphicsResourceSets(const BindingSetVector& resourceSets, const static_vector<BindingSetHandle, MaxBindingLayouts>* currentResourceSets, bool updateFramebuffer, BindingSetVector& outSetsToBind)
{
    outSetsToBind = resourceSets;

    if (currentResourceSets)
    {
        BindingSetVector setsToUnbind;
        
        for (const BindingSetHandle& bindingSet : *currentResourceSets)
        {
            setsToUnbind.push_back(bindingSet);
        }

        for (uint32_t i = 0; i < uint32_t(outSetsToBind.size()); i++)
        {
            if (outSetsToBind[i])
                for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                {
                    if (outSetsToBind[i] == setsToUnbind[j])
                    {
                        outSetsToBind[i] = nullptr;
                        setsToUnbind[j] = nullptr;
                        break;
                    }
                }
        }

        if (!updateFramebuffer)
        {
            for (uint32_t i = 0; i < uint32_t(outSetsToBind.size()); i++)
            {
                if (outSetsToBind[i])
                    for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                    {
                        if (setsToUnbind[j] && static_cast<PipelineBindingSet*>(outSetsToBind[i])->IsSupersetOf(*static_cast<PipelineBindingSet*>(setsToUnbind[j])))
                        {
                            setsToUnbind[j] = nullptr;
                        }
                    }
            }
        }

        for (IBindingSet* _set : setsToUnbind)
        {
            if (!_set)
                continue;

            PipelineBindingSet* set = static_cast<PipelineBindingSet*>(_set);

            D3D11_SET_ARRAY(VSSetConstantBuffers, set->VS.minConstantBufferSlot, set->VS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(VSSetShaderResources, set->VS.minSRVSlot, set->VS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(VSSetSamplers, set->VS.minSamplerSlot, set->VS.maxSamplerSlot, NullSamplers);

            D3D11_SET_ARRAY(HSSetConstantBuffers, set->HS.minConstantBufferSlot, set->HS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(HSSetShaderResources, set->HS.minSRVSlot, set->HS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(HSSetSamplers, set->HS.minSamplerSlot, set->HS.maxSamplerSlot, NullSamplers);

            D3D11_SET_ARRAY(DSSetConstantBuffers, set->DS.minConstantBufferSlot, set->DS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(DSSetShaderResources, set->DS.minSRVSlot, set->DS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(DSSetSamplers, set->DS.minSamplerSlot, set->DS.maxSamplerSlot, NullSamplers);

            D3D11_SET_ARRAY(GSSetConstantBuffers, set->GS.minConstantBufferSlot, set->GS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(GSSetShaderResources, set->GS.minSRVSlot, set->GS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(GSSetSamplers, set->GS.minSamplerSlot, set->GS.maxSamplerSlot, NullSamplers);

            D3D11_SET_ARRAY(PSSetConstantBuffers, set->PS.minConstantBufferSlot, set->PS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(PSSetShaderResources, set->PS.minSRVSlot, set->PS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(PSSetSamplers, set->PS.minSamplerSlot, set->PS.maxSamplerSlot, NullSamplers);
        }
    }
}


void Device::bindGraphicsResourceSets(const BindingSetVector& setsToBind)
{
    for(IBindingSet* _set : setsToBind)
    {
        if (!_set)
            continue;

        PipelineBindingSet* set = static_cast<PipelineBindingSet*>(_set);

        D3D11_SET_ARRAY(VSSetConstantBuffers, set->VS.minConstantBufferSlot, set->VS.maxConstantBufferSlot, set->VS.constantBuffers);
        D3D11_SET_ARRAY(VSSetShaderResources, set->VS.minSRVSlot, set->VS.maxSRVSlot, set->VS.SRVs);
        D3D11_SET_ARRAY(VSSetSamplers, set->VS.minSamplerSlot, set->VS.maxSamplerSlot, set->VS.samplers);

        D3D11_SET_ARRAY(HSSetConstantBuffers, set->HS.minConstantBufferSlot, set->HS.maxConstantBufferSlot, set->HS.constantBuffers);
        D3D11_SET_ARRAY(HSSetShaderResources, set->HS.minSRVSlot, set->HS.maxSRVSlot, set->HS.SRVs);
        D3D11_SET_ARRAY(HSSetSamplers, set->HS.minSamplerSlot, set->HS.maxSamplerSlot, set->HS.samplers);

        D3D11_SET_ARRAY(DSSetConstantBuffers, set->DS.minConstantBufferSlot, set->DS.maxConstantBufferSlot, set->DS.constantBuffers);
        D3D11_SET_ARRAY(DSSetShaderResources, set->DS.minSRVSlot, set->DS.maxSRVSlot, set->DS.SRVs);
        D3D11_SET_ARRAY(DSSetSamplers, set->DS.minSamplerSlot, set->DS.maxSamplerSlot, set->DS.samplers);

        D3D11_SET_ARRAY(GSSetConstantBuffers, set->GS.minConstantBufferSlot, set->GS.maxConstantBufferSlot, set->GS.constantBuffers);
        D3D11_SET_ARRAY(GSSetShaderResources, set->GS.minSRVSlot, set->GS.maxSRVSlot, set->GS.SRVs);
        D3D11_SET_ARRAY(GSSetSamplers, set->GS.minSamplerSlot, set->GS.maxSamplerSlot, set->GS.samplers);

        D3D11_SET_ARRAY(PSSetConstantBuffers, set->PS.minConstantBufferSlot, set->PS.maxConstantBufferSlot, set->PS.constantBuffers);
        D3D11_SET_ARRAY(PSSetShaderResources, set->PS.minSRVSlot, set->PS.maxSRVSlot, set->PS.SRVs);
        D3D11_SET_ARRAY(PSSetSamplers, set->PS.minSamplerSlot, set->PS.maxSamplerSlot, set->PS.samplers);
    }
}

void Device::bindComputeResourceSets(const BindingSetVector& resourceSets, const static_vector<BindingSetHandle, MaxBindingLayouts>* currentResourceSets)
{
    BindingSetVector setsToBind = resourceSets;

    if (currentResourceSets)
    {
        BindingSetVector setsToUnbind;

        for (const BindingSetHandle& bindingSet : *currentResourceSets)
        {
            setsToUnbind.push_back(bindingSet);
        }

        for (uint32_t i = 0; i < uint32_t(setsToBind.size()); i++)
        {
            if (setsToBind[i])
                for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                {
                    if (setsToBind[i] == setsToUnbind[j])
                    {
                        setsToBind[i] = nullptr;
                        setsToUnbind[j] = nullptr;
                        break;
                    }
                }
        }
        
        for (uint32_t i = 0; i < uint32_t(setsToBind.size()); i++)
        {
            if (setsToBind[i])
                for (uint32_t j = 0; j < uint32_t(setsToUnbind.size()); j++)
                {
                    PipelineBindingSet* setToBind = static_cast<PipelineBindingSet*>(setsToBind[j]);
                    PipelineBindingSet* setToUnbind = static_cast<PipelineBindingSet*>(setsToUnbind[j]);

                    if (setToUnbind && setToBind->IsSupersetOf(*setToUnbind) && setToUnbind->CS.maxUAVSlot < setToUnbind->CS.minUAVSlot)
                    {
                        setsToUnbind[j] = nullptr;
                    }
                }
        }

        for (IBindingSet* _set : setsToUnbind)
        {
            if (!_set)
                continue;

            PipelineBindingSet* set = static_cast<PipelineBindingSet*>(_set);

            D3D11_SET_ARRAY(CSSetConstantBuffers, set->CS.minConstantBufferSlot, set->CS.maxConstantBufferSlot, NullCBs);
            D3D11_SET_ARRAY(CSSetShaderResources, set->CS.minSRVSlot, set->CS.maxSRVSlot, NullSRVs);
            D3D11_SET_ARRAY(CSSetSamplers, set->CS.minSamplerSlot, set->CS.maxSamplerSlot, NullSamplers);

            if (set->CS.maxUAVSlot >= set->CS.minUAVSlot)
            {
                context->CSSetUnorderedAccessViews(set->CS.minUAVSlot,
                    set->CS.maxUAVSlot - set->CS.minUAVSlot + 1,
                    NullUAVs,
                    NullUAVInitialCounts);
            }
        }
    }

    for(IBindingSet* _set : resourceSets)
    {
        PipelineBindingSet* set = static_cast<PipelineBindingSet*>(_set);

        D3D11_SET_ARRAY(CSSetConstantBuffers, set->CS.minConstantBufferSlot, set->CS.maxConstantBufferSlot, set->CS.constantBuffers);
        D3D11_SET_ARRAY(CSSetShaderResources, set->CS.minSRVSlot, set->CS.maxSRVSlot, set->CS.SRVs);
        D3D11_SET_ARRAY(CSSetSamplers, set->CS.minSamplerSlot, set->CS.maxSamplerSlot, set->CS.samplers);

        if (set->CS.maxUAVSlot >= set->CS.minUAVSlot)
        {
            context->CSSetUnorderedAccessViews(set->CS.minUAVSlot,
                set->CS.maxUAVSlot - set->CS.minUAVSlot + 1,
                &set->CS.UAVs[set->CS.minUAVSlot],
                NullUAVInitialCounts);
        }
    }
}

#undef D3D11_SET_ARRAY

} // namespace d3d11
} // namespace nvrhi
