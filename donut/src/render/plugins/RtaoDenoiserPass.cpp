
#include <donut/render/plugins/RtaoDenoiserPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/engine/View.h>
#include "GFSDK_RTAO.h"

#include <nvrhi/d3d12/d3d12.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

RtaoDenoiserPass::RtaoDenoiserPass(nvrhi::IDevice* device)
    : m_Device(device)
{
    CreateRtaoContext();
}

RtaoDenoiserPass::~RtaoDenoiserPass()
{
    ReleaseRtaoContext();
}

void RtaoDenoiserPass::CreateRtaoContext()
{
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        GFSDK_RTAO_DescriptorHeaps_D3D12 descriptorHeaps;
        nvrhi::d3d12::StaticDescriptorHeap& srvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap();
        descriptorHeaps.CBV_SRV_UAV.BaseIndex = srvHeap.AllocateDescriptors(GFSDK_RTAO_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
        descriptorHeaps.CBV_SRV_UAV.pDescHeap = srvHeap.GetShaderVisibleHeap();
        m_BaseSrvDescriptor = descriptorHeaps.CBV_SRV_UAV.BaseIndex;
        m_SrvHeap = descriptorHeaps.CBV_SRV_UAV.pDescHeap;
        nvrhi::d3d12::StaticDescriptorHeap& rtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap();
        descriptorHeaps.RTV.BaseIndex = rtvHeap.AllocateDescriptors(GFSDK_RTAO_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
        descriptorHeaps.RTV.pDescHeap = rtvHeap.GetHeap();
        m_BaseRtvDescriptor = descriptorHeaps.RTV.BaseIndex;
        m_RtvHeap = descriptorHeaps.RTV.pDescHeap;

        GFSDK_RTAO_Status Status = GFSDK_RTAO_CreateContext_D3D12(
            m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device),
            1,
            descriptorHeaps,
            &m_RtaoContextD3D12);

        return;
    }
}

void RtaoDenoiserPass::ReleaseRtaoContext()
{
    if (m_RtaoContextD3D12)
    {
        m_RtaoContextD3D12->Release();
        m_RtaoContextD3D12 = nullptr;
    }

    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        if (m_SrvHeap)
        {
            d3d12Device->GetShaderResourceViewDescriptorHeap().ReleaseDescriptors(m_BaseSrvDescriptor, GFSDK_RTAO_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
            m_BaseSrvDescriptor = 0;
            m_SrvHeap = nullptr;
        }

        if (m_RtvHeap)
        {
            d3d12Device->GetRenderTargetViewDescriptorHeap().ReleaseDescriptors(m_BaseRtvDescriptor, GFSDK_RTAO_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
            m_BaseRtvDescriptor = 0;
            m_RtvHeap = nullptr;
        }
    }
}

void RtaoDenoiserPass::Render(
    nvrhi::ICommandList* commandList,
    const GFSDK_RTAO_DenoiseAOParameters& params,
    const ICompositeView& compositeView,
    nvrhi::ITexture* inputDepth,
    nvrhi::ITexture* inputNormals,
    nvrhi::ITexture* inputDistance,
    nvrhi::ITexture* inputAO,
    nvrhi::ITexture* outputTexture)
{
    if (m_Device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
        return;

    commandList->beginMarker("HBAO+");
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

            ReleaseRtaoContext();
            CreateRtaoContext();
        }
    }

    for (uint32_t viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);

        float4x4 viewMatrix = affineToHomogeneous(view->GetViewMatrix());

        float4x4 projectionMatrix = view->GetProjectionMatrix(false);
        projectionMatrix *= 1.0f / projectionMatrix[2][3];
        projectionMatrix[2][3] = 1.0f;

        nvrhi::ViewportState viewportState = view->GetViewportState();
        assert(viewportState.viewports.size() == 1);

        const nvrhi::Viewport& sourceViewport = viewportState.viewports[0];

        GFSDK_RTAO_InputViewport viewport;
        viewport.Enable = true;
        viewport.TopLeftX = static_cast<GFSDK_RTAO_UINT>(sourceViewport.minX);
        viewport.TopLeftY = static_cast<GFSDK_RTAO_UINT>(sourceViewport.minY);
        viewport.Width = static_cast<GFSDK_RTAO_UINT>(sourceViewport.maxX - sourceViewport.minX);
        viewport.Height = static_cast<GFSDK_RTAO_UINT>(sourceViewport.maxY - sourceViewport.minY);
        viewport.MinDepth = static_cast<GFSDK_RTAO_FLOAT>(sourceViewport.minZ);
        viewport.MaxDepth = static_cast<GFSDK_RTAO_FLOAT>(sourceViewport.maxZ);

        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();

        GFSDK_RTAO_InputData_D3D12 inputData = {};
        inputData.DepthData.DepthTextureType = GFSDK_RTAO_HARDWARE_DEPTHS;
        inputData.DepthData.ProjectionMatrix.Data = GFSDK_RTAO_Float4x4(projectionMatrix.m_data);
        inputData.DepthData.ProjectionMatrix.Layout = GFSDK_RTAO_ROW_MAJOR_ORDER;
        inputData.DepthData.Viewport = viewport;

        inputData.DepthData.FullResDepthTextureSRV.pResource = inputDepth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.DepthData.FullResDepthTextureSRV.GpuHandle = inputDepth->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;
        commandList->endTrackingTextureState(inputDepth, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.NormalData.Enable = true;

        inputData.NormalData.FullResNormalTextureSRV.pResource = inputNormals->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.NormalData.FullResNormalTextureSRV.GpuHandle = inputNormals->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;

        inputData.NormalData.WorldToViewMatrix.Data = GFSDK_RTAO_Float4x4(viewMatrix.m_data);
        commandList->endTrackingTextureState(inputNormals, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.HitDistanceTexture.pResource = inputDistance->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.HitDistanceTexture.GpuHandle = inputDistance->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;

        commandList->endTrackingTextureState(inputDistance, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

        inputData.AOTexture.pResource = inputAO->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        inputData.AOTexture.GpuHandle = inputAO->getNativeView(
            nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
            nvrhi::Format::UNKNOWN,
            subresources).integer;

        commandList->endTrackingTextureState(inputAO, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);



        GFSDK_RTAO_RenderTargetView_D3D12 outputRTV = {};
        outputRTV.pResource = outputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        outputRTV.CpuHandle = static_cast<GFSDK_RTAO_SIZE_T>(outputTexture->getNativeView(
            nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor,
            nvrhi::Format::UNKNOWN,
            subresources).integer);
        commandList->endTrackingTextureState(outputTexture, subresources, nvrhi::ResourceStates::RENDER_TARGET);

        GFSDK_RTAO_Output_D3D12 outputData = {};
        outputData.pRenderTargetView = &outputRTV;
        outputData.Blend.Mode = GFSDK_RTAO_OVERWRITE_RGB;

        currentSrvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap().GetShaderVisibleHeap();
        currentRtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap().GetHeap();

        if (currentSrvHeap == m_SrvHeap && currentRtvHeap == m_RtvHeap)
        {
            GFSDK_RTAO_Status Status = m_RtaoContextD3D12->DenoiseAO(
                m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_CommandQueue),
                commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList),
                inputData,
                params,
                outputData
            );

            assert(Status == GFSDK_RTAO_OK);
        }
    }

    commandList->clearState();
    commandList->endMarker();
}
