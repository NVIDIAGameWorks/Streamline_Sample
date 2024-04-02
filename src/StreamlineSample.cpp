////----------------------------------------------------------------------------------
//// File:        StreamlineSample.cpp
//// SDK Version: 2.0
//// Email:       StreamlineSupport@nvidia.com
//// Site:        http://developer.nvidia.com/
////
//// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
////
//// Redistribution and use in source and binary forms, with or without
//// modification, are permitted provided that the following conditions
//// are met:
////  * Redistributions of source code must retain the above copyright
////    notice, this list of conditions and the following disclaimer.
////  * Redistributions in binary form must reproduce the above copyright
////    notice, this list of conditions and the following disclaimer in the
////    documentation and/or other materials provided with the distribution.
////  * Neither the name of NVIDIA CORPORATION nor the names of its
////    contributors may be used to endorse or promote products derived
////    from this software without specific prior written permission.
////
//// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
//// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
//// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////
////----------------------------------------------------------------------------------

#include "StreamlineSample.h"
#include <thread>

#if USE_DX12
#include <d3d12.h>
#include <nvrhi/d3d12.h>
#endif
#if USE_VK
#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
#include <../src/vulkan/vulkan-backend.h>
#endif


using namespace donut;
using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;
using namespace donut::render;

// Constructor
StreamlineSample::StreamlineSample(
    DeviceManager* deviceManager,
    sl::ViewportHandle vpHandle,
    UIData& ui,
    const std::string& sceneName,
    ScriptingConfig scriptingConfig)
    : Super(deviceManager)
    , m_viewport(vpHandle)
    , m_ui(ui)
    , m_BindingCache(deviceManager->GetDevice())
    , m_ScriptingConfig(scriptingConfig)
{

    m_ui.DLSS_Supported = SLWrapper::Get().GetDLSSAvailable();
    m_ui.REFLEX_Supported = SLWrapper::Get().GetReflexAvailable();
    m_ui.NIS_Supported = SLWrapper::Get().GetNISAvailable();
    m_ui.DeepDVC_Supported = SLWrapper::Get().GetDeepDVCAvailable();
    m_ui.DLSSG_Supported = SLWrapper::Get().GetDLSSGAvailable();


    std::shared_ptr<NativeFileSystem> nativeFS = std::make_shared<NativeFileSystem>();

    std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "media";
    std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

    m_RootFs = std::make_shared<RootFileSystem>();
    m_RootFs->mount("/media", mediaPath);
    m_RootFs->mount("/shaders/donut", frameworkShaderPath);
    m_RootFs->mount("/native", nativeFS);

    m_TextureCache = std::make_shared<TextureCache>(GetDevice(), m_RootFs, nullptr);

    m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
    m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);

    m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();

    const nvrhi::Format shadowMapFormats[] = {
        nvrhi::Format::D24S8,
        nvrhi::Format::D32,
        nvrhi::Format::D16,
        nvrhi::Format::D32S8 };

    const nvrhi::FormatSupport shadowMapFeatures =
        nvrhi::FormatSupport::Texture |
        nvrhi::FormatSupport::DepthStencil |
        nvrhi::FormatSupport::ShaderLoad;

    nvrhi::Format shadowMapFormat = nvrhi::utils::ChooseFormat(GetDevice(), shadowMapFeatures, shadowMapFormats, std::size(shadowMapFormats));

    m_ShadowMap = std::make_shared<CascadedShadowMap>(GetDevice(), 2048, 4, 0, shadowMapFormat);
    m_ShadowMap->SetupProxyViews();

    m_ShadowFramebuffer = std::make_shared<FramebufferFactory>(GetDevice());
    m_ShadowFramebuffer->DepthTarget = m_ShadowMap->GetTexture();

    DepthPass::CreateParameters shadowDepthParams;
    shadowDepthParams.slopeScaledDepthBias = 4.f;
    shadowDepthParams.depthBias = 100;
    m_ShadowDepthPass = std::make_shared<DepthPass>(GetDevice(), m_CommonPasses);
    m_ShadowDepthPass->Init(*m_ShaderFactory, shadowDepthParams);

    m_CommandList = GetDevice()->createCommandList();

    m_FirstPersonCamera.SetMoveSpeed(3.0f);

    SetAsynchronousLoadingEnabled(false);

    if (sceneName.empty())
        SetCurrentSceneName("/media/sponza-plus.scene.json");
    else
        SetCurrentSceneName("/native/" + sceneName);

    // Set the callbacks for Reflex
    deviceManager->m_callbacks.beforeFrame = SLWrapper::Callback_FrameCount_Reflex_Sleep_Input_SimStart;
    deviceManager->m_callbacks.afterAnimate = SLWrapper::ReflexCallback_SimEnd;
    deviceManager->m_callbacks.beforeRender = SLWrapper::ReflexCallback_RenderStart;
    deviceManager->m_callbacks.afterRender = SLWrapper::ReflexCallback_RenderEnd;
    deviceManager->m_callbacks.beforePresent = SLWrapper::ReflexCallback_PresentStart;
    deviceManager->m_callbacks.afterPresent = SLWrapper::ReflexCallback_PresentEnd;

    if (m_ScriptingConfig.Reflex_mode != -1 && SLWrapper::Get().GetReflexAvailable()) {
        static constexpr std::array<int, 3> ValidReflexIndices{ 0,1,2 };
        if (std::find(ValidReflexIndices.begin(), ValidReflexIndices.end(), m_ScriptingConfig.Reflex_mode) != ValidReflexIndices.end()) { // CHECK IF THE DLSS MODE IS VALID
            m_ui.REFLEX_Mode = m_ScriptingConfig.Reflex_mode;
        }
    }

    if (m_ScriptingConfig.Reflex_fpsCap > 0 && SLWrapper::Get().GetReflexAvailable())
        m_ui.REFLEX_CapedFPS = m_ScriptingConfig.Reflex_fpsCap;

    if (m_ScriptingConfig.DLSS_mode != -1 && SLWrapper::Get().GetDLSSAvailable()) {
        static constexpr std::array<int, 6> ValidDLLSIndices{ 0,1,2,3,4,6 };
        if (std::find(ValidDLLSIndices.begin(), ValidDLLSIndices.end(), m_ScriptingConfig.DLSS_mode) != ValidDLLSIndices.end()) { // CHECK IF THE DLSS MODE IS VALID
            m_ui.AAMode = AntiAliasingMode::DLSS;
            m_ui.DLSS_Mode = static_cast<sl::DLSSMode>(m_ScriptingConfig.DLSS_mode);
        }
    }
    m_ui.DLSSPresetsReset();

    if (m_ScriptingConfig.DLSSG_on != -1 && SLWrapper::Get().GetDLSSGAvailable() && SLWrapper::Get().GetReflexAvailable()) {
        if (m_ui.REFLEX_Mode == 0) 
            m_ui.REFLEX_Mode = 1;
        m_ui.DLSSG_mode = sl::DLSSGMode::eOn;
    }

    if (m_ScriptingConfig.DeepDVC_on != -1 && SLWrapper::Get().GetDeepDVCAvailable()) {
        m_ui.DeepDVC_Mode = sl::DeepDVCMode::eOn;
    }
};
StreamlineSample::~StreamlineSample()
{
    SLWrapper::Get().SetViewportHandle(m_viewport);
    SLWrapper::Get().CleanupDLSS(true);
    SLWrapper::Get().CleanupDLSSG(false);
}

// Functions of interest

bool StreamlineSample::SetupView()
{

    if (m_TemporalAntiAliasingPass) m_TemporalAntiAliasingPass->SetJitter(m_ui.TemporalAntiAliasingJitter);

    float2 pixelOffset = m_ui.AAMode != AntiAliasingMode::NONE && m_TemporalAntiAliasingPass ? m_TemporalAntiAliasingPass->GetCurrentPixelOffset() : float2(0.f);

    std::shared_ptr<PlanarView> planarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);

    dm::affine3 viewMatrix;
    float verticalFov = dm::radians(m_CameraVerticalFov);
    float zNear = 0.01f;
    viewMatrix = m_FirstPersonCamera.GetWorldToViewMatrix();

    bool topologyChanged = false;

    // Render View
    {
        if (!planarView)
        {
            m_View = planarView = std::make_shared<PlanarView>();
            m_ViewPrevious = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float4x4 projection = perspProjD3DStyleReverse(verticalFov, float(m_RenderingRectSize.x) / m_RenderingRectSize.y, zNear);

        planarView->SetViewport(nvrhi::Viewport((float) m_RenderingRectSize.x, (float)m_RenderingRectSize.y));
        planarView->SetPixelOffset(pixelOffset);

        planarView->SetMatrices(viewMatrix, projection);
        planarView->UpdateCache();

        if (topologyChanged)
        {
            *std::static_pointer_cast<PlanarView>(m_ViewPrevious) = *std::static_pointer_cast<PlanarView>(m_View);
        }
    }

    // ToneMappingView
    {
        std::shared_ptr<PlanarView> tonemappingPlanarView = std::dynamic_pointer_cast<PlanarView, IView>(m_TonemappingView);

        if (!tonemappingPlanarView)
        {
            m_TonemappingView = tonemappingPlanarView = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float4x4 projection = perspProjD3DStyleReverse(verticalFov, float(m_RenderingRectSize.x) / m_RenderingRectSize.y, zNear);

        tonemappingPlanarView->SetViewport(nvrhi::Viewport((float)m_DisplaySize.x, (float)m_DisplaySize.y));
        tonemappingPlanarView->SetMatrices(viewMatrix, projection);
        tonemappingPlanarView->UpdateCache();
    }

    return topologyChanged;
}

void StreamlineSample::CreateRenderPasses(bool& exposureResetRequired, float lodBias)
{
    // Safety measure when we recreate the passes.
    GetDevice()->waitForIdle();

    {
        nvrhi::SamplerDesc samplerdescPoint = m_CommonPasses->m_PointClampSampler->getDesc();
        nvrhi::SamplerDesc samplerdescLinear = m_CommonPasses->m_LinearClampSampler->getDesc();
        nvrhi::SamplerDesc samplerdescLinearWrap = m_CommonPasses->m_LinearWrapSampler->getDesc();
        nvrhi::SamplerDesc samplerdescAniso = m_CommonPasses->m_AnisotropicWrapSampler->getDesc();
        samplerdescPoint.mipBias = lodBias;
        samplerdescLinear.mipBias = lodBias;
        samplerdescLinearWrap.mipBias = lodBias;
        samplerdescAniso.mipBias = lodBias;
        m_CommonPasses->m_PointClampSampler = GetDevice()->createSampler(samplerdescPoint);
        m_CommonPasses->m_LinearClampSampler = GetDevice()->createSampler(samplerdescLinear);
        m_CommonPasses->m_LinearWrapSampler = GetDevice()->createSampler(samplerdescLinearWrap);
        m_CommonPasses->m_AnisotropicWrapSampler = GetDevice()->createSampler(samplerdescAniso);
    }

    uint32_t motionVectorStencilMask = 0x01;

    GBufferFillPass::CreateParameters GBufferParams;
    GBufferParams.enableMotionVectors = true;
    GBufferParams.stencilWriteMask = motionVectorStencilMask;
    m_GBufferPass = std::make_unique<GBufferFillPass>(GetDevice(), m_CommonPasses);
    m_GBufferPass->Init(*m_ShaderFactory, GBufferParams);

    m_DeferredLightingPass = std::make_unique<DeferredLightingPass>(GetDevice(), m_CommonPasses);
    m_DeferredLightingPass->Init(m_ShaderFactory);

    m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);

    {
        TemporalAntiAliasingPass::CreateParameters taaParams;
        taaParams.sourceDepth = m_RenderTargets->Depth;
        taaParams.motionVectors = m_RenderTargets->MotionVectors;
        taaParams.unresolvedColor = m_RenderTargets->HdrColor;
        taaParams.resolvedColor = m_RenderTargets->AAResolvedColor;
        taaParams.feedback1 = m_RenderTargets->TemporalFeedback1;
        taaParams.feedback2 = m_RenderTargets->TemporalFeedback2;
        taaParams.motionVectorStencilMask = motionVectorStencilMask;
        taaParams.useCatmullRomFilter = true;

        m_TemporalAntiAliasingPass = std::make_unique<TemporalAntiAliasingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, *m_View, taaParams);
    }

    m_SsaoPass = std::make_unique<SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->Depth, m_RenderTargets->GBufferNormals, m_RenderTargets->AmbientOcclusion); //DIFFERENCE
    nvrhi::BufferHandle exposureBuffer = nullptr;
    if (m_ToneMappingPass)
        exposureBuffer = m_ToneMappingPass->GetExposureBuffer();
    else
        exposureResetRequired = true;

    m_BloomPass = std::make_unique<BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->HdrFramebuffer, *m_TonemappingView);

    ToneMappingPass::CreateParameters toneMappingParams;
    toneMappingParams.exposureBufferOverride = exposureBuffer;
    m_ToneMappingPass = std::make_unique<ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, *m_TonemappingView, toneMappingParams);

    m_PreviousViewsValid = false;
}

void MultiViewportApp::RenderScene(nvrhi::IFramebuffer* framebuffer)
{
    sl::Extent nullExtent{};
    uint32_t nViewports = (uint32_t)m_ui.BackBufferExtents.size();
    nViewports = std::max(1u, nViewports); // can't have 0 viewports
    for (uint32_t uV = 0; uV < nViewports; ++uV)
    {
        bool isExtentValid = uV < m_ui.BackBufferExtents.size() && m_ui.BackBufferExtents[uV].width > 0 && m_ui.BackBufferExtents[uV].height > 0;

        if (!isExtentValid && uV > 0)
        {
            // remove invalid viewport
            m_pViewports.erase(m_pViewports.begin() + uV);
            --uV;
            continue;
        }

        // if we don't have this viewport - create it
        if (uV >= m_pViewports.size())
        {
            m_pViewports.push_back(this->createViewport());
        }

        m_pViewports[uV]->m_pSample->SetBackBufferExtent(isExtentValid ? m_ui.BackBufferExtents[uV] : nullExtent);
        m_pViewports[uV]->m_pSample->RenderScene(framebuffer);
    }
    // erase all unused viewports
    m_pViewports.resize(nViewports);
}

void StreamlineSample::RenderScene(nvrhi::IFramebuffer* framebuffer)
{

    // INITIALISE

    int windowWidth, windowHeight;
    GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
    nvrhi::Viewport windowViewport = nvrhi::Viewport((float)windowWidth, (float)windowHeight);

    m_Scene->RefreshSceneGraph(GetFrameIndex());

    bool exposureResetRequired = false;
    bool needNewPasses = false;

    uint32_t backbufferWidth = framebuffer->getFramebufferInfo().width;
    uint32_t backbufferHeight = framebuffer->getFramebufferInfo().height;

    // Validate resource extent against full resource size.
    auto isViewportExtentValid = [](const sl::Extent& resourceExtent, uint32_t resourceWidth, uint32_t resourceHeight, const std::string& resourceExtentSrc) -> bool
    {   
        bool validExtent = true;
        std::stringstream errorMsg{};
        errorMsg << "Invalid viewport extent input from " << resourceExtentSrc << ", IF optionally specified by the user! Ignoring it.";

        if (resourceExtent.width == 0 || resourceExtent.height == 0)
        {
            errorMsg << "One of the extent dimensions (" << resourceWidth << " x " << resourceHeight << ") is incorrectly zero.";
            validExtent = false;
        }
        else if (resourceExtent.width > resourceWidth || resourceExtent.height > resourceHeight)
        {
            errorMsg << "Extent size (" << resourceExtent.width << " x " << resourceExtent.height << ") exceeds full resource size (" << resourceWidth << " x " << resourceHeight << ").";
            validExtent = false;
        }
        if (resourceExtent.left >= resourceWidth || resourceExtent.top >= resourceHeight)
        {
            errorMsg << "Extent's base offset (" << resourceExtent.left << ", " << resourceExtent.top << ") is >= either of the resource's dimensions ("
                << resourceWidth << " x " << resourceHeight << ").";
            validExtent = false;
        }
        else if ((resourceExtent.left + resourceExtent.width - 1) >= resourceWidth || (resourceExtent.top + resourceExtent.height - 1) >= resourceHeight)
        {
            errorMsg << "Extent region (" << resourceExtent.left << ", " << resourceExtent.top << ", " << resourceExtent.width << " x "
                << resourceExtent.height << ") overflows full resource size (" << resourceWidth << " x " << resourceHeight << ").";
            validExtent = false;
        }

        if (validExtent)
        {
            log::info("Using viewport extent: ( %d, %d, %d x %d )", resourceExtent.left, resourceExtent.top, resourceExtent.width, resourceExtent.height);
        }
        else
        {
            log::warning(errorMsg.str().c_str());
        }

        return validExtent;
    };

    sl::Extent nullExtent{};
    bool validViewportExtent = (m_backbufferViewportExtent != nullExtent);
    if (validViewportExtent)
    {
        m_DisplaySize = int2(m_backbufferViewportExtent.width, m_backbufferViewportExtent.height);
    }
    else
    {
        m_DisplaySize = int2(windowWidth, windowHeight);
    }

    SLWrapper::Get().SetViewportHandle(m_viewport);

    float lodBias = 0.f;

    // RESIZE (from ui)

    if (m_ui.Resolution_changed) {
        glfwSetWindowSize(GetDeviceManager()->GetWindow(), m_ui.Resolution.x, m_ui.Resolution.y);
        m_ui.Resolution_changed = false;
    }
    else {
        m_ui.Resolution.x = windowWidth;
        m_ui.Resolution.y = windowHeight;
    }

    // DeepDVC VRAM Usage
    SLWrapper::Get().QueryDeepDVCState(m_ui.DeepDVC_VRAM);

    // DLSS-G Setup

    // Query whether SLWrapper thinks that DLSS-FG is wanted
    bool prevDlssgWanted;
    SLWrapper::Get().Get_DLSSG_SwapChainRecreation(prevDlssgWanted);

    // Query whether the UI "wants" DLSS-FG to be active
    bool dlssgWanted = (m_ui.DLSSG_mode != sl::DLSSGMode::eOff);

    // If there is a change, trigger a swapchain recreation
    if (prevDlssgWanted != dlssgWanted)
    {
        SLWrapper::Get().Set_DLSSG_SwapChainRecreation(dlssgWanted);
    }

    // This is where DLSS-G is toggled On and Off (using dlssgConst.mode) and where we set DLSS-G parameters.  
    auto dlssgConst = sl::DLSSGOptions{};
    dlssgConst.mode = m_ui.DLSSG_mode;

    // Explicitly manage DLSS-G resources in order to prevent stutter when
    // temporarily disabled.
    dlssgConst.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;

    // Turn off DLSSG if we are changing the UI
    if (m_ui.MouseOverUI) {
        dlssgConst.mode = sl::DLSSGMode::eOff;
    }

    if (m_ui.DLSS_Resolution_Mode == RenderingResolutionMode::DYNAMIC)
    {
        dlssgConst.flags |= sl::DLSSGFlags::eDynamicResolutionEnabled;
        dlssgConst.dynamicResWidth = m_DisplaySize.x / 2;
        dlssgConst.dynamicResHeight = m_DisplaySize.y / 2;
    }

    // This is where we query DLSS-G minimum swapchain size
    uint64_t estimatedVramUsage;
    sl::DLSSGStatus status;
    int fps_multiplier;
    int minSize = 0;
    SLWrapper::Get().QueryDLSSGState(estimatedVramUsage, fps_multiplier, status, minSize);

    if (framebuffer->getFramebufferInfo().width < minSize || framebuffer->getFramebufferInfo().height < minSize) {
        donut::log::info("Swapchain is too small. DLSSG is disabled.");
        dlssgConst.mode = sl::DLSSGMode::eOff;
    }

    SLWrapper::Get().SetDLSSGOptions(dlssgConst);

    // This is where we query DLSS-G FPS, estimated VRAM usage and status
    SLWrapper::Get().QueryDLSSGState(estimatedVramUsage, fps_multiplier, status, minSize);
    m_ui.DLSSG_fps = fps_multiplier * 1.0/GetDeviceManager()->GetAverageFrameTimeSeconds();

    if (status != sl::DLSSGStatus::eOk) {
        if (status == sl::DLSSGStatus::eFailResolutionTooLow)
            m_ui.DLSSG_status = "Resolution Too Low";
        else if (status == sl::DLSSGStatus::eFailReflexNotDetectedAtRuntime)
            m_ui.DLSSG_status = "Reflex Not Detected";
        else if (status == sl::DLSSGStatus::eFailHDRFormatNotSupported)
            m_ui.DLSSG_status = "HDR Format Not Supported";
        else if (status == sl::DLSSGStatus::eFailCommonConstantsInvalid)
            m_ui.DLSSG_status = "Common Constants Invalid";
        else if (status == sl::DLSSGStatus::eFailGetCurrentBackBufferIndexNotCalled)
            m_ui.DLSSG_status = "Common Constants Invalid";
        log::warning("Encountered DLSSG State Error: ", m_ui.DLSSG_status.c_str());
    }
    else {
        m_ui.DLSSG_status = "";
    }

    // After we've actually set DLSS-G on/off, free resources
    if (m_ui.DLSSG_cleanup_needed)
    {
        SLWrapper::Get().CleanupDLSSG(false);
        m_ui.DLSSG_cleanup_needed = false;
    }




    // REFLEX Setup

    auto reflexConst = sl::ReflexOptions{};
    reflexConst.mode = (sl::ReflexMode) m_ui.REFLEX_Mode;
    reflexConst.useMarkersToOptimize = true;
    reflexConst.virtualKey = VK_F13;
    reflexConst.frameLimitUs = m_ui.REFLEX_CapedFPS==0 ? 0 : int(1000000./m_ui.REFLEX_CapedFPS);
    SLWrapper::Get().SetReflexConsts(reflexConst);

    bool flashIndicatorDriverAvailable;
    SLWrapper::Get().QueryReflexStats(m_ui.REFLEX_LowLatencyAvailable, flashIndicatorDriverAvailable, m_ui.REFLEX_Stats);
    SLWrapper::Get().SetReflexFlashIndicator(flashIndicatorDriverAvailable);

    // DLSS SETUP

    //Make sure DLSS is available
    if (m_ui.AAMode == AntiAliasingMode::DLSS && !SLWrapper::Get().GetDLSSAvailable())
    {
        log::warning("DLSS antialiasing is not available. Switching to TAA. ");
        m_ui.AAMode = AntiAliasingMode::TEMPORAL;
    }

    // Reset DLSS vars if we stop using it
    if (m_ui.DLSS_Last_AA == AntiAliasingMode::DLSS && m_ui.AAMode != AntiAliasingMode::DLSS) {
        DLSS_Last_Mode = sl::DLSSMode::eOff;
        m_ui.DLSS_Mode = sl::DLSSMode::eOff;
        m_ui.DLSS_Last_DisplaySize = { 0,0 };
        SLWrapper::Get().CleanupDLSS(true); // We can also expressly tell SL to cleanup DLSS resources.
    }
    // If we turn on DLSS then we set its default values
    else if (m_ui.DLSS_Last_AA != AntiAliasingMode::DLSS && m_ui.AAMode == AntiAliasingMode::DLSS) {
        DLSS_Last_Mode = sl::DLSSMode::eBalanced;
        m_ui.DLSS_Mode = sl::DLSSMode::eBalanced;
        m_ui.DLSS_Last_DisplaySize = { 0,0 };
    }
    m_ui.DLSS_Last_AA = m_ui.AAMode;

    // If we are using DLSS set its constants
    if ((m_ui.AAMode == AntiAliasingMode::DLSS && m_ui.DLSS_Mode != sl::DLSSMode::eOff))
    {
        sl::DLSSOptions dlssConstants = {};
        dlssConstants.mode = m_ui.DLSS_Mode;
        dlssConstants.outputWidth = m_DisplaySize.x;
        dlssConstants.outputHeight = m_DisplaySize.y;
        dlssConstants.colorBuffersHDR = sl::Boolean::eTrue;
        dlssConstants.sharpness = m_RecommendedDLSSSettings.sharpness;

        if (m_ui.DLSSPresetsAnyNonDefault())
        {
            dlssConstants.dlaaPreset = m_ui.DLSS_presets[static_cast<int>(sl::DLSSMode::eDLAA)];
            dlssConstants.qualityPreset = m_ui.DLSS_presets[static_cast<int>(sl::DLSSMode::eMaxQuality)];
            dlssConstants.balancedPreset = m_ui.DLSS_presets[static_cast<int>(sl::DLSSMode::eBalanced)];
            dlssConstants.performancePreset = m_ui.DLSS_presets[static_cast<int>(sl::DLSSMode::eMaxPerformance)];
            dlssConstants.ultraPerformancePreset = m_ui.DLSS_presets[static_cast<int>(sl::DLSSMode::eUltraPerformance)];
        }

        dlssConstants.useAutoExposure = sl::Boolean::eFalse;

        // Changing presets requires a restart of DLSS
        if (m_ui.DLSSPresetsChanged())
            SLWrapper::Get().CleanupDLSS(true);

        m_ui.DLSSPresetsUpdate();

        SLWrapper::Get().SetDLSSOptions(dlssConstants);

        // Check if we need to update the rendertarget size.
        bool DLSS_resizeRequired = (m_ui.DLSS_Mode != DLSS_Last_Mode) || (m_DisplaySize.x != m_ui.DLSS_Last_DisplaySize.x) || (m_DisplaySize.y != m_ui.DLSS_Last_DisplaySize.y);
        if (DLSS_resizeRequired) {
            // Only quality, target width and height matter here
            SLWrapper::Get().QueryDLSSOptimalSettings(m_RecommendedDLSSSettings);

            if (m_RecommendedDLSSSettings.optimalRenderSize.x <= 0 || m_RecommendedDLSSSettings.optimalRenderSize.y <= 0) {
                m_ui.AAMode = AntiAliasingMode::NONE;
                m_ui.DLSS_Mode = sl::DLSSMode::eBalanced;
                m_RenderingRectSize = m_DisplaySize;
            }
            else {
                DLSS_Last_Mode = m_ui.DLSS_Mode;
                m_ui.DLSS_Last_DisplaySize = m_DisplaySize;
            }
        }

        // in variable ratio mode, pick a random ratio between min and max rendering resolution
        int2 maxSize = m_RecommendedDLSSSettings.maxRenderSize;
        int2 minSize = m_RecommendedDLSSSettings.minRenderSize;
        float texLodXDimension;
        if (m_ui.DLSS_Resolution_Mode == RenderingResolutionMode::DYNAMIC)
        {
            // Even if we request dynamic res, it is possible that the DLSS mode has max==min
            if (any(maxSize != minSize))
            {
                if (m_ui.DLSS_Dynamic_Res_change)
                {
                    m_ui.DLSS_Dynamic_Res_change = false;
                    std::uniform_int_distribution<int> distributionWidth(minSize.x, maxSize.x);
                    int newWidth = distributionWidth(m_Generator);

                    // Height is initially based on width and aspect
                    int newHeight = (int)(newWidth * (float)m_DisplaySize.y / (float)m_DisplaySize.x);

                    // But that height might be too small or too large for the min/max settings of the DLSS
                    // mode (in theory); skip changing the res if it is out of range.
                    // We predict this never to happen. It is more of a safety measure.
                    if (newHeight >= minSize.y && newHeight <= maxSize.y) m_RenderingRectSize = { newWidth , newHeight };
                }

                // For dynamic ratio, we want to choose the minimum rendering size
                // to select a Texture LOD that will preserve its sharpness over a large range of rendering resolution.
                // Ideally, the texture LOD would be allowed to be variable as well based on the dynamic scale
                // but we don't support that here yet.
                texLodXDimension = (float)minSize.x;

                // If the OUTPUT buffer resized or the DLSS mode changed, we need to recreate passes in dynamic mode.
                // In fixed resolution DLSS, this just happens when we change DLSS mode because it causes one of the
                // other cases below to hit (likely texLod).
                if (DLSS_resizeRequired) needNewPasses = true;
            }
            else
            {
                m_RenderingRectSize = maxSize;
                texLodXDimension = (float)m_RenderingRectSize.x;
            }
        }
        else if (m_ui.AAMode == AntiAliasingMode::DLSS)
        {
            m_RenderingRectSize = m_RecommendedDLSSSettings.optimalRenderSize;
            texLodXDimension = (float)m_RenderingRectSize.x;
        }

        // Use the formula of the DLSS programming guide for the Texture LOD Bias...
        lodBias = std::log2f(texLodXDimension/m_DisplaySize.x) - 1;
    }
    else {
        sl::DLSSOptions dlssConstants = {};
        dlssConstants.mode = sl::DLSSMode::eOff;
        SLWrapper::Get().SetDLSSOptions(dlssConstants);
        m_RenderingRectSize = m_DisplaySize;
    }

    // PASS SETUP
    {
        bool needNewPasses = false;

        // Here, we intentionally leave the renderTargets oversized: (displaySize, displaySize) instead of (m_RenderingRectSize, displaySize), to show the power of sl::Extent
        bool useFullSizeRenderingBuffers = m_ui.DLSS_always_use_extents || (m_ui.DLSS_Resolution_Mode == RenderingResolutionMode::DYNAMIC);

        donut::math::int2 renderSize = useFullSizeRenderingBuffers ? m_DisplaySize : m_RenderingRectSize;

        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(renderSize, m_DisplaySize))
        {
            m_BindingCache.Clear();

            m_RenderTargets = nullptr;
            m_RenderTargets = std::make_unique<RenderTargets>();
            m_RenderTargets->Init(GetDevice(), renderSize, m_DisplaySize, framebuffer->getDesc().colorAttachments[0].texture->getDesc().format);

            needNewPasses = true;
        }

        // Render scene, change bias
        if (m_ui.DLSS_lodbias_useoveride) lodBias = m_ui.DLSS_lodbias_overide;
        if (m_PreviousLodBias != lodBias)
        {
            needNewPasses = true;
            m_PreviousLodBias = lodBias;
        }

        if (SetupView())
        {
            needNewPasses = true;
        }

        if (needNewPasses)
        {
            CreateRenderPasses(exposureResetRequired, lodBias);
        }

    }

    // BEGIN COMMAND LIST
    m_CommandList->open();

    // DO RESETS
    m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());

    m_RenderTargets->Clear(m_CommandList);

    nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
    // only the very first viewport needs to clear the framebuffer
    if (m_viewport == 0)
    {
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }

    if (exposureResetRequired)
        m_ToneMappingPass->ResetExposure(m_CommandList, 8.f);

    m_AmbientTop = m_ui.AmbientIntensity * m_ui.SkyParams.skyColor * m_ui.SkyParams.brightness;
    m_AmbientBottom = m_ui.AmbientIntensity * m_ui.SkyParams.groundColor * m_ui.SkyParams.brightness;

    // SHADOW PASS
    if (m_ui.EnableShadows)
    {
        m_SunLight->shadowMap = m_ShadowMap;
        box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();

        frustum projectionFrustum = m_View->GetProjectionFrustum();
        projectionFrustum = projectionFrustum.grow(1.f); // to prevent volumetric light leaking
        const float maxShadowDistance = 100.f;

        dm::affine3 viewMatrixInv = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix();

        float zRange = length(sceneBounds.diagonal()) * 0.5f;
        m_ShadowMap->SetupForPlanarViewStable(*m_SunLight, projectionFrustum, viewMatrixInv, maxShadowDistance, zRange, zRange, m_ui.CsmExponent);

        m_ShadowMap->Clear(m_CommandList);

        DepthPass::Context context;

        RenderCompositeView(m_CommandList,
            &m_ShadowMap->GetView(), nullptr,
            *m_ShadowFramebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *m_ShadowDepthPass,
            context,
            "ShadowMap");
    }
    else
    {
        m_SunLight->shadowMap = nullptr;
    }

    // Do CPU Load
    if (m_ui.CpuLoad != 0) {
        auto start = std::chrono::high_resolution_clock::now();
        while ((std::chrono::high_resolution_clock::now() - start).count() / 1e6 < m_ui.CpuLoad);
    }

    // Deffered Shading
    {
        // DO GBUFFER
        GBufferFillPass::Context gbufferContext;

        for (auto i = 0; i <= m_ui.GpuLoad; ++i) {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill");
        }

        // DO MOTION VECTORS
        if (m_PreviousViewsValid) m_TemporalAntiAliasingPass->RenderMotionVectors(m_CommandList, *m_View, *m_ViewPrevious);

        // DO SSAO
        nvrhi::ITexture* ambientOcclusionTarget = nullptr;
        if (m_ui.EnableSsao && m_SsaoPass)
        {
            m_SsaoPass->Render(m_CommandList, m_ui.SsaoParams, *m_View);
            ambientOcclusionTarget = m_RenderTargets->AmbientOcclusion;
        }

        // DO DEFFERED
        DeferredLightingPass::Inputs deferredInputs;
        deferredInputs.SetGBuffer(*m_RenderTargets);
        deferredInputs.ambientOcclusion = m_ui.EnableSsao ? m_RenderTargets->AmbientOcclusion : nullptr;
        deferredInputs.ambientColorTop = m_AmbientTop;
        deferredInputs.ambientColorBottom = m_AmbientBottom;
        deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
        deferredInputs.output = m_RenderTargets->HdrColor;

        m_DeferredLightingPass->Render(m_CommandList, *m_View, deferredInputs);
    }

    if (m_ui.EnableProceduralSky)
        m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);

    // DO BLOOM
    if (m_ui.EnableBloom) m_BloomPass->Render(m_CommandList, m_RenderTargets->HdrFramebuffer, *m_View, m_RenderTargets->HdrColor, m_ui.BloomSigma, m_ui.BloomAlpha);

    // SET STREAMLINE CONSTANTS
    {
        // This section of code updates the streamline constants every frame. Regardless of whether we are utilising the streamline plugins, as long as streamline is in use, we must set its constants.

        constexpr float zNear = 0.1f;
        constexpr float zFar = 200.f;

        affine3 viewReprojection = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix() * m_ViewPrevious->GetViewMatrix();
        float4x4 reprojectionMatrix = inverse(m_View->GetProjectionMatrix(false)) * affineToHomogeneous(viewReprojection) * m_ViewPrevious->GetProjectionMatrix(false);
        float aspectRatio = float(m_RenderingRectSize.x) / float(m_RenderingRectSize.y);
        float4x4 projection = perspProjD3DStyleReverse(dm::radians(m_CameraVerticalFov), aspectRatio, zNear);

        float2 jitterOffset = std::dynamic_pointer_cast<PlanarView, IView>(m_View)->GetPixelOffset();

        sl::Constants slConstants = {};
        slConstants.cameraAspectRatio = aspectRatio;
        slConstants.cameraFOV = dm::radians(m_CameraVerticalFov);
        slConstants.cameraFar = zFar;
        slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
        slConstants.cameraNear = zNear;
        slConstants.cameraPinholeOffset = { 0.f, 0.f };
        slConstants.cameraPos = make_sl_float3(m_FirstPersonCamera.GetPosition());
        slConstants.cameraFwd = make_sl_float3(m_FirstPersonCamera.GetDir());
        slConstants.cameraUp = make_sl_float3(m_FirstPersonCamera.GetUp());
        slConstants.cameraRight = make_sl_float3(normalize(cross(m_FirstPersonCamera.GetDir(), m_FirstPersonCamera.GetUp())));
        slConstants.cameraViewToClip = make_sl_float4x4(projection);
        slConstants.clipToCameraView = make_sl_float4x4(inverse(projection));
        slConstants.clipToPrevClip = make_sl_float4x4(reprojectionMatrix);
        slConstants.depthInverted = m_View->IsReverseDepth() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        slConstants.jitterOffset = make_sl_float2(jitterOffset);
        slConstants.mvecScale = { 1.0f / m_RenderingRectSize.x , 1.0f / m_RenderingRectSize.y }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
        slConstants.prevClipToClip = make_sl_float4x4(inverse(reprojectionMatrix));
        slConstants.reset = needNewPasses ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        slConstants.motionVectors3D = sl::Boolean::eFalse;
        slConstants.motionVectorsInvalidValue = FLT_MIN;

        SLWrapper::Get().SetSLConsts(slConstants);
    }

    // TAG STREAMLINE RESOURCES
    SLWrapper::Get().TagResources_General(m_CommandList,
        m_View->GetChildView(ViewType::PLANAR, 0),
        m_RenderTargets->MotionVectors,
        m_RenderTargets->Depth,
        m_RenderTargets->PreUIColor);

    // ANTI-ALIASING

    // TAG STREAMLINE RESOURCES
    SLWrapper::Get().TagResources_DLSS_NIS(m_CommandList,
        m_View->GetChildView(ViewType::PLANAR, 0),
        m_RenderTargets->AAResolvedColor,
        m_RenderTargets->HdrColor);

    if (m_ui.AAMode != AntiAliasingMode::NONE) {

        // DO DLSS

        if (m_ui.AAMode == AntiAliasingMode::DLSS && !m_ui.DLSS_DebugShowFullRenderingBuffer)
        {
            SLWrapper::Get().EvaluateDLSS(m_CommandList);
        }

        if (m_ui.AAMode == AntiAliasingMode::DLSS && m_ui.DLSS_DebugShowFullRenderingBuffer) {
            m_CommonPasses->BlitTexture(m_CommandList, m_RenderTargets->AAResolvedFramebuffer->GetFramebuffer(*m_View), m_RenderTargets->HdrColor, &m_BindingCache);
            m_PreviousViewsValid = false;
        }

        // DO TAA
        if (m_ui.AAMode == AntiAliasingMode::TEMPORAL)
        {
            m_TemporalAntiAliasingPass->TemporalResolve(m_CommandList, m_ui.TemporalAntiAliasingParams, m_PreviousViewsValid, *m_View, m_PreviousViewsValid ? *m_ViewPrevious : *m_View);
        }

        m_PreviousViewsValid = true;
    }
    else
    {
        // IF YOU DO NOTHING SPECIAL -> FORWARD TEXTURE
        m_CommonPasses->BlitTexture(m_CommandList, m_RenderTargets->AAResolvedFramebuffer->GetFramebuffer(*m_View), m_RenderTargets->HdrColor, &m_BindingCache);
        m_PreviousViewsValid = false;
    }

    //DO TONEMAPPING
    nvrhi::ITexture* texToDisplay;
    if (m_ui.EnableToneMapping)
    {
        auto toneMappingParams = m_ui.ToneMappingParams;
        if (exposureResetRequired)
        {
            toneMappingParams.minAdaptedLuminance = 0.1f;
            toneMappingParams.eyeAdaptationSpeedDown = 0.0f;
        }
        m_ToneMappingPass->SimpleRender(m_CommandList, toneMappingParams, *m_TonemappingView, m_RenderTargets->AAResolvedColor);

        m_CommandList->copyTexture(m_RenderTargets->ColorspaceCorrectionColor, nvrhi::TextureSlice(), m_RenderTargets->LdrColor, nvrhi::TextureSlice());
        texToDisplay = m_RenderTargets->ColorspaceCorrectionColor;
    }
    else {
        texToDisplay = m_RenderTargets->AAResolvedColor;
    }

    // MOVE TO PPRE_UI
    m_CommonPasses->BlitTexture(m_CommandList, m_RenderTargets->PreUIFramebuffer->GetFramebuffer(*m_View), texToDisplay, &m_BindingCache);

    //
    // DO NIS
    //
    if (m_ui.NIS_Mode != sl::NISMode::eOff) {

        // NIS SETUP
        auto nisConsts = sl::NISOptions{};
        nisConsts.mode = m_ui.NIS_Mode;
        nisConsts.sharpness = m_ui.NIS_Sharpness;
        SLWrapper::Get().SetNISOptions(nisConsts);

        // Use PreUI Color
        m_CommandList->copyTexture(m_RenderTargets->NisColor, nvrhi::TextureSlice(), m_RenderTargets->PreUIColor, nvrhi::TextureSlice());

        // TAG STREAMLINE RESOURCES
        SLWrapper::Get().TagResources_DLSS_NIS(m_CommandList,
            m_View->GetChildView(ViewType::PLANAR, 0),
            m_RenderTargets->PreUIColor,
            m_RenderTargets->NisColor);

        SLWrapper::Get().EvaluateNIS(m_CommandList);
    }

    // TAG STREAMLINE RESOURCES
    SLWrapper::Get().TagResources_DLSS_FG(m_CommandList, validViewportExtent, m_backbufferViewportExtent);

    //
    // DO DEEPDVC
    //
    if (m_ui.DeepDVC_Mode != sl::DeepDVCMode::eOff) {
        // DeepDVC SETUP
        auto deepdvcConsts = sl::DeepDVCOptions{};
        deepdvcConsts.mode = m_ui.DeepDVC_Mode;
        deepdvcConsts.intensity = m_ui.DeepDVC_Intensity;
        deepdvcConsts.saturationBoost = m_ui.DeepDVC_SaturationBoost;
        SLWrapper::Get().SetDeepDVCOptions(deepdvcConsts);

        // TAG STREAMLINE RESOURCES
        SLWrapper::Get().TagResources_DeepDVC(m_CommandList,
            m_View->GetChildView(ViewType::PLANAR, 0),
            m_RenderTargets->PreUIColor);
        SLWrapper::Get().EvaluateDeepDVC(m_CommandList);
    }

    if (validViewportExtent)
    {
        // blit to target framebuffer viewport
        nvrhi::Viewport backBufferViewport
        {
            static_cast<float>(m_backbufferViewportExtent.left),
            static_cast<float>(m_backbufferViewportExtent.left + m_backbufferViewportExtent.width - 1),
            static_cast<float>(m_backbufferViewportExtent.top),
            static_cast<float>(m_backbufferViewportExtent.top + m_backbufferViewportExtent.height - 1),
            0.f,
            1.f
        };
        engine::BlitParameters blitParams{};
        blitParams.targetFramebuffer = framebuffer;
        blitParams.targetViewport = backBufferViewport;
        blitParams.sourceTexture = m_RenderTargets->PreUIColor;

        m_CommonPasses->BlitTexture(m_CommandList, blitParams, &m_BindingCache);
    }
    else
    {
        // Copy to framebuffer
        m_CommandList->copyTexture(framebufferTexture, nvrhi::TextureSlice(), m_RenderTargets->PreUIColor, nvrhi::TextureSlice());
    }

    // DEBUG OVERLAY
    if (m_ui.VisualiseBuffers) {

        static constexpr int SubWindowNumber = 2;
        static constexpr float SubWindowSpacing = 5.f;

        // If we want to, we can overlay the other textures onto the screen for comparative inspection
        auto displayDebugPiP = [&](nvrhi::TextureHandle texture, int2 pos, float scale) {
            // This snippet is by Manuel Kraemer

            dm::float2 size = dm::float2(float(windowWidth), float(windowHeight - 2.f * SubWindowSpacing)) * scale;

            nvrhi::Viewport viewport = nvrhi::Viewport(
                SubWindowSpacing * (pos.x + 1) + size.x * pos.x,
                SubWindowSpacing * (pos.x + 1) + size.x * (pos.x + 1),
                windowViewport.maxY - SubWindowSpacing * (pos.y + 1) - size.y * (pos.y + 1),
                windowViewport.maxY - SubWindowSpacing * (pos.y + 1) - size.y * pos.y, 0.f, 1.f
            );

            engine::BlitParameters blitParams;
            blitParams.targetFramebuffer = framebuffer;
            blitParams.targetViewport = viewport;
            blitParams.sourceTexture = texture;
            m_CommonPasses->BlitTexture(m_CommandList, blitParams, &m_BindingCache);
        };

        int counter = 0;
        displayDebugPiP(m_RenderTargets->MotionVectors, int2(counter % SubWindowNumber, counter++ / SubWindowNumber), 1 / float(SubWindowNumber));
        displayDebugPiP(m_RenderTargets->Depth, int2(counter % SubWindowNumber, counter++ / SubWindowNumber), 1 / float(SubWindowNumber));
    }

    // CLOSE COMMANDLIST
    m_CommandList->close();
    GetDevice()->executeCommandList(m_CommandList);

    // CLEANUP
    {

        m_TemporalAntiAliasingPass->AdvanceFrame();

        std::swap(m_View, m_ViewPrevious);

        m_CameraPreviousMatrix = m_FirstPersonCamera.GetWorldToViewMatrix();

        GetDeviceManager()->SetVsyncEnabled(m_ui.EnableVsync);
    }

    // CLOSE: 
    if (GetFrameIndex() == m_ScriptingConfig.maxFrames)
        glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
}

// Logistic functions 

std::shared_ptr<TextureCache> StreamlineSample::GetTextureCache()
{
    return m_TextureCache;
}

std::vector<std::string> const& StreamlineSample::GetAvailableScenes() const
{
    return m_SceneFilesAvailable;
}

std::string StreamlineSample::GetCurrentSceneName() const
{
    return m_CurrentSceneName;
}

void StreamlineSample::SetCurrentSceneName(const std::string& sceneName)
{
    if (m_CurrentSceneName == sceneName)
        return;

    m_CurrentSceneName = sceneName;

    BeginLoadingScene(m_RootFs, m_CurrentSceneName);
}

bool StreamlineSample::KeyboardUpdate(int key, int scancode, int action, int mods)
{

    if (key == GLFW_KEY_F13 && action == GLFW_PRESS) {
        // As GLFW abstracts away from Windows messages
        // We instead set the F13 as the PC_Ping key in the constants and compare against that.
        SLWrapper::Get().ReflexTriggerPcPing(GetFrameIndex());
    }
    
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        m_ui.EnableAnimations = !m_ui.EnableAnimations;
    }

    m_FirstPersonCamera.KeyboardUpdate(key, scancode, action, mods);
    return true;
}

bool StreamlineSample::MousePosUpdate(double xpos, double ypos)
{
    m_FirstPersonCamera.MousePosUpdate(xpos, ypos);
    return true;
}

bool StreamlineSample::MouseButtonUpdate(int button, int action, int mods)
{

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        SLWrapper::Get().ReflexTriggerFlash(GetFrameIndex());
    }

    m_FirstPersonCamera.MouseButtonUpdate(button, action, mods);
    return true;
}

bool StreamlineSample::MouseScrollUpdate(double xoffset, double yoffset)
{
    m_FirstPersonCamera.MouseScrollUpdate(xoffset, yoffset);
    return true;
}

void StreamlineSample::Animate(float fElapsedTimeSeconds)
{
    m_FirstPersonCamera.Animate(fElapsedTimeSeconds);

    if (m_ToneMappingPass)
        m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);

    if (IsSceneLoaded() && m_ui.EnableAnimations)
    {
        m_WallclockTime += fElapsedTimeSeconds*m_ui.AnimationSpeed;

        for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
        {
            float duration = anim->GetDuration();
            float integral;
            float animationTime = std::modf(m_WallclockTime / duration, &integral) * duration;
            (void)anim->Apply(animationTime);
        }
    }
}

void StreamlineSample::SceneUnloading()
{
    if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
    if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
    if (m_ShadowDepthPass) m_ShadowDepthPass->ResetBindingCache();
    m_BindingCache.Clear();
    m_SunLight.reset();
}

bool StreamlineSample::LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName)
{
    using namespace std::chrono;

    Scene* scene = new Scene(GetDevice(), *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

    auto startTime = high_resolution_clock::now();

    if (scene->Load(fileName))
    {
        m_Scene = std::unique_ptr<Scene>(scene);

        auto endTime = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(endTime - startTime).count();
        log::info("Scene loading time: %llu ms", duration);

        return true;
    }

    return false;
}

void StreamlineSample::SceneLoaded()
{
    Super::SceneLoaded();

    m_Scene->FinishedLoading(GetFrameIndex());

    m_WallclockTime = 0.f;
    m_PreviousViewsValid = false;

    for (auto light : m_Scene->GetSceneGraph()->GetLights())
    {
        if (light->GetLightType() == LightType_Directional)
        {
            m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
            break;
        }
    }

    if (!m_SunLight)
    {
        m_SunLight = std::make_shared<DirectionalLight>();
        m_SunLight->angularSize = 0.53f;
        m_SunLight->irradiance = 1.f;

        auto node = std::make_shared<SceneGraphNode>();
        node->SetLeaf(m_SunLight);
        m_SunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
        m_SunLight->SetName("Sun");
        m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
    }

    m_FirstPersonCamera.LookAt(float3(0.f, 1.8f, 0.f), float3(1.f, 1.8f, 0.f));
    m_CameraVerticalFov = 60.f;

}

void StreamlineSample::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer)
{
    nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
    m_CommandList->open();
    m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
    m_CommandList->close();
    GetDevice()->executeCommandList(m_CommandList);
    GetDeviceManager()->SetVsyncEnabled(true);
}

