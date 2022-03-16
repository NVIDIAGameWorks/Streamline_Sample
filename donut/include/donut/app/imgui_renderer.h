#pragma once

#include <donut/app/DeviceManager.h>
#include <memory>
#include <filesystem>

#include "imgui_nvrhi.h"

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    class ShaderFactory;
}

namespace donut::app
{
    // base class to build IRenderPass-based UIs using ImGui through NVRHI
    class ImGui_Renderer : public IRenderPass
    {
    protected:
        std::unique_ptr<ImGui_NVRHI> imgui_nvrhi;
        std::vector<ImFont*> fonts;
        std::vector<std::shared_ptr<vfs::IBlob>> m_fontData;

        // buffer mouse click and keypress events to make sure we don't lose events which last less than a full frame
        std::array<bool, 3> mouseDown = { false };
        std::array<bool, GLFW_KEY_LAST + 1> keyDown = { false };

    public:
        ImGui_Renderer(DeviceManager *devManager);
        ~ImGui_Renderer();
        bool Init(std::shared_ptr<engine::ShaderFactory> shaderFactory);
        bool LoadFont(vfs::IFileSystem& fs, const std::filesystem::path& fontFile, float fontSize);

        ImFont* GetFont(uint32_t fontID);

        virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
        virtual bool KeyboardCharInput(unsigned int unicode, int mods) override;
        virtual bool MousePosUpdate(double xpos, double ypos) override;
        virtual bool MouseScrollUpdate(double xoffset, double yoffset) override;
        virtual bool MouseButtonUpdate(int button, int action, int mods) override;
        virtual void Animate(float elapsedTimeSeconds) override;
        virtual void Render(nvrhi::IFramebuffer* framebuffer) override;
        virtual void BackBufferResizing() override;

    protected:
        // creates the UI in ImGui, updates internal UI state
        virtual void buildUI(void) = 0;

        void BeginFullScreenWindow();
        void DrawScreenCenteredText(const char* text);
        void EndFullScreenWindow();
    };
}