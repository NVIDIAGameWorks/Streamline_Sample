
#include <donut/render/plugins/VolumeLightingPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/View.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShadowMap.h>

#if USE_DX11 && USE_DX12
#error "The GameWorks Volumetric Lightihg library cannot be used in an application that supports both DX11 and DX12"
#endif

#if USE_DX11
#define NV_PLATFORM_D3D11
#elif USE_DX12
#define NV_PLATFORM_D3D12
#include <nvrhi/d3d12/d3d12.h>
#endif

#include <NvVolumetricLighting.h>

// This is a brute-force workaround for an unlikely bug. 
// The problem is, when the VL integration calls getNativeView on nvrhi resources multiple times, 
// the nvrhi backend can decide to grow a descriptor heap somewhere in the middle of that sequence.
// Which makes the previously returned views invalid. Doing the whole sequence twice handles that case
// because the second iteration doesn't allocate any views, so the heap doesn't grow.
// Unless there's a different thread which also creates some views... but right now there isn't.
#define NUM_RESOURCE_CONVERSION_PASSES 2

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

class VolumeLightingLibrary
{
public:
    static void Initialize()
    {
        static VolumeLightingLibrary Instance;
    }
    
private:
    VolumeLightingLibrary() 
    {
        Nv::Vl::OpenLibrary();
    }

    ~VolumeLightingLibrary() 
    {
        Nv::Vl::CloseLibrary();
    }
};

VolumeLightingPass::VolumeLightingPass(
    nvrhi::IDevice* device,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView
)
    : m_Device(device)
    , m_FramebufferFactory(framebufferFactory)
{
    VolumeLightingLibrary::Initialize();

    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    Nv::Vl::ContextDesc contextDesc;
    const nvrhi::FramebufferInfo& fbinfo = sampleFramebuffer->GetFramebufferInfo();
    contextDesc.framebuffer.uWidth = fbinfo.width;
    contextDesc.framebuffer.uHeight = fbinfo.height;
    contextDesc.framebuffer.uSamples = fbinfo.sampleCount;
    contextDesc.eDownsampleMode = Nv::Vl::DownsampleMode::FULL; // ?
    contextDesc.eFilterMode = Nv::Vl::FilterMode::NONE; // ?
    contextDesc.eInternalSampleMode = Nv::Vl::MultisampleMode::SINGLE; // ?

    m_VolumeLightingContext = nullptr;

    Nv::Vl::PlatformDesc platformDesc = {};

#if USE_DX11
    platformDesc.platform = Nv::VolumetricLighting::PlatformName::D3D11;
    platformDesc.d3d11.pDevice = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D11_Device);
#elif USE_DX12
    platformDesc.platform = Nv::VolumetricLighting::PlatformName::D3D12;
    platformDesc.d3d12.pDevice = m_Device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
#endif

    Nv::Vl::Status Status = Nv::Vl::CreateContext(m_VolumeLightingContext, &platformDesc, &contextDesc);

    assert(Status == Nv::Vl::Status::OK);

#if USE_DX12
    CreateLibraryResources(fbinfo.width, fbinfo.height);
#endif
}

VolumeLightingPass::~VolumeLightingPass()
{
    if (m_VolumeLightingContext)
    {
        m_Device->waitForIdle();

        Nv::Vl::ReleaseContext(m_VolumeLightingContext);
        m_VolumeLightingContext = nullptr;
    }
}

void VolumeLightingPass::CreateLibraryResources(uint32_t width, uint32_t height)
{
    const uint32_t LIGHT_LUT_DEPTH_RESOLUTION = 128;
    const uint32_t LIGHT_LUT_WDOTV_RESOLUTION = 512;

    auto createRenderTarget = [this](uint32_t w, uint32_t h, nvrhi::Format format, bool isUAV, nvrhi::Color clearColor, nvrhi::ResourceStates::Enum initialState, const char* name)
    {
        nvrhi::TextureDesc desc;
        desc.height = h;
        desc.width = w;
        desc.format = format;
        desc.debugName = name;
        desc.isRenderTarget = true;
        desc.isUAV = isUAV;
        desc.useClearValue = true;
        desc.clearValue = clearColor;
        desc.initialState = initialState;
        desc.isTypeless = (format == nvrhi::Format::D24S8);

        return m_Device->createTexture(desc);
    };

    const nvrhi::Color black = nvrhi::Color(0.f);
    const nvrhi::Color depth1stencil0 = nvrhi::Color(1.f, 0.f, 0.f, 0.f);

    m_VlPhaseLUT = createRenderTarget(1, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Phase LUT");
    m_VlLightLUT_P[0] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Point [0]");
    m_VlLightLUT_P[1] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Point [1]");
    m_VlLightLUT_S1[0] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Spot 1 [0]");
    m_VlLightLUT_S1[1] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Spot 1 [1]");
    m_VlLightLUT_S2[0] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Spot 2 [0]");
    m_VlLightLUT_S2[1] = createRenderTarget(LIGHT_LUT_DEPTH_RESOLUTION, LIGHT_LUT_WDOTV_RESOLUTION, nvrhi::Format::RGBA16_FLOAT, true, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Light LUT Spot 2 [1]");

    m_VlAccumulation = createRenderTarget(width, height, nvrhi::Format::RGBA16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Accumulation");
    m_VlResolvedAccumulation = createRenderTarget(width, height, nvrhi::Format::RGBA16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Resolved Accumulation");
    m_VlFilteredAccumulation[0] = createRenderTarget(width, height, nvrhi::Format::RGBA16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Filtered Accumulation 0");
    m_VlFilteredAccumulation[1] = createRenderTarget(width, height, nvrhi::Format::RGBA16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Filtered Accumulation 1");
    m_VlResolvedDepth = createRenderTarget(width, height, nvrhi::Format::RG16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Resolved Depth");
    m_VlFilteredDepth[0] = createRenderTarget(width, height, nvrhi::Format::RG16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Filtered Depth 0");
    m_VlFilteredDepth[1] = createRenderTarget(width, height, nvrhi::Format::RG16_FLOAT, false, black, nvrhi::ResourceStates::RENDER_TARGET, "NvVl::Filtered Depth 1");

    m_VlDepth = createRenderTarget(width, height, nvrhi::Format::D24S8, false, depth1stencil0, nvrhi::ResourceStates::DEPTH_WRITE, "NvVl::Depth");
}

static NvcVec3 VectorToNvc(const float3 v)
{
    return NvcVec3{ v.x, v.y, v.z };
}

static NvcMat44 MatrixToNvc(const float4x4& m)
{
    return *reinterpret_cast<const NvcMat44*>(&m);
}

void ConvertRenderContext(nvrhi::ICommandList* commandList, Nv::Vl::PlatformRenderCtx& renderCtx)
{
#if USE_DX11
    renderCtx.d3d11 = commandList->getDevice()->getNativeObject(nvrhi::ObjectTypes::D3D11_DeviceContext);
#elif USE_DX12
    renderCtx.d3d12 = commandList->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
#endif
}

void ConvertDepthStencilTarget(const nvrhi::FramebufferAttachment& attachment, Nv::VolumetricLighting::PlatformDepthStencilTarget& target)
{
#if USE_DX11
    target.d3d11_dsv = attachment.getNativeView(nvrhi::ObjectTypes::D3D11_DepthStencilView);
    target.d3d11_srv = attachment.getNativeView(nvrhi::ObjectTypes::D3D11_ShaderResourceView);
#elif USE_DX12
    target.d3d12_resource = attachment.texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    target.d3d12_dsv = attachment.getNativeView(nvrhi::ObjectTypes::D3D12_DepthStencilViewDescriptor).integer;
    target.d3d12_srv = attachment.getNativeView(nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror).integer;
    target.d3d12_dsv_readonly = target.d3d12_dsv;
#endif
}

void ConvertRenderTarget(const nvrhi::FramebufferAttachment& attachment, Nv::VolumetricLighting::PlatformRenderTarget& target)
{
#if USE_DX11
    target.d3d11_rtv = attachment.getNativeView(nvrhi::ObjectTypes::D3D11_RenderTargetView);
    target.d3d11_srv = attachment.getNativeView(nvrhi::ObjectTypes::D3D11_ShaderResourceView);
#elif USE_DX12
    target.d3d12_resource = attachment.texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    target.d3d12_rtv = attachment.texture->GetDesc().isRenderTarget ? attachment.getNativeView(nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor).integer : 0;
    target.d3d12_srv = attachment.getNativeView(nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror).integer;
    target.d3d12_uav = attachment.texture->GetDesc().isUAV ? attachment.getNativeView(nvrhi::ObjectTypes::D3D12_UnorderedAccessViewGpuDescripror).integer : 0;
#endif
}

void ConvertShaderResource(nvrhi::ITexture* texture, Nv::VolumetricLighting::PlatformShaderResource& resource, nvrhi::Format format = nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet subresources = nvrhi::AllSubresources)
{
#if USE_DX11
    resource.d3d11 = texture->getNativeView(nvrhi::ObjectTypes::D3D11_ShaderResourceView, format, subresources);
#elif USE_DX12
    resource.d3d12 = texture->getNativeView(nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror, format, subresources).integer;
#endif
}

void VolumeLightingPass::BeginAccumulation(nvrhi::ICommandList* commandList, const PlanarView& view, const VolumeLightingMediumParameters& mediumParams)
{
    if (!m_VolumeLightingContext)
        return;

    commandList->beginMarker("BeginAccumulation");
    if(m_NeedClearState) commandList->clearState();

    nvrhi::IFramebuffer* framebuffer = m_FramebufferFactory->GetFramebuffer(view);
    const nvrhi::FramebufferAttachment& depthAttachment = framebuffer->GetDesc().depthAttachment;

    Nv::Vl::ViewerDesc viewerDesc = {};
    {
        nvrhi::Rect viewExtent = view.GetViewExtent();
        viewerDesc.uViewportWidth = viewExtent.maxX - viewExtent.minX;
        viewerDesc.uViewportHeight = viewExtent.maxY - viewExtent.minY;
        viewerDesc.vEyePosition = VectorToNvc(view.GetViewOrigin());
        viewerDesc.mProj = MatrixToNvc(view.GetProjectionMatrix(true));
        viewerDesc.mViewProj = MatrixToNvc(view.GetViewProjectionMatrix(true));
    }

    Nv::Vl::MediumDesc mediumDesc = {};
    {
        float scale = powf(10.f, mediumParams.logScale);
        mediumDesc.vAbsorption = VectorToNvc(mediumParams.absorption * scale);
        mediumDesc.uNumPhaseTerms = mediumParams.numPhaseTerms;

        for (uint32_t term = 0; term < mediumParams.numPhaseTerms; term++)
        {
            switch (mediumParams.phaseTerms[term].phaseFunction)
            {
            case VolumeLightingPhaseFunctionType::ISOTROPIC:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::ISOTROPIC;
                break;
            case VolumeLightingPhaseFunctionType::RAYLEIGH:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::RAYLEIGH;
                break;
            case VolumeLightingPhaseFunctionType::HENYEYGREENSTEIN:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::HENYEYGREENSTEIN;
                break;
            case VolumeLightingPhaseFunctionType::MIE_HAZY:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::MIE_HAZY;
                break;
            case VolumeLightingPhaseFunctionType::MIE_MURKY:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::MIE_MURKY;
                break;
            default:
                mediumDesc.PhaseTerms[term].ePhaseFunc = Nv::VolumetricLighting::PhaseFunctionType::ISOTROPIC;
                break;
            }

            mediumDesc.PhaseTerms[term].vDensity = VectorToNvc(mediumParams.phaseTerms[term].density * scale);
            mediumDesc.PhaseTerms[term].fEccentricity = mediumParams.phaseTerms[term].eccentricity;
        }
    }

    Nv::Vl::BeginAccumulationArgs beginArgs = {};
    beginArgs.pViewerDesc = &viewerDesc;
    beginArgs.pMediumDesc = &mediumDesc;

    ConvertRenderContext(commandList, beginArgs.renderCtx);

    for (int pass = 0; pass < NUM_RESOURCE_CONVERSION_PASSES; pass++)
    {
        ConvertDepthStencilTarget(depthAttachment, beginArgs.sceneDepth);

#if USE_DX12
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlAccumulation), beginArgs.accumulation);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlPhaseLUT), beginArgs.lutPhase);
        ConvertDepthStencilTarget(nvrhi::FramebufferAttachment(m_VlDepth), beginArgs.internalDepth);
#endif
    }

#if USE_DX12
    nvrhi::d3d12::CommandList* d3d12commandList = commandList->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_CommandList);
    bool allocated = d3d12commandList->allocateUploadBuffer(GFSDK_VOL_BYTES_CBV_SRV_UAV_HEAP_D3D12, reinterpret_cast<void**>(&beginArgs.constantDescriptors.m_pCpu), &beginArgs.constantDescriptors.m_gpu);
    assert(allocated);

    d3d12commandList->commitDescriptorHeaps();
#endif

    Nv::Vl::Status Status = Nv::Vl::BeginAccumulation(m_VolumeLightingContext, &beginArgs);

    assert(Status == Nv::Vl::Status::OK);

    if (m_NeedClearState) commandList->clearState();
    commandList->endMarker();
}

void VolumeLightingPass::RenderVolume(nvrhi::ICommandList* commandList, std::shared_ptr<Light> light, const VolumeLightingLightParameters& lightParams)
{
    if (!m_VolumeLightingContext)
        return;

    if (!light->shadowMap)
        return;

    std::shared_ptr<IShadowMap> shadowMap = light->shadowMap;

    commandList->beginMarker("RenderVolume");
    if (m_NeedClearState) commandList->clearState();

    Nv::Vl::ShadowMapDesc shadowMapDesc = {};
    {
        shadowMapDesc.eType = shadowMap->GetNumberOfCascades() == 1 
            ? Nv::Vl::ShadowMapLayout::SIMPLE 
            : Nv::Vl::ShadowMapLayout::CASCADE_ARRAY;
        shadowMapDesc.uWidth = shadowMap->GetTextureSize().x;
        shadowMapDesc.uHeight = shadowMap->GetTextureSize().y;
        shadowMapDesc.uElementCount = std::min(lightParams.maxCascades, shadowMap->GetNumberOfCascades());
        for (uint32_t nCascade = 0; nCascade < shadowMapDesc.uElementCount; nCascade++)
        {
            const IShadowMap* cascade = shadowMap->GetCascade(nCascade);
            const IView* view = cascade->GetView().GetChildView(ViewType::PLANAR, 0);

            shadowMapDesc.Elements[nCascade].uOffsetX = 0; // TODO: not zero, but not derived from cascade->GetUVRange either.
            shadowMapDesc.Elements[nCascade].uOffsetY = 0;
            shadowMapDesc.Elements[nCascade].uWidth = shadowMapDesc.uWidth;
            shadowMapDesc.Elements[nCascade].uHeight = shadowMapDesc.uHeight;
            shadowMapDesc.Elements[nCascade].mViewProj = MatrixToNvc(view->GetViewProjectionMatrix(false));
            shadowMapDesc.Elements[nCascade].mArrayIndex = view->GetSubresources().baseArraySlice;
        }
    }

    Nv::Vl::VolumeDesc volumeDesc = {};
    {
        volumeDesc.fTargetRayResolution = 12.0f;
        volumeDesc.uMaxMeshResolution = shadowMapDesc.uWidth;
        volumeDesc.fDepthBias = 0.0f;
        volumeDesc.eTessQuality = Nv::Vl::TessellationQuality::HIGH;
    }

    Nv::Vl::LightDesc lightDesc = {};
    {
        lightDesc.mLightToWorld = MatrixToNvc(shadowMap->GetCascade(shadowMapDesc.uElementCount - 1)->GetView().GetChildView(ViewType::PLANAR, 0)->GetInverseViewProjectionMatrix(true));

        float lightScale = 1.f;
        switch (light->GetLightType())
        {
        case LightType::DIRECTIONAL: {
            DirectionalLight& directionalLight = static_cast<DirectionalLight&>(*light);
            lightDesc.eType = Nv::Vl::LightType::DIRECTIONAL;
            lightDesc.Directional.vDirection = VectorToNvc(normalize(directionalLight.direction));
            lightScale = directionalLight.irradiance;
            break;
        }

        case LightType::SPOT:
        case LightType::POINT:
        default:
            assert(!"Other light types are not supported yet");
            break;
        }

        lightDesc.vIntensity = VectorToNvc(light->color * lightParams.intensity * lightScale);
    }

    Nv::Vl::RenderVolumeArgs volumeArgs = {};
    volumeArgs.pShadowMapDesc = &shadowMapDesc;
    volumeArgs.pVolumeDesc = &volumeDesc;
    volumeArgs.pLightDesc = &lightDesc;

    ConvertRenderContext(commandList, volumeArgs.renderCtx);

    for (int pass = 0; pass < NUM_RESOURCE_CONVERSION_PASSES; pass++)
    {
        ConvertShaderResource(shadowMap->GetTexture(), volumeArgs.shadowMap);

#if USE_DX12
        ConvertDepthStencilTarget(nvrhi::FramebufferAttachment(m_VlDepth), volumeArgs.internalDepth);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlAccumulation), volumeArgs.accumulation);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlPhaseLUT), volumeArgs.lutPhase);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_P[0]), volumeArgs.lutPoint0);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_P[1]), volumeArgs.lutPoint1);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_S1[0]), volumeArgs.lutSpot0[0]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_S1[1]), volumeArgs.lutSpot0[1]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_S2[0]), volumeArgs.lutSpot1[0]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlLightLUT_S2[1]), volumeArgs.lutSpot1[1]);
#endif
    }

#if USE_DX12
    nvrhi::d3d12::CommandList* d3d12commandList = commandList->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_CommandList);
    d3d12commandList->commitDescriptorHeaps();
#endif

    Nv::Vl::Status Status = Nv::Vl::RenderVolume(m_VolumeLightingContext, &volumeArgs);

    assert(Status == Nv::Vl::Status::OK);

    if (m_NeedClearState) commandList->clearState();
    commandList->endMarker();
}

void VolumeLightingPass::EndAccumulation(nvrhi::ICommandList* commandList)
{
    if (!m_VolumeLightingContext)
        return;

    commandList->beginMarker("EndAccumulation");
    if (m_NeedClearState) commandList->clearState();

    Nv::Vl::EndAccumulationArgs endArgs = {};

    ConvertRenderContext(commandList, endArgs.renderCtx);

    Nv::Vl::Status Status = Nv::Vl::EndAccumulation(m_VolumeLightingContext, &endArgs);

    assert(Status == Nv::Vl::Status::OK);

    if (m_NeedClearState) commandList->clearState();
    commandList->endMarker();
}

void VolumeLightingPass::ApplyLighting(nvrhi::ICommandList* commandList, const PlanarView& view)
{
    if (!m_VolumeLightingContext)
        return;

    commandList->beginMarker("ApplyLighting");
    if (m_NeedClearState) commandList->clearState();

    nvrhi::IFramebuffer* framebuffer = m_FramebufferFactory->GetFramebuffer(view);
    const nvrhi::FramebufferAttachment& depthAttachment = framebuffer->GetDesc().depthAttachment;
    const nvrhi::FramebufferAttachment& colorAttachment = framebuffer->GetDesc().colorAttachments[0];

    Nv::Vl::PostprocessDesc postprocessDesc = {};
    postprocessDesc.mUnjitteredViewProj = MatrixToNvc(view.GetViewProjectionMatrix(false));
    postprocessDesc.vFogLight = VectorToNvc(float3(1.0f));
    postprocessDesc.fMultiscatter = 0.000002f;
    postprocessDesc.bDoFog = true;
    postprocessDesc.bIgnoreSkyFog = false;
    postprocessDesc.eUpsampleQuality = Nv::Vl::UpsampleQuality::BILINEAR;
    postprocessDesc.fBlendfactor = 1.0f;
    postprocessDesc.fTemporalFactor = 0.95f;
    postprocessDesc.fFilterThreshold = 0.20f;

    Nv::Vl::ApplyLightingArgs applyArgs = {};
    applyArgs.pPostprocessDesc = &postprocessDesc;

    ConvertRenderContext(commandList, applyArgs.renderCtx);

    for (int pass = 0; pass < NUM_RESOURCE_CONVERSION_PASSES; pass++)
    {
        ConvertRenderTarget(colorAttachment, applyArgs.sceneTarget);
        ConvertDepthStencilTarget(depthAttachment, applyArgs.sceneDepth);

#if USE_DX12
        ConvertDepthStencilTarget(nvrhi::FramebufferAttachment(m_VlDepth), applyArgs.internalDepth);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlAccumulation), applyArgs.accumulation);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlPhaseLUT), applyArgs.lutPhase);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlResolvedAccumulation), applyArgs.resolvedAccumulation);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlFilteredAccumulation[0]), applyArgs.filteredAccumulation[0]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlFilteredAccumulation[1]), applyArgs.filteredAccumulation[1]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlResolvedDepth), applyArgs.resolvedDepth);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlFilteredDepth[0]), applyArgs.filteredDepth[0]);
        ConvertRenderTarget(nvrhi::FramebufferAttachment(m_VlFilteredDepth[1]), applyArgs.filteredDepth[1]);
#endif
    }

#if USE_DX12
    nvrhi::d3d12::CommandList* d3d12commandList = commandList->getNativeObject(nvrhi::ObjectTypes::Nvrhi_D3D12_CommandList);
    d3d12commandList->commitDescriptorHeaps();
#endif


    Nv::Vl::Status Status = Nv::Vl::ApplyLighting(m_VolumeLightingContext, &applyArgs);

    assert(Status == Nv::Vl::Status::OK);

    if (m_NeedClearState) commandList->clearState();
    commandList->endMarker();
}

void VolumeLightingPass::RenderSingleLight(
    nvrhi::ICommandList* commandList, 
    const ICompositeView& compositeView,
    std::shared_ptr<Light> light,
    const VolumeLightingMediumParameters& mediumParams,
    const VolumeLightingLightParameters& lightParams)
{
    if (!m_VolumeLightingContext)
        return;

    if (!light->shadowMap)
        return;

    commandList->beginMarker("VolumeLighting");
    commandList->clearState();
    m_NeedClearState = false;

    for (uint viewIndex = 0; viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR); viewIndex++)
    {
        const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
        const PlanarView* planarView = dynamic_cast<const PlanarView*>(view);

        if (planarView)
        {
            nvrhi::IFramebuffer* framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
            const nvrhi::FramebufferAttachment& depthAttachment = framebuffer->GetDesc().depthAttachment;
            const nvrhi::FramebufferAttachment& colorAttachment = framebuffer->GetDesc().colorAttachments[0];
            commandList->endTrackingTextureState(depthAttachment.texture, depthAttachment.subresources, nvrhi::ResourceStates::DEPTH_WRITE);
            commandList->endTrackingTextureState(colorAttachment .texture, colorAttachment.subresources, nvrhi::ResourceStates::RENDER_TARGET);

            BeginAccumulation(commandList, *planarView, mediumParams);
            RenderVolume(commandList, light, lightParams);
            EndAccumulation(commandList);
            ApplyLighting(commandList, *planarView);

            commandList->beginTrackingTextureState(depthAttachment.texture, depthAttachment.subresources, nvrhi::ResourceStates::DEPTH_WRITE);
            commandList->beginTrackingTextureState(colorAttachment.texture, colorAttachment.subresources, nvrhi::ResourceStates::RENDER_TARGET);
        }
    }

    commandList->clearState();
    m_NeedClearState = true;
    commandList->endMarker();
}
