
#include <donut/render/plugins/HbaoPlusPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/engine/View.h>
#include "GFSDK_SSAO.h"

#if USE_DX12
#include <nvrhi/d3d12/d3d12.h>
#endif

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

HbaoPlusPass::HbaoPlusPass(nvrhi::IDevice* device)
    : m_Device(device)
{
    CreateHbaoPlusContext();
}

HbaoPlusPass::~HbaoPlusPass()
{
    ReleaseHbaoPlusContext();
}

void HbaoPlusPass::CreateHbaoPlusContext()
{
#if USE_DX11
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
    {
        GFSDK_SSAO_Status Status = GFSDK_SSAO_CreateContext_D3D11(
            m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_Device),
            &m_HbaoContextD3D11);

        assert(Status == GFSDK_SSAO_OK);

        return;
    }
#endif

#if USE_DX12
    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        GFSDK_SSAO_DescriptorHeaps_D3D12 descriptorHeaps;
        nvrhi::d3d12::StaticDescriptorHeap& srvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap();
        descriptorHeaps.CBV_SRV_UAV.BaseIndex = srvHeap.AllocateDescriptors(GFSDK_SSAO_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
        descriptorHeaps.CBV_SRV_UAV.pDescHeap = srvHeap.GetShaderVisibleHeap();
        m_BaseSrvDescriptor = descriptorHeaps.CBV_SRV_UAV.BaseIndex;
        m_SrvHeap = descriptorHeaps.CBV_SRV_UAV.pDescHeap;
        nvrhi::d3d12::StaticDescriptorHeap& rtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap();
        descriptorHeaps.RTV.BaseIndex = rtvHeap.AllocateDescriptors(GFSDK_SSAO_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
        descriptorHeaps.RTV.pDescHeap = rtvHeap.GetHeap();
        m_BaseRtvDescriptor = descriptorHeaps.RTV.BaseIndex;
        m_RtvHeap = descriptorHeaps.RTV.pDescHeap;

        GFSDK_SSAO_Status Status = GFSDK_SSAO_CreateContext_D3D12(
            m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device),
            1,
            descriptorHeaps,
            &m_HbaoContextD3D12);

        return;
    }
#endif
}

void HbaoPlusPass::ReleaseHbaoPlusContext()
{
#if USE_DX11
    if (m_HbaoContextD3D11)
    {
        m_HbaoContextD3D11->Release();
        m_HbaoContextD3D11 = nullptr;
    }
#endif

#if USE_DX12
    if (m_HbaoContextD3D12)
    {
        m_HbaoContextD3D12->Release();
        m_HbaoContextD3D12 = nullptr;
    }

    if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
    {
        nvrhi::d3d12::Device* d3d12Device = m_Device->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_Device);

        if (m_SrvHeap)
        {
            d3d12Device->GetShaderResourceViewDescriptorHeap().ReleaseDescriptors(m_BaseSrvDescriptor, GFSDK_SSAO_NUM_DESCRIPTORS_CBV_SRV_UAV_HEAP_D3D12);
            m_BaseSrvDescriptor = 0;
            m_SrvHeap = nullptr;
        }

        if (m_RtvHeap)
        {
            d3d12Device->GetRenderTargetViewDescriptorHeap().ReleaseDescriptors(m_BaseRtvDescriptor, GFSDK_SSAO_NUM_DESCRIPTORS_RTV_HEAP_D3D12);
            m_BaseRtvDescriptor = 0;
            m_RtvHeap = nullptr;
        }
    }
#endif
}

bool HbaoPlusPass::CanRender()
{
#if USE_DX12
    if (m_HbaoContextD3D12 != NULL)
    {
        return true;
    }
#endif

#if USE_DX11
    if (m_HbaoContextD3D11 != NULL)
    {
        return true;
    }
#endif

    return false;
}

void HbaoPlusPass::Render(
    nvrhi::ICommandList* commandList,
    const SsaoParameters& params,
    const ICompositeView& compositeView,
    nvrhi::ITexture* inputDepth,
    nvrhi::ITexture* inputNormals,
    nvrhi::ITexture* outputTexture)
{
    commandList->beginMarker("HBAO+");
    commandList->clearState();

#if USE_DX12
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

            ReleaseHbaoPlusContext();
            CreateHbaoPlusContext();
        }
    }
#endif

    GFSDK_SSAO_Parameters hbaoParams;
    hbaoParams.Radius = params.radiusWorld;
    hbaoParams.Bias = params.surfaceBias;
    hbaoParams.PowerExponent = params.powerExponent;
    hbaoParams.SmallScaleAO = params.amount * 0.5f;
    hbaoParams.LargeScaleAO = params.amount * 0.5f;
    hbaoParams.BackgroundAO.Enable = true;
    hbaoParams.BackgroundAO.BackgroundViewDepth = params.backgroundViewDepth;
    hbaoParams.DepthStorage = GFSDK_SSAO_FP32_VIEW_DEPTHS;
    hbaoParams.Blur.Enable = params.enableBlur;
    hbaoParams.Blur.Sharpness = params.blurSharpness;

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

        GFSDK_SSAO_InputViewport viewport;
        viewport.Enable = true;
        viewport.TopLeftX = static_cast<GFSDK_SSAO_UINT>(sourceViewport.minX);
        viewport.TopLeftY = static_cast<GFSDK_SSAO_UINT>(sourceViewport.minY);
        viewport.Width = static_cast<GFSDK_SSAO_UINT>(sourceViewport.maxX - sourceViewport.minX);
        viewport.Height = static_cast<GFSDK_SSAO_UINT>(sourceViewport.maxY - sourceViewport.minY);
        viewport.MinDepth = static_cast<GFSDK_SSAO_FLOAT>(sourceViewport.minZ);
        viewport.MaxDepth = static_cast<GFSDK_SSAO_FLOAT>(sourceViewport.maxZ);

        nvrhi::TextureSubresourceSet subresources = view->GetSubresources();

#if USE_DX11
        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D11)
        {
            GFSDK_SSAO_InputData_D3D11 inputData;
            inputData.DepthData.DepthTextureType = GFSDK_SSAO_HARDWARE_DEPTHS;
            inputData.DepthData.MetersToViewSpaceUnits = 1.0f;
            inputData.DepthData.ProjectionMatrix.Data = GFSDK_SSAO_Float4x4(projectionMatrix.m_data);
            inputData.DepthData.ProjectionMatrix.Layout = GFSDK_SSAO_ROW_MAJOR_ORDER;
            inputData.DepthData.Viewport = viewport;

            inputData.DepthData.pFullResDepthTextureSRV = inputDepth->getNativeView(
                nvrhi::ObjectTypes::D3D11_ShaderResourceView,
                nvrhi::Format::UNKNOWN,
                subresources);

            if (inputNormals)
            {
                inputData.NormalData.Enable = true;

                inputData.NormalData.pFullResNormalTextureSRV = inputNormals->getNativeView(
                    nvrhi::ObjectTypes::D3D11_ShaderResourceView,
                    nvrhi::Format::UNKNOWN,
                    subresources);

                inputData.NormalData.WorldToViewMatrix.Data = GFSDK_SSAO_Float4x4(viewMatrix.m_data);
            }

            GFSDK_SSAO_Output_D3D11 outputData;
            outputData.pRenderTargetView = outputTexture->getNativeView(
                nvrhi::ObjectTypes::D3D11_RenderTargetView,
                nvrhi::Format::UNKNOWN,
                subresources);
            outputData.Blend.Mode = GFSDK_SSAO_MULTIPLY_RGB;

            GFSDK_SSAO_Status Status = m_HbaoContextD3D11->RenderAO(
                m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext),
                inputData,
                hbaoParams,
                outputData
            );

            assert(Status == GFSDK_SSAO_OK);
        }
#endif

#if USE_DX12
        if (m_Device->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12)
        {
            GFSDK_SSAO_InputData_D3D12 inputData;
            inputData.DepthData.DepthTextureType = GFSDK_SSAO_HARDWARE_DEPTHS;
            inputData.DepthData.MetersToViewSpaceUnits = 1.0f;
            inputData.DepthData.ProjectionMatrix.Data = GFSDK_SSAO_Float4x4(projectionMatrix.m_data);
            inputData.DepthData.ProjectionMatrix.Layout = GFSDK_SSAO_ROW_MAJOR_ORDER;
            inputData.DepthData.Viewport = viewport;

            inputData.DepthData.FullResDepthTextureSRV.pResource = inputDepth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            inputData.DepthData.FullResDepthTextureSRV.GpuHandle = inputDepth->getNativeView(
                nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
                nvrhi::Format::UNKNOWN,
                subresources).integer;
            commandList->endTrackingTextureState(inputDepth, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);

            if (inputNormals)
            {
                inputData.NormalData.Enable = true;

                inputData.NormalData.FullResNormalTextureSRV.pResource = inputNormals->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
                inputData.NormalData.FullResNormalTextureSRV.GpuHandle = inputNormals->getNativeView(
                    nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror,
                    nvrhi::Format::UNKNOWN,
                    subresources).integer;

                inputData.NormalData.WorldToViewMatrix.Data = GFSDK_SSAO_Float4x4(viewMatrix.m_data);
                commandList->endTrackingTextureState(inputNormals, subresources, nvrhi::ResourceStates::SHADER_RESOURCE);
            }

            GFSDK_SSAO_RenderTargetView_D3D12 outputRTV;
            outputRTV.pResource = outputTexture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
            outputRTV.CpuHandle = static_cast<GFSDK_SSAO_SIZE_T>(outputTexture->getNativeView(
                nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor,
                nvrhi::Format::UNKNOWN,
                subresources).integer);
            commandList->endTrackingTextureState(outputTexture, subresources, nvrhi::ResourceStates::RENDER_TARGET);

            GFSDK_SSAO_Output_D3D12 outputData;
            outputData.pRenderTargetView = &outputRTV;
            outputData.Blend.Mode = GFSDK_SSAO_MULTIPLY_RGB;

            currentSrvHeap = d3d12Device->GetShaderResourceViewDescriptorHeap().GetShaderVisibleHeap();
            currentRtvHeap = d3d12Device->GetRenderTargetViewDescriptorHeap().GetHeap();

            if (currentSrvHeap == m_SrvHeap && currentRtvHeap == m_RtvHeap)
            {
                GFSDK_SSAO_Status Status = m_HbaoContextD3D12->RenderAO(
                    m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_CommandQueue),
                    commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList),
                    inputData,
                    hbaoParams,
                    outputData
                );

                assert(Status == GFSDK_SSAO_OK);
            }
        }
#endif
    }

    commandList->clearState();
    commandList->endMarker();
}
