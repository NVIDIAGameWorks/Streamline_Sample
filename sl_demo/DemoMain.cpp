//----------------------------------------------------------------------------------
// File:        DemoMain.cpp
// SDK Version: 1.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <random>
#include <iostream>


#if !__linux__
#include <nvapi.h>
#endif

#if __linux__
#define ExitProcess exit
#endif

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/BloomPass.h>
#include <donut/render/CascadedShadowMap.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/LightProbeProcessingPass.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/render/SkyPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <nvrhi/utils.h>

// All render target resources are created in here.  Check here for formats.
#include "glue/RenderTargets.h"

#if USE_SL
#include <SLWrapper.h>
#endif

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

constexpr float OPTIMAL_RATIO  = -1.0f; // Use the scaling ratio that the DLSS recommends
constexpr size_t NUM_OFFSET_SEQUENCES = 64; // Use a large number of Halton sequence offsets to accomodate large scaling ratios.

enum class AntiAliasingMode { 
    NONE, 
    TEMPORAL, 
#ifdef USE_SL
    DLSS,
#endif
};


struct UIData
{

    bool                                ShowUI = true;
    bool                                EnableSsao = true;
    SsaoParameters                      SsaoParameters;
    ToneMappingParameters               ToneMappingParams;
    TemporalAntiAliasingParameters      TemporalAntiAliasingParams;
    bool                                EnableVsync = false;
    bool                                ShaderReloadRequested = false;
    bool                                EnableProceduralSky = true;
    bool                                EnableBloom = true;
    float                               BloomSigma = 32.f;
    bool                                EnableMaterialEvents = false;
    float                               AmbientIntensity = 0.2f;
    float                               CsmExponent = 4.f;
    std::string                         ScreenshotFileName = "";
    AntiAliasingMode                    AAMode = AntiAliasingMode::TEMPORAL;
    int2                                renderSize = { 0,0 };

#if USE_SL
    // SL specific parameters
    float                               DLSS_Sharpness = 0.f;
    bool                                DLSS_Supported = false;
    sl::DLSSMode                        DLSS_Mode = sl::DLSSMode::eDLSSModeBalanced;
    
    //For history tracking so we can only update the rendersize when needed
    int2                               DLSS_Last_DisplaySize = { 0,0 };
    sl::DLSSMode                       DLSS_Last_Mode = sl::DLSSMode::eDLSSModeOff;
    AntiAliasingMode                   DLSS_Last_AA = AntiAliasingMode::TEMPORAL;
#endif

    nvrhi::GraphicsAPI                  deviceType;
};


class FeatureDemo : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    std::unique_ptr<MediaFolder>        m_MediaFolder;
    std::string                         m_CurrentSceneName;
    std::shared_ptr<Scene>              m_Scene;
    std::shared_ptr<ShaderFactory>      m_ShaderFactory;
    std::shared_ptr<DirectionalLight>   m_SunLight;
    std::shared_ptr<CascadedShadowMap>  m_ShadowMap;
    std::shared_ptr<FramebufferFactory> m_ShadowFramebuffer;
    std::shared_ptr<DepthPass>          m_ShadowDepthPass;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::unique_ptr<RenderTargets>      m_RenderTargets;
    std::unique_ptr<GBufferFillPass>    m_GBufferPass;
    std::unique_ptr<DeferredLightingPass> m_DeferredLightingPass;
    std::unique_ptr<SkyPass>            m_SkyPass;
    std::unique_ptr<TemporalAntiAliasingPass> m_TemporalAntiAliasingPass;
    std::unique_ptr<BloomPass>          m_BloomPass;
    std::unique_ptr<ToneMappingPass>    m_ToneMappingPass;
    std::unique_ptr<SsaoPass>           m_SsaoPass;

    std::shared_ptr<IView>              m_View;
    std::shared_ptr<IView>              m_ViewPrevious;
    std::shared_ptr<IView>              m_TonemappingView;
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_GBufferBindingSets;
    nvrhi::CommandListHandle            m_CommandList;
    bool                                m_PreviousViewsValid = false;
    FPSCamera                           m_Camera;
    affine3                             m_CameraPreviousMatrix;
    degrees_f                           m_CameraVerticalFov = 60.f;
    float3                              m_AmbientTop = 0.f;
    float3                              m_AmbientBottom = 0.f;
    int                                 m_FrameIndex = 0;

#if USE_SL
    std::unique_ptr<SLWrapper>         m_SLWrapper;
#endif
    float                               m_PreviousLodBias = 0.f;
    int2                                m_PreviousRenderSize = {~0, ~0, };

    UIData&                             m_ui;

public:

    FeatureDemo(DeviceManager* deviceManager, UIData& ui)
        : Super(deviceManager)
        , m_ui(ui)
    {
        m_ui.deviceType = GetDevice()->getGraphicsAPI();

        // We do not support vulkan yet.
        if (m_ui.deviceType == nvrhi::GraphicsAPI::VULKAN) {
            log::error("Vulkan is not supported. Please use a different Graphics API.");
        }

        // Create and initialize  library
#if USE_SL
        m_SLWrapper = std::make_unique<SLWrapper>(GetDevice());
        m_ui.DLSS_Supported = m_SLWrapper->GetDLSSAvailable();
#endif

        if (m_ui.DLSS_Supported) log::info("DLSS is supported on this system.");
        else log::warning("DLSS is not supported on this system.");

        std::shared_ptr<NativeFileSystem> nativeFS = std::make_shared<NativeFileSystem>();

        std::filesystem::path mediaPath = FindMediaFolder("media/sponza.json");
        std::filesystem::path frameworkShaderPath = FindDirectoryWithShaderBin(GetDevice()->getGraphicsAPI(), *nativeFS, GetDirectoryWithExecutable(), "donut/shaders", "blit_ps");

        std::shared_ptr<RootFileSystem> rootFS = std::make_shared<RootFileSystem>();
        rootFS->mount("/media", mediaPath);
        rootFS->mount("/shaders/framework", frameworkShaderPath);

        m_MediaFolder = std::make_unique<MediaFolder>(rootFS, "/media");
        if (m_MediaFolder->GetAvailableScenes().empty())
            ExitProcess(1);

        m_TextureCache = std::make_shared<TextureCache>(GetDevice(), rootFS);

        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), rootFS, "/shaders/framework", "");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);

        m_ShadowMap = std::make_shared<CascadedShadowMap>(GetDevice(), 2048, 4, 0, nvrhi::Format::D24S8);

        m_ShadowFramebuffer = std::make_shared<FramebufferFactory>(GetDevice());
        m_ShadowFramebuffer->DepthTarget = m_ShadowMap->GetTexture();

        DepthPass::CreateParameters shadowDepthParams;
        shadowDepthParams.rasterState.slopeScaledDepthBias = 4.f;
        shadowDepthParams.rasterState.depthBias = 100;
        m_ShadowDepthPass = std::make_shared<DepthPass>(GetDevice(), m_CommonPasses);
        m_ShadowDepthPass->Init(*m_ShaderFactory, *m_ShadowFramebuffer, m_ShadowMap->GetView(), shadowDepthParams);

        m_CommandList = GetDevice()->createCommandList();

        m_Camera.SetMoveSpeed(3.0f);

        SetAsynchronousLoadingEnabled(false);
        SetCurrentSceneName("sponza.json");
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_CurrentSceneName == sceneName)
            return;

        m_CurrentSceneName = sceneName;

        BeginLoadingScene(m_MediaFolder->GetFileSystem(), m_MediaFolder->GetPath() / m_CurrentSceneName);
    }

    MediaFolder& GetMediaFolder() const
    {
        return *m_MediaFolder;
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS)
        {
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;
        }

        m_Camera.KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        m_Camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_Camera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        m_Camera.Animate(fElapsedTimeSeconds);
        if(m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(fElapsedTimeSeconds);
    }


    virtual void SceneUnloading() override
    {
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        if (m_ShadowDepthPass) m_ShadowDepthPass->ResetBindingCache();
        m_SunLight.reset();
    }

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        Scene* scene = new Scene(fs);

        if (scene->Load(fileName, VertexAttribute::ALL, *m_TextureCache))
        {
            m_Scene = std::unique_ptr<Scene>(scene);
            return true;
        }

        return false;
    }

    virtual void SceneLoaded() override
    {
        Super::SceneLoaded();

        m_Scene->CreateRenderingResources(GetDevice());

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>(*m_Scene);
        m_PreviousViewsValid = false;

        for (auto light : m_Scene->Lights)
        {
            if (light->GetLightType() == LightType::DIRECTIONAL)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                break;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<DirectionalLight>();
            m_SunLight->direction = normalize(float3(-0.05f, -1.0f, 0.1f));
            m_SunLight->angularSize = 0.53f;
            m_SunLight->irradiance = 1.f;
            m_SunLight->name = "Sun";
            m_Scene->Lights.push_back(m_SunLight);
        }

        if (m_Scene->DefaultCamera)
        {
            m_Camera.LookAt(m_Scene->DefaultCamera->position, m_Scene->DefaultCamera->lookAt, m_Scene->DefaultCamera->up);
            m_CameraVerticalFov = m_Scene->DefaultCamera->verticalFov;
        }
        else
        {
            m_Camera.LookAt(float3(0.f, 1.8f, 0.f),float3(1.f, 1.8f, 0.f));
            m_CameraVerticalFov = 60.f;
        }
    }

    std::shared_ptr<TextureCache> GetTextureCache()
    {
        return m_TextureCache;
    }

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    bool SetupView(donut::math::int2 renderSize, donut::math::int2 renderOffset, bool preTonemapping)
    {
        const float zNear = 0.1f;
        const float zFar = 200.f;
        bool topologyChanged = false;

        float2 displaySize = float2(m_RenderTargets->m_DisplaySize);

        // Always use the display aspect ratio
        // because we may render to a different rendering resolution than the display resolution
        // we still want geometry to appear similar once it is scaled up to the whole window/screen
        float aspectRatio = displaySize.x / displaySize.y;

        donut::math::float2 renderTargetSize = m_RenderTargets->m_MaximumRenderSize;

        float2 pixelOffset =  (m_ui.AAMode ==AntiAliasingMode::NONE) ? float2(0.f) : GetCurrentPixelOffset();

        nvrhi::Viewport renderViewport = nvrhi::Viewport(float(renderOffset.x), float(renderOffset.x + renderSize.x), float(renderOffset.y), float(renderOffset.y + renderSize.y), 0.0f, 1.0f);;

        {
            std::shared_ptr<PlanarView> renderPlanarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);

            if (!renderPlanarView)
            {
                m_View = renderPlanarView = std::make_shared<PlanarView>();
                m_ViewPrevious = std::make_shared<PlanarView>();
                topologyChanged = true;
            }

            std::shared_ptr<PlanarView> renderPlanarViewPrevious = std::dynamic_pointer_cast<PlanarView, IView>(m_ViewPrevious);

            float4x4 projection = perspProjD3DStyle(dm::radians(m_CameraVerticalFov), aspectRatio, zNear, zFar);

            renderPlanarView->SetViewport(renderViewport);
            renderPlanarView->SetPixelOffset(pixelOffset);

            // Also correct the ViewPrevious so that the motion vector rendering does not
            // account for the change in viewport and pixel offsets !
            renderPlanarViewPrevious->SetViewport(renderViewport);
            renderPlanarViewPrevious->SetPixelOffset(pixelOffset);
            renderPlanarViewPrevious->SetMatrices(m_CameraPreviousMatrix, projection);

            renderPlanarView->SetMatrices(m_Camera.GetWorldToViewMatrix(), projection);
        }
        {
            std::shared_ptr<PlanarView> tonemappingPlanarView = std::dynamic_pointer_cast<PlanarView, IView>(m_TonemappingView);

            if (!tonemappingPlanarView)
            {
                m_TonemappingView = tonemappingPlanarView = std::make_shared<PlanarView>();
                topologyChanged = true;
            }

            float4x4 projection = perspProjD3DStyle(dm::radians(m_CameraVerticalFov), aspectRatio, zNear, zFar);
            float2 vpSize = preTonemapping ? displaySize : renderTargetSize;

            tonemappingPlanarView->SetViewport(nvrhi::Viewport(vpSize.x, vpSize.y));
            tonemappingPlanarView->SetMatrices(m_Camera.GetWorldToViewMatrix(), projection);
        }

        return topologyChanged;
    }

    void CreateRenderPasses(donut::math::int2 creationTimeRenderSize, float lodBias, bool& exposureResetRequired)
    {
        uint32_t motionVectorStencilMask = 0x01;

        nvrhi::SamplerDesc samplerdescPoint      = m_CommonPasses->m_PointClampSampler->GetDesc();
        nvrhi::SamplerDesc samplerdescLinear     = m_CommonPasses->m_LinearClampSampler->GetDesc();
        nvrhi::SamplerDesc samplerdescLinearWrap = m_CommonPasses->m_LinearWrapSampler->GetDesc();
        nvrhi::SamplerDesc samplerdescAniso      = m_CommonPasses->m_AnisotropicWrapSampler->GetDesc();

        samplerdescPoint.mipBias      = lodBias;
        samplerdescLinear.mipBias     = lodBias;
        samplerdescLinearWrap.mipBias = lodBias;
        samplerdescAniso.mipBias      = lodBias;

        m_CommonPasses->m_PointClampSampler      = GetDevice()->createSampler(samplerdescPoint);
        m_CommonPasses->m_LinearClampSampler     = GetDevice()->createSampler(samplerdescLinear);
        m_CommonPasses->m_LinearWrapSampler      = GetDevice()->createSampler(samplerdescLinearWrap);
        m_CommonPasses->m_AnisotropicWrapSampler = GetDevice()->createSampler(samplerdescAniso);

        GBufferFillPass::CreateParameters GBufferParams;
        GBufferParams.enableMotionVectors = true;
        GBufferParams.stencilWriteMask = motionVectorStencilMask;
        m_GBufferPass = std::make_unique<GBufferFillPass>(GetDevice(), m_CommonPasses);
        m_GBufferPass->Init(*m_ShaderFactory, *m_RenderTargets->m_GBufferFramebuffer, *m_View, GBufferParams);
        m_GBufferBindingSets.clear();

        m_DeferredLightingPass = std::make_unique<DeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(*m_ShaderFactory, *m_RenderTargets->m_HdrFramebuffer, *m_View);

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->m_ForwardFramebuffer, *m_View);

        m_TemporalAntiAliasingPass = std::make_unique<TemporalAntiAliasingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, *m_View,
            m_RenderTargets->m_Depth,
            m_RenderTargets->m_MotionVectors,
            m_RenderTargets->m_HdrColor,
            m_RenderTargets->m_ResolvedColor,
            m_RenderTargets->m_TemporalFeedback1,
            m_RenderTargets->m_TemporalFeedback2,
            motionVectorStencilMask,
            true);

        m_SsaoPass = std::make_unique<SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->m_HdrFramebuffer, m_RenderTargets->m_Depth, m_RenderTargets->m_GBufferNormals, *m_View, true);

        nvrhi::TextureHandle exposureTexture = nullptr;
        if (m_ToneMappingPass)
            exposureTexture = m_ToneMappingPass->GetExposureTexture();
        else
            exposureResetRequired = true;

        m_ToneMappingPass = std::make_unique<ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->m_LdrFramebuffer, *m_TonemappingView, 256, false, exposureTexture);

        m_BloomPass = std::make_unique<BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->m_HdrFramebuffer, *m_View);

        m_PreviousViewsValid = false;
    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        nvrhi::ITexture* framebufferTexture = framebuffer->GetDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        GetDeviceManager()->SetVsyncEnabled(true);
    }

    nvrhi::IBindingSet* GetGBufferBindingSet(nvrhi::ITexture* indirectDiffuse)
    {
        nvrhi::BindingSetHandle& bindingSet = m_GBufferBindingSets[indirectDiffuse];

        if (!bindingSet)
        {
            bindingSet = m_DeferredLightingPass->CreateGBufferBindingSet(
                nvrhi::AllSubresources,
                m_RenderTargets->m_Depth,
                m_RenderTargets->m_GBufferDiffuse,
                m_RenderTargets->m_GBufferSpecular,
                m_RenderTargets->m_GBufferNormals,
                indirectDiffuse
            );
        }

        return bindingSet;
    }

    void AdvanceFrame()
    {
        m_FrameIndex = (m_FrameIndex + 1) % NUM_OFFSET_SEQUENCES;

        m_CameraPreviousMatrix = m_Camera.GetWorldToViewMatrix();
    }

    dm::float2 GetCurrentPixelOffset()
    {
        // Halton jitter
        float2 Result(0.0f, 0.0f);

        constexpr int BaseX = 2;
        int Index = m_FrameIndex + 1;
        float InvBase = 1.0f / BaseX;
        float Fraction = InvBase;
        while (Index > 0)
        {
            Result.x += (Index % BaseX) * Fraction;
            Index /= BaseX;
            Fraction *= InvBase;
        }

        constexpr int BaseY = 3;
        Index = m_FrameIndex + 1;
        InvBase = 1.0f / BaseY;
        Fraction = InvBase;
        while (Index > 0)
        {
            Result.y += (Index % BaseY) * Fraction;
            Index /= BaseY;
            Fraction *= InvBase;
        }

        Result.x -= 0.5f;
        Result.y -= 0.5f;
        return Result;
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));

        bool exposureResetRequired = false;
        bool needNewPasses  = false;

        int2 displaySize = int2(windowWidth, windowHeight);
        int2 requiredRendertargetSize = displaySize;

        int2 renderOffset   = {0, 0};
        bool preTonemapping = true;
        float lodBias = 0.f;

        // Update variables

        if ((m_ui.AAMode != AntiAliasingMode::DLSS)) {
            m_ui.renderSize = displaySize;
        }

#ifdef USE_SL

        //Make sure DLSS is available
        if (m_ui.AAMode ==AntiAliasingMode::DLSS && !m_SLWrapper->GetDLSSAvailable())
        {
            log::warning("DLSS antialiasing is not available. Switching to TAA. ");
            m_ui.AAMode =AntiAliasingMode::TEMPORAL;
        }

        // Reset DLSS vars if we stop using it
        if (m_ui.DLSS_Last_AA == AntiAliasingMode::DLSS && m_ui.AAMode != AntiAliasingMode::DLSS) {
            m_ui.DLSS_Last_Mode = sl::DLSSMode::eDLSSModeBalanced;
            m_ui.DLSS_Mode = sl::DLSSMode::eDLSSModeBalanced;
            m_ui.DLSS_Last_DisplaySize = { 0,0 };
        }
        m_ui.DLSS_Last_AA = m_ui.AAMode;

        // If we are using DLSS set its constants
        if ((m_ui.AAMode == AntiAliasingMode::DLSS))
        {

            sl::DLSSConstants dlssConstants = {};
            dlssConstants.mode = m_ui.DLSS_Mode;
            dlssConstants.outputWidth = displaySize.x;
            dlssConstants.outputHeight = displaySize.y;
            dlssConstants.colorBuffersHDR = sl::Boolean::eTrue;
            m_SLWrapper->SetDLSSConsts(dlssConstants, m_FrameIndex);

            bool DLSS_resizeRequired = (m_ui.DLSS_Mode != m_ui.DLSS_Last_Mode) || (displaySize.x != m_ui.DLSS_Last_DisplaySize.x) || (displaySize.y != m_ui.DLSS_Last_DisplaySize.y);

            // Check if we need to update the rendertarget size.
            if (DLSS_resizeRequired) {
                m_SLWrapper->QueryDLSSOptimalSettings(m_ui.renderSize, m_ui.DLSS_Sharpness);

                if (m_ui.renderSize.x <= 0 || m_ui.renderSize.y <= 0) {
                    m_ui.AAMode = AntiAliasingMode::TEMPORAL;
                    m_ui.DLSS_Mode = sl::DLSSMode::eDLSSModeBalanced;
                    m_ui.renderSize = displaySize;
                }
                else {
                    m_ui.DLSS_Last_Mode = m_ui.DLSS_Mode;
                    m_ui.DLSS_Last_DisplaySize = displaySize;
                }

                lodBias = std::log2f(float(m_ui.renderSize.x) / float(displaySize.x)) - 1.0f;
            }
            
        }
#endif

        // Setup Render passes
        {
            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(displaySize, displaySize, preTonemapping))
            {

                m_RenderTargets = nullptr;
                m_CommonPasses->ResetBindingCache();
                m_RenderTargets = std::make_unique<RenderTargets>(GetDevice(), displaySize, displaySize, preTonemapping);

                needNewPasses = true;
            }

            // Render scene, change bias
            if (m_PreviousLodBias != lodBias)
            {
                needNewPasses = true;
                m_PreviousLodBias = lodBias;
            }

            if (SetupView(m_ui.renderSize, renderOffset, preTonemapping))
            {
                needNewPasses = true;
            }

            if (m_ui.ShaderReloadRequested)
            {
                m_ShaderFactory->ClearCache();
                needNewPasses = true;
            }

            if (any(m_PreviousRenderSize != m_ui.renderSize))
            {
                m_PreviousRenderSize = m_ui.renderSize;
                needNewPasses = true;
            }

            if (needNewPasses)
            {
                CreateRenderPasses(m_ui.renderSize, lodBias, exposureResetRequired);
            }

            m_ui.ShaderReloadRequested = false;

            m_CommandList->open();
            m_ToneMappingPass->BeginTrackingState(m_CommandList);

            nvrhi::ITexture* framebufferTexture = framebuffer->GetDesc().colorAttachments[0].texture;
            m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

            DaylightInformation daylight = CalculateDaylightInformation(-m_SunLight->direction);
            m_AmbientTop = daylight.AmbientColor * m_ui.AmbientIntensity * m_SunLight->irradiance;
            m_AmbientBottom = m_AmbientTop * float3(0.3f, 0.4f, 0.3f);
            m_SunLight->color = daylight.SunColor;

        }

        // Shadow Pass
        {
            m_SunLight->shadowMap = m_ShadowMap;
            box3 sceneBounds = m_Scene->GetSceneBounds();

            frustum projectionFrustum = m_View->GetProjectionFrustum();
            projectionFrustum = projectionFrustum.grow(1.f); // to prevent volumetric light leaking
            const float maxShadowDistance = 100.f;

            dm::affine3 viewMatrixInv = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix();

            float zRange = length(sceneBounds.diagonal()) * 0.5f;
            m_ShadowMap->SetupForPlanarViewStable(*m_SunLight, projectionFrustum, viewMatrixInv, maxShadowDistance, zRange, zRange, m_ui.CsmExponent);

            m_ShadowMap->Clear(m_CommandList);

            RenderCompositeView(m_CommandList,
                &m_ShadowMap->GetView(), nullptr,
                *m_ShadowFramebuffer,
                *m_OpaqueDrawStrategy,
                *m_ShadowDepthPass,
                "ShadowMap",
                m_ui.EnableMaterialEvents);
        }

        std::vector<std::shared_ptr<LightProbe>> lightProbes; // no probes
        m_RenderTargets->Clear(m_CommandList);

        if (exposureResetRequired)
            m_ToneMappingPass->ResetExposure(m_CommandList, 8.f);

        // GBuffer Pass
        RenderCompositeView(m_CommandList, 
            m_View.get(), m_ViewPrevious.get(), 
            *m_RenderTargets->m_GBufferFramebuffer, 
            *m_OpaqueDrawStrategy, 
            *m_GBufferPass, 
            "GBufferFill",
            m_ui.EnableMaterialEvents);

        // Deffered Lighting pass
        m_DeferredLightingPass->Render(m_CommandList, *m_RenderTargets->m_HdrFramebuffer, *m_View, m_Scene->Lights,
            GetGBufferBindingSet(nullptr), m_AmbientTop, m_AmbientBottom, lightProbes);

        // Sky pass
        m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight);

        // SSAO pass
        if (m_ui.EnableSsao)
        {
            float2 randomOffset = float2(float(rand()), float(rand()));
            m_SsaoPass->Render(m_CommandList, m_ui.SsaoParameters, *m_View, randomOffset);
        }

        // Define texture handles
        nvrhi::ITexture* renderColor = m_RenderTargets->m_HdrColor;
        nvrhi::ITexture* postResolveColor;
        nvrhi::ITexture* finalTonemappedColor;

        // Bloom pass
        if (m_ui.EnableBloom)
        {
            float effectiveBloomSigma = m_ui.BloomSigma * (((float)m_ui.renderSize.x) / m_RenderTargets->m_DisplaySize.x);
            m_BloomPass->Render(m_CommandList, m_RenderTargets->m_HdrFramebuffer, *m_View, renderColor, effectiveBloomSigma);
        }

#ifdef USE_SL
        // This section of code updates the sl constants. If SL is being used, it must be done regardless of whether we use the plugins.
        {
            constexpr float zNear = 0.1f;
            constexpr float zFar = 200.f;

            affine3 viewReprojection = inverse(m_View->GetViewMatrix()) * m_ViewPrevious->GetViewMatrix();
            float4x4 reprojectionMatrix = inverse(m_View->GetProjectionMatrix(false)) * affineToHomogeneous(viewReprojection) * m_ViewPrevious->GetProjectionMatrix(false);
            float2 displaySize = float2(m_RenderTargets->m_DisplaySize);
            float aspectRatio = displaySize.x / displaySize.y;
            float4x4 projection = perspProjD3DStyle(dm::radians(m_CameraVerticalFov), aspectRatio, zNear, zFar);

            std::shared_ptr<PlanarView> renderPlanarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);
            float2 jitterOffset = renderPlanarView->GetPixelOffset();

            float renderWidth = m_View->GetViewportState().viewports[0].maxX - m_View->GetViewportState().viewports[0].minX;
            float renderHeight = m_View->GetViewportState().viewports[0].maxY - m_View->GetViewportState().viewports[0].minY;

            sl::Constants slConstants = {};
            slConstants.cameraAspectRatio = aspectRatio;
            slConstants.cameraFOV = dm::radians(m_CameraVerticalFov);
            slConstants.cameraFar = zFar;
            slConstants.cameraFwd = make_sl_float3(m_Camera.GetDir());
            slConstants.cameraMotionIncluded = sl::Boolean::eTrue;
            slConstants.cameraNear = zNear;
            slConstants.cameraPinholeOffset = { 0.f, 0.f };
            slConstants.cameraPos = make_sl_float3(m_Camera.GetPosition());
            slConstants.cameraRight = make_sl_float3(normalize(cross(m_Camera.GetDir(), m_Camera.GetUp())));
            slConstants.cameraUp = make_sl_float3(m_Camera.GetUp());
            slConstants.cameraViewToClip = make_sl_float4x4(projection);
            slConstants.clipToCameraView = make_sl_float4x4(inverse(projection));
            slConstants.clipToPrevClip = make_sl_float4x4(reprojectionMatrix);
            slConstants.depthInverted = m_View->IsReverseDepth() ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            slConstants.jitterOffset = make_sl_float2(jitterOffset);
            slConstants.mvecScale = { 1.0f / renderWidth , 1.0f / renderHeight }; // This are scale factors used to normalize mvec (to -1,1) and donut has mvec in pixel space
            slConstants.notRenderingGameFrames = sl::Boolean::eFalse;
            slConstants.prevClipToClip = make_sl_float4x4(inverse(reprojectionMatrix));
            slConstants.reset = sl::Boolean::eFalse;
            slConstants.motionVectors3D = sl::Boolean::eFalse;

            m_SLWrapper->SetSLConsts(slConstants, m_FrameIndex);
        }
#endif

        // If we do TAA or DLSS
        if (m_ui.AAMode !=AntiAliasingMode::NONE) {

            // TAA evaluation


#ifdef USE_SL
            // DLSS Evaluation
            if (m_ui.AAMode ==AntiAliasingMode::DLSS)
            {

                m_SLWrapper->EvaluateDLSS(m_CommandList,
                    m_RenderTargets->m_ResolvedColor, 
                    renderColor,
                    m_RenderTargets->m_MotionVectors,
                    m_RenderTargets->m_Depth,
                    m_FrameIndex);
            }
#endif

            if (m_ui.AAMode ==AntiAliasingMode::TEMPORAL)
            {
                if (m_PreviousViewsValid) m_TemporalAntiAliasingPass->RenderMotionVectors(m_CommandList, *m_View, *m_ViewPrevious);

                m_TemporalAntiAliasingPass->TemporalResolve(m_CommandList, m_ui.TemporalAntiAliasingParams, m_PreviousViewsValid, *m_View, m_PreviousViewsValid ? *m_ViewPrevious : *m_View);
            }

            postResolveColor = m_RenderTargets->m_ResolvedColor;
            m_PreviousViewsValid = true;
        }
        else // If we do nothing, the we just forward the buffer
        {
            postResolveColor = renderColor;
            m_PreviousViewsValid = false;
        }


        // Tonemapping
        {
            m_ui.ToneMappingParams.minAdaptedLuminance = 0.1f;
            m_ui.ToneMappingParams.maxAdaptedLuminance = m_ui.ToneMappingParams.minAdaptedLuminance;
            m_ui.ToneMappingParams.eyeAdaptationSpeedDown = 0.0f;
            m_ui.ToneMappingParams.eyeAdaptationSpeedUp = m_ui.ToneMappingParams.eyeAdaptationSpeedDown;

            m_ToneMappingPass->SimpleRender(m_CommandList, m_ui.ToneMappingParams, *m_TonemappingView, postResolveColor);
            finalTonemappedColor = m_RenderTargets->m_LdrColor;
        }

        // Blit to output
        m_CommonPasses->BlitTexture(m_CommandList, framebuffer, windowViewport, box2(0.f, 1.f), finalTonemappedColor, box2(0.f, 1.f));

        // Cleanup
        {
            m_ToneMappingPass->SaveCurrentState(m_CommandList);

            m_CommandList->close();
            GetDevice()->executeCommandList(m_CommandList);

            m_TemporalAntiAliasingPass->AdvanceFrame();

            AdvanceFrame();

            GetDeviceManager()->SetVsyncEnabled(m_ui.EnableVsync); 
        }
        
    }

    std::shared_ptr<ShaderFactory> GetShaderFactory()
    {
        return m_ShaderFactory;
    }
};

// Just some UX glue code.  Go here to add a button or muck with the UX controls.
#include "glue/UIRenderer.h"

bool ProcessCommandLine(DeviceCreationParameters& deviceParams, nvrhi::GraphicsAPI api)
{
    LPWSTR *szArglist;
    int nArgs;
    int i;

    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (!szArglist)
    {
        return false;
    }
    else
    {
        for (i = 0; i < nArgs; i++)
        {
            if (!lstrcmpW(szArglist[i], L"-width"))
            {
                deviceParams.backBufferWidth = std::stoi(szArglist[++i]);
            }
            else if (!lstrcmpW(szArglist[i], L"-height"))
            {
                deviceParams.backBufferHeight = std::stoi(szArglist[++i]);
            }
            else if (!lstrcmpW(szArglist[i], L"-fullscreen"))
            {
                deviceParams.startFullscreen = true;
            }
        }
    }

    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);

    DeviceCreationParameters deviceParams;
    UIData uiData;

    NvAPI_Initialize();

    deviceParams.backBufferWidth = 2560;
    deviceParams.backBufferHeight = 1440;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 2;
    deviceParams.startFullscreen = false;
    deviceParams.enablePerMonitorDPI = true;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif
    deviceParams.vsyncEnabled = true;

    if (!ProcessCommandLine(deviceParams, api))
    {
        log::error("Failed to process the command line.");
        return 1;
    }

    DeviceManager* deviceManager = DeviceManager::Create(api);
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "NVIDIA SL DLSS Sample (" + std::string(apiString) + ")";

#ifdef USE_SL
    SLWrapper::Initialize();
#endif

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
    {
        log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
        return 1;
    }

    {
        std::shared_ptr<FeatureDemo> demo = std::make_shared<FeatureDemo>(deviceManager, uiData);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, demo, uiData);

        gui->LoadFont(*demo->GetMediaFolder().GetFileSystem(), demo->GetMediaFolder().GetPath() / "OpenSansFont/OpenSans-Regular.ttf", 17.f);
        gui->Init(demo->GetShaderFactory());

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();

#ifdef _DEBUG
    deviceManager->ReportLiveObjects();
#endif
    delete deviceManager;

#ifdef USE_SL
    SLWrapper::Shutdown();
#endif

    
    return 0;
}
