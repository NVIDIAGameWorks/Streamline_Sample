////----------------------------------------------------------------------------------
//// File:        StreamlineSample.h
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

#pragma once

#include "SLWrapper.h"
#include "RenderTargets.h"
#include "UIData.h"
#include <random>
#include <chrono>

// From Donut
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

using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

struct ScriptingConfig {

    // Control at start behavior
    int maxFrames = -1;
    int DLSS_mode = -1;
    int Reflex_mode = -1;
    int Reflex_fpsCap = -1;
#ifdef DLSSG_ALLOWED // NDA ONLY DLSS-G DLSS_G Release
    int DLSSG_on = -1;
#endif

    ScriptingConfig(int argc, const char* const* argv)
    {

        for (int i = 1; i < argc; i++)
        {
            //MaxFrames
            if (!strcmp(argv[i], "-maxFrames"))
            {
                maxFrames = std::stoi(argv[++i]);
            }

            // DLSS
            else if (!strcmp(argv[i], "-DLSS_mode"))
            {
                DLSS_mode = std::stoi(argv[++i]);
            }

            // Reflex
            else if (!strcmp(argv[i], "-Reflex_mode"))
            {
                Reflex_mode = std::stoi(argv[++i]);
            }
            else if (!strcmp(argv[i], "-Reflex_fpsCap"))
            {
                Reflex_fpsCap = std::stoi(argv[++i]);
            }

#ifdef DLSSG_ALLOWED // NDA ONLY DLSS-G DLSS_G Release
            // DLSSG
            else if (!strcmp(argv[i], "-DLSSG_on"))
            {
                DLSSG_on = 1;
            }
#endif
        }
    }
};

class StreamlineSample : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    // Main Command queue and binding cache
    nvrhi::CommandListHandle                        m_CommandList;
    BindingCache                                    m_BindingCache;

    // Filesystem and scene
    std::shared_ptr<RootFileSystem>                 m_RootFs;
    std::vector<std::string>                        m_SceneFilesAvailable;
    std::string                                     m_CurrentSceneName;
    std::shared_ptr<Scene>				            m_Scene;
    float                                           m_WallclockTime = 0.f;
                                                    
    // Render Passes                                
    std::shared_ptr<ShaderFactory>                  m_ShaderFactory;
    std::shared_ptr<DirectionalLight>               m_SunLight;
    std::shared_ptr<CascadedShadowMap>              m_ShadowMap;
    std::shared_ptr<FramebufferFactory>             m_ShadowFramebuffer;
    std::shared_ptr<DepthPass>                      m_ShadowDepthPass;
    std::shared_ptr<InstancedOpaqueDrawStrategy>    m_OpaqueDrawStrategy;
    std::unique_ptr<GBufferFillPass>                m_GBufferPass;
    std::unique_ptr<DeferredLightingPass>           m_DeferredLightingPass;
    std::unique_ptr<SkyPass>                        m_SkyPass;
    std::unique_ptr<TemporalAntiAliasingPass>       m_TemporalAntiAliasingPass;
    std::unique_ptr<BloomPass>                      m_BloomPass;
    std::unique_ptr<ToneMappingPass>                m_ToneMappingPass;
    std::unique_ptr<SsaoPass>                       m_SsaoPass;

    // RenderTargets
    std::unique_ptr<RenderTargets>                  m_RenderTargets;

    //Views
    std::shared_ptr<IView>                          m_View;
    bool                                            m_PreviousViewsValid = false;
    std::shared_ptr<IView>                          m_ViewPrevious;
    std::shared_ptr<IView>                          m_TonemappingView;

    // Camera
    FirstPersonCamera                               m_FirstPersonCamera;
    float                                           m_CameraVerticalFov = 60.f;

    // UI
    UIData& m_ui;
    float3                                          m_AmbientTop = 0.f;
    float3                                          m_AmbientBottom = 0.f;

    // For Streamline
    int2                                            m_RenderingRectSize = { 0, 0 };
    int2                                            m_DisplaySize;
    SLWrapper::DLSSSettings                         m_RecommendedDLSSSettings;
    std::default_random_engine                      m_Generator;
    float                                           m_PreviousLodBias;
    affine3                                         m_CameraPreviousMatrix;

    bool                                            m_presentStarted = false;

    // Scripting Behavior
    ScriptingConfig                                 m_ScriptingConfig;

public:
    StreamlineSample(DeviceManager* deviceManager, UIData& ui, const std::string& sceneName, ScriptingConfig scriptingConfig);

    // Functions of interest
    bool SetupView();
    void CreateRenderPasses(bool& exposureResetRequired, float lodBias);
    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override;

    // Logistic functions 
    std::shared_ptr<TextureCache> GetTextureCache();
    std::vector<std::string> const& GetAvailableScenes() const;
    std::string GetCurrentSceneName() const;
    void SetCurrentSceneName(const std::string& sceneName);
    std::shared_ptr<ShaderFactory> GetShaderFactory() const { return m_ShaderFactory; };
    std::shared_ptr<donut::vfs::IFileSystem> GetRootFs() const { return m_RootFs; };

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
    virtual bool MousePosUpdate(double xpos, double ypos) override;
    virtual bool MouseButtonUpdate(int button, int action, int mods) override;
    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override;
    virtual void Animate(float fElapsedTimeSeconds) override;
    virtual void SceneUnloading() override;
    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override;
    virtual void SceneLoaded() override;
    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override;

};