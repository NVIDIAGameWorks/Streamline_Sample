//----------------------------------------------------------------------------------
// File:        UIData.h
// SDK Version: 2.0
// Email:       StreamlineSupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#pragma once

// Donut
#include <donut/engine/Scene.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/render/SkyPass.h>
#include <donut/render/ToneMappingPasses.h>
#include <nvrhi/nvrhi.h>

// Streamline
#include<sl.h>
#include<sl_dlss.h>
#include<sl_dlss_g.h>

/// <summary>
/// This enum describes the available anti-aliasing modes. These can be toggled from the UI
/// </summary>
enum class AntiAliasingMode {
    NONE,
    TEMPORAL,
    DLSS,
};

/// <summary>
/// This enum describes the Dynamic Resolution mode used in-game
/// </summary>
enum class RenderingResolutionMode {
    FIXED,
    DYNAMIC,
    COUNT
};

struct UIData
{
    // General
    nvrhi::GraphicsAPI                  GraphicsAPI = nvrhi::GraphicsAPI::D3D12;
    bool                                EnableAnimations = true;
    float                               AnimationSpeed = 1.;
    bool                                EnableVsync = false;
    bool                                VisualiseBuffers = false;
    float                               CpuLoad = 0;
    int                                 GpuLoad = 0;
    donut::math::int2                   Resolution = { 0,0 };
    bool                                Resolution_changed = false;
    bool                                MouseOverUI = false;
    std::vector<sl::Extent>             BackBufferExtents{};

    // SSAO
    bool                                EnableSsao = true;
    donut::render::SsaoParameters       SsaoParams;

    // Tonemapping
    bool                                 EnableToneMapping = true;
    donut::render::ToneMappingParameters ToneMappingParams;

    // Sky
    bool                                EnableProceduralSky = true;
    donut::render::SkyParameters        SkyParams;
    float                               AmbientIntensity = .2f;

    // Antialising (+TAA)
    AntiAliasingMode                                   AAMode = AntiAliasingMode::NONE;
    donut::render::TemporalAntiAliasingJitter          TemporalAntiAliasingJitter = donut::render::TemporalAntiAliasingJitter::MSAA;
    donut::render::TemporalAntiAliasingParameters      TemporalAntiAliasingParams;

    // Bloom
    bool                                EnableBloom = true;
    float                               BloomSigma = 32.f;
    float                               BloomAlpha = 0.05f;

    // Shadows
    bool                                EnableShadows = true;
    float                               CsmExponent = 4.f;

    // DLSS specific parameters
    float                               DLSS_Sharpness = 0.f;
    bool                                DLSS_Supported = false;
    sl::DLSSMode                        DLSS_Mode = sl::DLSSMode::eOff;
    RenderingResolutionMode             DLSS_Resolution_Mode = RenderingResolutionMode::FIXED;
    bool                                DLSS_Dynamic_Res_change = true;
    donut::math::int2                   DLSS_Last_DisplaySize = { 0,0 };
    AntiAliasingMode                    DLSS_Last_AA = AntiAliasingMode::NONE;
    bool                                DLSS_DebugShowFullRenderingBuffer = false;
    bool                                DLSS_lodbias_useoveride = false;
    float                               DLSS_lodbias_overide = 0.f;
    bool                                DLSS_always_use_extents = false;
    sl::DLSSPreset                      DLSS_presets[static_cast<int>(sl::DLSSMode::eCount)] = {};
    sl::DLSSPreset                      DLSS_last_presets[static_cast<int>(sl::DLSSMode::eCount)] = {};
    bool UIData::DLSSPresetsChanged()
    {
        for (int i = 0; i < static_cast<int>(sl::DLSSMode::eCount); i++)
        {
            if (DLSS_presets[i] != DLSS_last_presets[i])
                return true;
        }
        return false;
    };
    bool UIData::DLSSPresetsAnyNonDefault()
    {
        for (int i = 0; i < static_cast<int>(sl::DLSSMode::eCount); i++)
        {
            if (DLSS_presets[i] != sl::DLSSPreset::eDefault)
                return true;
        }
        return false;
    };
    void UIData::DLSSPresetsUpdate()
    {
        for (int i = 0; i < static_cast<int>(sl::DLSSMode::eCount); i++)
            DLSS_last_presets[i] = DLSS_presets[i];
    };
    void UIData::DLSSPresetsReset()
    {
        for (int i = 0; i < static_cast<int>(sl::DLSSMode::eCount); i++)
            DLSS_last_presets[i] = DLSS_presets[i] = sl::DLSSPreset::eDefault;
    };

    // NIS specific parameters
    bool                                NIS_Supported = false;
    sl::NISMode                         NIS_Mode = sl::NISMode::eOff;
    float                               NIS_Sharpness = 0.5f;

    // DeepDVC specific parameters
    bool                                DeepDVC_Supported = false;
    sl::DeepDVCMode                     DeepDVC_Mode = sl::DeepDVCMode::eOff;
    float                               DeepDVC_Intensity = 0.5f;
    float                               DeepDVC_SaturationBoost = 0.75f;
    uint64_t                            DeepDVC_VRAM = 0;

    // LATENCY specific parameters
    bool                                REFLEX_Supported = false;
    bool                                REFLEX_LowLatencyAvailable = false;
    int                                 REFLEX_Mode = sl::ReflexMode::eOff;
    int                                 REFLEX_CapedFPS = 0;
    std::string                         REFLEX_Stats = "";

    // DLFG specific parameters
    bool                                DLSSG_Supported = false;
    sl::DLSSGMode                       DLSSG_mode = sl::DLSSGMode::eOff;
    float                               DLSSG_fps = 0;
    size_t                              DLSSG_memory = 0;
    std::string                         DLSSG_status = "";
    bool                                DLSSG_cleanup_needed = false;

};

