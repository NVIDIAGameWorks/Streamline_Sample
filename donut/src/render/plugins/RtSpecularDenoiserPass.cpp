#include <donut/render/plugins/RtSpecularDenoiserPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/engine/View.h>

#include <nvrhi/d3d12/d3d12.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

RtSpecularDenoiserPass::RtSpecularDenoiserPass(nvrhi::IDevice* device)
    : m_Device(device)
{
    CreateRtReflectionFilterContext();
}

RtSpecularDenoiserPass::~RtSpecularDenoiserPass()
{
    ReleaseRtReflectionFilterContext();
}

void RtSpecularDenoiserPass::CreateRtReflectionFilterContext()
{
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        GFSDK_RTReflectionFilter_DescriptorHeaps_D3D12 descriptorHeaps;
        nvrhi::d3d12::StaticDescriptorHeap& srvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap();
        descriptorHeaps.CBV_SRV_UAV.BaseIndex = srvHeap.AllocateDescriptors(GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
        descriptorHeaps.CBV_SRV_UAV.NumDescriptors = GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12;
        descriptorHeaps.CBV_SRV_UAV.pDescHeap = srvHeap.GetShaderVisibleHeap();
        m_BaseSrvDescriptor = descriptorHeaps.CBV_SRV_UAV.BaseIndex;
        m_SrvHeap = descriptorHeaps.CBV_SRV_UAV.pDescHeap;
        nvrhi::d3d12::StaticDescriptorHeap& rtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap();
        descriptorHeaps.RTV.BaseIndex = rtvHeap.AllocateDescriptors(GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
        descriptorHeaps.RTV.NumDescriptors = GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_RTV_HEAP_D3D12;
        descriptorHeaps.RTV.pDescHeap = rtvHeap.GetHeap();
        m_BaseRtvDescriptor = descriptorHeaps.RTV.BaseIndex;
        m_RtvHeap = descriptorHeaps.RTV.pDescHeap;

        GFSDK_RTReflectionFilter_Status Status = GFSDK_RTReflectionFilter_CreateContext_D3D12(
            m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device),
            1,
            descriptorHeaps,
            &m_RtReflectionFilterContextD3D12);

        assert(Status == GFSDK_RT_REFLECTION_FILTER_OK);

        return;
    }
}

void RtSpecularDenoiserPass::ReleaseRtReflectionFilterContext()
{
    if (m_RtReflectionFilterContextD3D12)
    {
        m_RtReflectionFilterContextD3D12->Release();
        m_RtReflectionFilterContextD3D12 = nullptr;
    }

    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        if (m_SrvHeap)
        {
            d3d12Device->GetShaderResourceViewDescriptorHeap().ReleaseDescriptors(m_BaseSrvDescriptor, GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
            m_BaseSrvDescriptor = 0;
            m_SrvHeap = nullptr;
        }

        if (m_RtvHeap)
        {
            d3d12Device->GetRenderTargetViewDescriptorHeap().ReleaseDescriptors(m_BaseRtvDescriptor, GFSDK_RT_REFLECTION_FILTER_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
            m_BaseRtvDescriptor = 0;
            m_RtvHeap = nullptr;
        }
    }
}

void RtSpecularDenoiserPass::Render(
    nvrhi::ICommandList* commandList,
    const GFSDK_RTReflectionFilter_Parameters& inParams,
    const ICompositeView& compositeView,
    const ICompositeView& compositeViewPrevious,
    nvrhi::ITexture* inputDepth,
    nvrhi::ITexture* inputNormals,
    nvrhi::ITexture* inputDistance,
    GFSDK_RTReflectionFilter_Channel distanceChannel,
    nvrhi::ITexture* inputRoughness,
    GFSDK_RTReflectionFilter_Channel roughnessChannel,
    nvrhi::ITexture* inputSpecular,
    nvrhi::ITexture* outputTexture,
    GFSDK_RTReflectionFilter_RenderMask renderMask,
    nvrhi::ITexture* inputMotionVectors,
    nvrhi::ITexture* inputPreviousNormals,
    nvrhi::ITexture* inputPreviousDepth)
{
    if (m_Device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
        return;

    commandList->beginMarker("SpecularDenoiser");
    commandList->clearState();

    ID3D12DescriptorHeap* currentSrvHeap = nullptr;
    ID3D12DescriptorHeap* currentRtvHeap = nullptr;
    nvrhi::d3d12::Device* d3d12Device = nullptr;

    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        currentSrvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap().GetShaderVisibleHeap();
        currentRtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap().GetHeap();

        if (currentSrvHeap != m_SrvHeap || currentRtvHeap != m_RtvHeap)
        {
            // If the NVRHI backend has reallocated a descriptor heap, we need to re-initialize HBAO+ to use the new heaps

            ReleaseRtReflectionFilterContext();
            CreateRtReflectionFilterContext();
        }
    }

    // Take a copy of the params block, because it's up to us to populate the camera fields
    GFSDK_RTReflectionFilter_Parameters params = inParams;

    for (uint32_t viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const IView* viewPrevious = compositeViewPrevious.GetChildView(ViewType::PLANAR, viewIndex);

        float4x4 viewMatrix = affineToHomogeneous(view->GetViewMatrix());

        float4x4 projectionMatrix = view->GetProjectionMatrix(false);
        projectionMatrix *= 1.0f / projectionMatrix[2][3];
        projectionMatrix[2][3] = 1.0f;

        params.Camera.ViewProjectionMatrix.Data = GFSDK_RTReflectionFilter_Float4x4(view->GetViewProjectionMatrix().m_data);
        params.Camera.ViewProjectionMatrix.Layout = GFSDK_RT_REFLECTION_FILTER_ROW_MAJOR_ORDER;
        params.Camera.InvViewProjectionMatrix.Data = GFSDK_RTReflectionFilter_Float4x4(view->GetInverseViewProjectionMatrix().m_data);
        params.Camera.InvViewProjectionMatrix.Layout = GFSDK_RT_REFLECTION_FILTER_ROW_MAJOR_ORDER;
        float4x4 clipToPrevClip = viewPrevious->GetViewProjectionMatrix() * view->GetInverseViewProjectionMatrix();
        params.Camera.ClipToPrevClip.Data = GFSDK_RTReflectionFilter_Float4x4(clipToPrevClip.m_data);
        params.Camera.ClipToPrevClip.Layout = GFSDK_RT_REFLECTION_FILTER_ROW_MAJOR_ORDER;
        params.Camera.WorldPositionX = view->GetViewOrigin().x;
        params.Camera.WorldPositionY = view->GetViewOrigin().y;
        params.Camera.WorldPositionZ = view->GetViewOrigin().z;

        nvrhi::ViewportState viewportState = view->GetViewportState();
        assert(viewportState.viewports.size() == 1);

        const nvrhi::Viewport& sourceViewport = viewportState.viewports[0];

        GFSDK_RTReflectionFilter_InputViewport viewport;
        viewport.Enable = true;
        viewport.TopLeftX = static_cast<GFSDK_RT_REFLECTION_FILTER_UINT>(sourceViewport.minX);
        viewport.TopLeftY = static_cast<GFSDK_RT_REFLECTION_FILTER_UINT>(sourceViewport.minY);
        viewport.Width = static_cast<GFSDK_RT_REFLECTION_FILTER_UINT>(sourceViewport.maxX - sourceViewport.minX);
        viewport.Height = static_cast<GFSDK_RT_REFLECTION_FILTER_UINT>(sourceViewport.maxY - sourceViewport.minY);
        viewport.MinDepth = static_cast<GFSDK_RT_REFLECTION_FILTER_FLOAT>(sourceViewport.minZ);
        viewport.MaxDepth = static_cast<GFSDK_RT_REFLECTION_FILTER_FLOAT>(sourceViewport.maxZ);

        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();

        GFSDK_RTReflectionFilter_InputData_D3D12 inputData = {};
        inputData.Depth.DepthTextureType = GFSDK_RT_REFLECTION_FILTER_HARDWARE_DEPTHS;
        inputData.Depth.ProjectionMatrix.Data = GFSDK_RTReflectionFilter_Float4x4(projectionMatrix.m_data);
        inputData.Depth.ProjectionMatrix.Layout = GFSDK_RT_REFLECTION_FILTER_ROW_MAJOR_ORDER;
        inputData.Depth.Viewport = viewport;

        inputData.Depth.SRV.pResource = inputDepth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.Depth.SRV.GpuHandle = inputDepth->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        commandList->endTrackingTextureState(inputDepth, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.Normals.BumpMappedSRV.pResource = inputNormals->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.Normals.BumpMappedSRV.GpuHandle = inputNormals->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        inputData.Normals.NormalWeightMode = GFSDK_RT_REFLECTION_NORMAL_WEIGHT_DISABLE;
        inputData.Normals.WorldToViewMatrix.Data = GFSDK_RTReflectionFilter_Float4x4(viewMatrix.m_data);
        inputData.Normals.WorldToViewMatrix.Layout = GFSDK_RT_REFLECTION_FILTER_ROW_MAJOR_ORDER;
        commandList->endTrackingTextureState(inputNormals, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.HitT.SRV.pResource = inputDistance->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.HitT.SRV.GpuHandle = inputDistance->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        inputData.HitT.Channel = distanceChannel;
        commandList->endTrackingTextureState(inputDistance, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.Roughness.SRV.pResource = inputRoughness->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.Roughness.SRV.GpuHandle = inputRoughness->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        inputData.Roughness.Channel = roughnessChannel;
        commandList->endTrackingTextureState(inputRoughness, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.Reflections.SRV.pResource = inputSpecular->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.Reflections.SRV.GpuHandle = inputSpecular->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        commandList->endTrackingTextureState(inputSpecular, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        if(inputMotionVectors && params.EnableTemporalFilter)
        {
            inputData.Temporal.TemporalMode = GFSDK_RT_REFLECTION_TEMPORAL_ENABLE;
            const nvrhi::TextureDesc& motionVectorsDesc = inputMotionVectors->GetDesc();
            inputData.Temporal.DecodeScaleX = 1.f / (float)motionVectorsDesc.width;
            inputData.Temporal.DecodeScaleY = 1.f / (float)motionVectorsDesc.height;

            inputData.Temporal.MotionVectorSRV.pResource = inputMotionVectors->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            inputData.Temporal.MotionVectorSRV.GpuHandle = inputMotionVectors->getNativeView(
                nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
                nvrhi::Format::UNKNOWN,
                subresources).integer;
            commandList->endTrackingTextureState(inputMotionVectors, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

            inputData.Temporal.PrevNormalSRV.pResource = inputPreviousNormals->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            inputData.Temporal.PrevNormalSRV.GpuHandle = inputPreviousNormals->getNativeView(
                nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
                nvrhi::Format::UNKNOWN,
                subresources).integer;
            commandList->endTrackingTextureState(inputPreviousNormals, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

            inputData.Temporal.PrevDepthSRV.pResource = inputPreviousDepth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            inputData.Temporal.PrevDepthSRV.GpuHandle = inputPreviousDepth->getNativeView(
                nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
                nvrhi::Format::UNKNOWN,
                subresources).integer;
            commandList->endTrackingTextureState(inputPreviousDepth, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);
        }

        GFSDK_RTReflectionFilter_RenderTargetView_D3D12 outputRTV = {};
        outputRTV.pResource = outputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        outputRTV.CpuHandle = static_cast<GFSDK_RT_REFLECTION_FILTER_SIZE_T>(outputTexture->getNativeView(
            nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor,
            nvrhi::Format::UNKNOWN,
            subresources).integer);
        commandList->endTrackingTextureState(outputTexture, subresources, nvrhi::ResourceStates::RENDER_TARGET);

        GFSDK_RTReflectionFilter_Output_D3D12 outputData = {};
        outputData.pRenderTargetView = &outputRTV;

        currentSrvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap().GetShaderVisibleHeap();
        currentRtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap().GetHeap();

        if (currentSrvHeap == m_SrvHeap && currentRtvHeap == m_RtvHeap)
        {
            GFSDK_RTReflectionFilter_Status Status = m_RtReflectionFilterContextD3D12->Filter(
                m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_CommandQueue),
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList),
                inputData,
                params,
                outputData,
                renderMask
            );

            assert(Status == GFSDK_RT_REFLECTION_FILTER_OK);
        }
    }

    commandList->clearState();
    commandList->endMarker();
}
