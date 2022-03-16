#pragma once

#include <donut/app/DeviceManager.h>
#include <nvrhi/nvrhi.h>
#include <thread>
#include <vector>
#include <filesystem>

namespace donut::vfs
{
    class IFileSystem;
}

namespace donut::engine
{
    class TextureCache;
    class CommonRenderPasses;
}

namespace donut::app
{
    class MediaFolder
    {
    private:
        std::shared_ptr<vfs::IFileSystem>   m_fs;
        std::filesystem::path               m_path;
        std::vector<std::string>            m_SceneNames;

        void EnumerateScenes(std::vector<std::string>& outScenes);

    public:
        MediaFolder(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& path);
        
        std::shared_ptr<vfs::IFileSystem> GetFileSystem() const;
        std::filesystem::path GetPath() const;

        const std::vector<std::string>& GetAvailableScenes() const;
    };

    class ApplicationBase : public IRenderPass
    {
    private:
        bool                                m_SceneLoaded;

    protected:
        typedef IRenderPass Super;

        std::shared_ptr<engine::TextureCache> m_TextureCache;
        std::unique_ptr<std::thread>        m_SceneLoadingThread;
        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

        bool                                m_IsAsyncLoad;

    public:
        ApplicationBase(DeviceManager* deviceManager);

        virtual void Render(nvrhi::IFramebuffer* framebuffer) override;

        virtual void RenderScene(nvrhi::IFramebuffer* framebuffer);
        virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer);
        virtual void BeginLoadingScene(std::shared_ptr<vfs::IFileSystem> fs, std::filesystem::path sceneFileName);
        virtual bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& sceneFileName) = 0;
        virtual void SceneUnloading();
        virtual void SceneLoaded();

        void SetAsynchronousLoadingEnabled(bool enabled);
        bool IsSceneLoading() const;
        bool IsSceneLoaded() const;

        std::shared_ptr<engine::CommonRenderPasses> GetCommonPasses();
    };

    std::filesystem::path FindDirectoryWithFile(vfs::IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, int maxDepth = 5);
    std::filesystem::path FindDirectoryWithShaderBin(nvrhi::GraphicsAPI api, vfs::IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, const std::string& baseFileName, int maxDepth = 5);
    std::filesystem::path GetDirectoryWithExecutable();
    std::filesystem::path FindMediaFolder(const std::filesystem::path& sampleScene);
    bool FileDialog(bool bOpen, const char* pFilters, std::string& fileName);
    nvrhi::GraphicsAPI GetGraphicsAPIFromCommandLine(int argc, const char* const* argv);
}