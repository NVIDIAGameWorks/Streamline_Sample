//----------------------------------------------------------------------------------
// File:        UIRenderer.h
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

#include <donut/app/imgui_renderer.h>

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<FeatureDemo> m_app;
    UIData& m_ui;
    nvrhi::CommandListHandle m_CommandList;
    std::deque<float> frameTimeList;

public:
    UIRenderer(DeviceManager* deviceManager, std::shared_ptr<FeatureDemo> app, UIData& ui)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ui(ui)
    {
        m_CommandList = GetDevice()->createCommandList();
    }

protected:
    virtual void buildUI(void) override
    {
        if (!m_ui.ShowUI)
            return;

        const auto& io = ImGui::GetIO();


        if (m_app->IsSceneLoading())
        {
            BeginFullScreenWindow();

            char messageBuffer[256];
            const auto& stats = Scene::GetLoadingStats();
            snprintf(messageBuffer, std::size(messageBuffer), "Loading scene %s, please wait...\nObjects: %d/%d, Textures: %d/%d",
                m_app->GetCurrentSceneName().c_str(), stats.ObjectsLoaded.load(), stats.ObjectsTotal.load(), m_app->GetTextureCache()->GetNumberOfLoadedTextures(), m_app->GetTextureCache()->GetNumberOfRequestedTextures());

            DrawScreenCenteredText(messageBuffer);

            EndFullScreenWindow();

            return;
        }


        ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
        double avgFrameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
        if (avgFrameTime > 0.0)
        {
            if (frameTimeList.size() > 50)
            {
                frameTimeList.pop_front();
            }
            frameTimeList.push_back((float)avgFrameTime * 1e3f);
            std::vector<float> sortedList(frameTimeList.begin(), frameTimeList.end());
            std::sort(sortedList.begin(), sortedList.end());
            ImGui::Text("Mdn %.3f ms/frm Avg %.3f ms/frm (%.1f FPS)",
                        sortedList[sortedList.size() / 2],
                        avgFrameTime * 1e3,
                        1.0 / avgFrameTime );
        }

        std::string deviceNames[] = { "d3d11", "d3d12", "vulkan" };
        auto name = deviceNames[(int)m_ui.deviceType];
        ImGui::Text("API: %s", name.c_str());

        ImGui::Separator();
        ImGui::Text("DLSS_Supported: %s", m_ui.DLSS_Supported ? "yes":"no");
#ifdef USE_SL
        ImGui::Combo("AA Mode", (int*)&m_ui.AAMode, "None\0TemporalAA\0DLSS\0");
#else
        ImGui::Combo("AA Mode", (int*)&m_ui.AAMode, "None\0TemporalAA\0");
#endif

#ifdef USE_SL

        if (m_ui.AAMode == AntiAliasingMode::DLSS)
        {
            // We do not show 'eDLSSModeOff' or 'eDLSSModeUltraQuality' so we need range [1:eDLSSModeUltraQuality).
            static const char* DLSSModeNames[] = {
                "Performance",
                "Balanced",
                "Quality",
                "Ultra-Performance",
                // Not visible to end-users as per "RTX UI Developer Guidelines"
                //"eDLSSModeUltraQuality"
            };

            if (ImGui::BeginCombo("DLSS Mode", DLSSModeNames[(int)m_ui.DLSS_Mode - 1]))
            {
                for (int i = 1; i < sl::eDLSSModeCount - 1; ++i)
                {
                    bool is_selected = (i == (int)m_ui.DLSS_Mode);

                    if (ImGui::Selectable(DLSSModeNames[i-1], is_selected)) m_ui.DLSS_Mode = (sl::DLSSMode)i;
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            static const char* DLSSResModeNames[] = {
                "Fixed",
                "Dynamic"
            };

            if (ImGui::BeginCombo("DLSS Resolution Mode", DLSSResModeNames[(int)m_ui.DLSS_Resolution_Mode]))
            {
                for (int i = 0; i < (int)RenderingResolutionMode::COUNT; ++i)
                {
                    bool is_selected = (i == (int)m_ui.DLSS_Resolution_Mode);

                    if (ImGui::Selectable(DLSSResModeNames[i], is_selected))
                        m_ui.DLSS_Resolution_Mode = (RenderingResolutionMode)i;
                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (m_ui.DLSS_Resolution_Mode == RenderingResolutionMode::DYNAMIC)
                ImGui::Checkbox("Debug: Show full rendering buffer", &m_ui.DebugShowFullRenderingBuffer);
            else
                m_ui.DebugShowFullRenderingBuffer = false;
        }
#endif
        ImGui::End();
    }
};