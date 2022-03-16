#include <nvrhi/vulkan.h>


namespace nvrhi 
{
namespace vulkan
{

ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
{
    vk::Result res;

    assert(desc.CS);

    if (!context.pipelineCache)
    {
        auto pipelineInfo = vk::PipelineCacheCreateInfo();
        res = context.device.createPipelineCache(&pipelineInfo,
                                                 context.allocationCallbacks,
                                                 &context.pipelineCache);
        CHECK_VK_FAIL(res);
        nameVKObject(context.pipelineCache, (std::string("pipelineCache for: ") + std::string(!desc.CS->GetDesc().debugName.empty()? desc.CS->GetDesc().debugName : "(?)")).c_str());
    }

    ComputePipeline *pso = new (nvrhi::HeapAlloc) ComputePipeline(this);
    pso->desc = desc;

    for (const BindingLayoutHandle& _layout : desc.bindingLayouts)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout.Get());
        pso->pipelineBindingLayouts.push_back(layout);
    }

    BindingVector<vk::DescriptorSetLayout> descriptorSetLayouts;
    for (const BindingLayoutHandle& _layout : desc.bindingLayouts)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout.Get());
        descriptorSetLayouts.push_back(layout->descriptorSetLayout);
    }

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo()
                                .setSetLayoutCount(uint32_t(descriptorSetLayouts.size()))
                                .setPSetLayouts(descriptorSetLayouts.data())
                                .setPushConstantRangeCount(0);

    res = context.device.createPipelineLayout(&pipelineLayoutInfo,
                                              context.allocationCallbacks,
                                              &pso->pipelineLayout);

    auto shaderStageInfo = vk::PipelineShaderStageCreateInfo()
                            .setStage(vk::ShaderStageFlagBits::eCompute)
                            .setModule(static_cast<Shader*>(pso->desc.CS.Get())->shaderModule)
                            .setPName(static_cast<Shader*>(pso->desc.CS.Get())->entryName.c_str());

    auto pipelineInfo = vk::ComputePipelineCreateInfo()
                            .setStage(shaderStageInfo)
                            .setLayout(pso->pipelineLayout);

    res = context.device.createComputePipelines(context.pipelineCache,
                                                1, &pipelineInfo,
                                                context.allocationCallbacks,
                                                &pso->pipeline);
    nameVKObject(pso->pipeline, (std::string("computePipeline for: ") + std::string(!desc.CS->GetDesc().debugName.empty()? desc.CS->GetDesc().debugName : "(?)")).c_str());

    CHECK_VK_FAIL(res);

    return ComputePipelineHandle::Create(pso);
}

unsigned long ComputePipeline::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyComputePipeline(this); 
    return result; 
}

void Device::destroyComputePipeline(IComputePipeline* _pso)
{
    ComputePipeline* pso = static_cast<ComputePipeline*>(_pso);

    if (pso->pipeline)
    {
        context.device.destroyPipeline(pso->pipeline, context.allocationCallbacks);
        pso->pipeline = nullptr;
    }

    if (pso->pipelineLayout)
    {
        context.device.destroyPipelineLayout(pso->pipelineLayout, context.allocationCallbacks);
    }

    heapDelete(pso);
}

void Device::bindComputePipeline(TrackedCommandBuffer *cmd,
                                                  BarrierTracker& barrierTracker,
                                                  ComputePipeline *pso,
                                                  const BindingSetVector& bindingSets)
{
    for (size_t i = 0; i < bindingSets.size() && i < pso->desc.bindingLayouts.size(); i++)
    {
        PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(pso->pipelineBindingLayouts[i].Get());

        trackResourcesAndBarriers(cmd,
                                  barrierTracker,
                                  layout->bindingMap_CS,
                                  static_cast<ResourceBindingSet*>(bindingSets[i])->desc.CS,
                                  vk::PipelineStageFlagBits::eComputeShader);
    }

    cmd->bindPSO(vk::PipelineBindPoint::eCompute, pso->pipeline);

    BindingVector<vk::DescriptorSet> descriptorSets;
    for(const auto& b : bindingSets)
    {
        descriptorSets.push_back(static_cast<ResourceBindingSet*>(b)->descriptorSet);
    }

    cmd->cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pso->pipelineLayout,
                                   0, uint32_t(descriptorSets.size()), descriptorSets.data(),
                                   0, nullptr);
}

void Device::setComputeState(const ComputeState& state)
{
    BarrierTracker barrierTracker;
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Compute);
    assert(cmd);

    bindComputePipeline(cmd, barrierTracker, static_cast<ComputePipeline*>(state.pipeline), state.bindings);

    if (state.indirectParams)
    {
        Buffer* indirectParams = static_cast<Buffer*>(state.indirectParams);

        // include the indirect params buffer in the barrier tracker state
        barrierTracker.update(indirectParams,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::AccessFlagBits::eIndirectCommandRead);
        cmd->markRead(indirectParams);
    }

    m_CurrentDispatchIndirectBuffer = state.indirectParams;

    barrierTracker.execute(cmd);

}

void Device::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Compute);
    assert(cmd);

    cmd->cmdBuf.dispatch(groupsX, groupsY, groupsZ);
}

void Device::dispatchIndirect(uint32_t offsetBytes)
{
    TrackedCommandBuffer *cmd = getCmdBuf(QueueID::Compute);
    assert(cmd);

    Buffer* indirectParams = static_cast<Buffer*>(m_CurrentDispatchIndirectBuffer.Get());
    assert(indirectParams);

    cmd->cmdBuf.dispatchIndirect(indirectParams->buffer, offsetBytes);
}

} // namespace vulkan
} // namespace nvrhi
