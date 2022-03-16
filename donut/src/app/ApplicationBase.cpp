
#include <climits>

#include <donut/app/ApplicationBase.h>
#include <donut/engine/Scene.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>

#ifndef _WIN32
#include <unistd.h>
#include <cstdio>
#else
#define PATH_MAX MAX_PATH
#endif // _WIN32

using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::app;

ApplicationBase::ApplicationBase(DeviceManager* deviceManager)
    : Super(deviceManager)
    , m_IsAsyncLoad(true)
    , m_SceneLoaded(false)
{
}

void ApplicationBase::Render(nvrhi::IFramebuffer* framebuffer)
{
    if (m_TextureCache)
    {
        m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 20.f);
    }

    if (!m_SceneLoaded)
    {
        RenderSplashScreen(framebuffer);
        return;
    }

    if (m_SceneLoaded && m_SceneLoadingThread)
    {
        m_SceneLoadingThread->join();
        m_SceneLoadingThread = nullptr;

        // SceneLoaded() would already get called from 
        // BeginLoadingScene() in case of synchronous loads
        SceneLoaded();
    }

    RenderScene(framebuffer);
}

void ApplicationBase::RenderScene(nvrhi::IFramebuffer* framebuffer)
{

}

void ApplicationBase::RenderSplashScreen(nvrhi::IFramebuffer* framebuffer)
{

}

void ApplicationBase::SceneUnloading()
{

}

void ApplicationBase::SceneLoaded()
{
    if (m_TextureCache)
    {
        m_TextureCache->ProcessRenderingThreadCommands(*m_CommonPasses, 0.f);
        m_TextureCache->LoadingFinished();
    }
    m_SceneLoaded = true;
}

void ApplicationBase::SetAsynchronousLoadingEnabled(bool enabled)
{
    m_IsAsyncLoad = enabled;
}

bool ApplicationBase::IsSceneLoading() const
{
    return m_SceneLoadingThread != nullptr;
}

bool ApplicationBase::IsSceneLoaded() const
{
    return m_SceneLoaded;
}

void ApplicationBase::BeginLoadingScene(std::shared_ptr<IFileSystem> fs, std::filesystem::path sceneFileName)
{
    if (m_SceneLoaded)
        SceneUnloading();

    m_SceneLoaded = false;
    if (m_TextureCache)
    {
        m_TextureCache->Reset();
    }
    GetDevice()->waitForIdle();
    GetDevice()->runGarbageCollection();


    if (m_IsAsyncLoad)
    {
        m_SceneLoadingThread = std::make_unique<std::thread>([this, fs, sceneFileName]() { m_SceneLoaded = LoadScene(fs, sceneFileName); });
    }
    else
    {
        m_SceneLoaded = LoadScene(fs, sceneFileName);
        SceneLoaded();
    }
}

std::shared_ptr<CommonRenderPasses> ApplicationBase::GetCommonPasses()
{
    return m_CommonPasses;
}

std::filesystem::path donut::app::FindDirectoryWithShaderBin(nvrhi::GraphicsAPI api, IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, const std::string& baseFileName, int maxDepth)
{
	std::string shaderFileSuffix = ".bin";
    std::filesystem::path shaderFileBasePath;

    switch (api)
    {
#if DONUT_USE_DXIL_ON_DX12
    case nvrhi::GraphicsAPI::D3D11:
        shaderFileBasePath = "dxbc";
        break;
    case nvrhi::GraphicsAPI::D3D12:
        shaderFileBasePath = "dxil";
        break;
#else
    case nvrhi::GraphicsAPI::D3D11:
    case nvrhi::GraphicsAPI::D3D12:
        shaderFileBasePath = "dxbc";
        break;
#endif
    case nvrhi::GraphicsAPI::VULKAN:
        shaderFileBasePath = "spirv";
        break;
    default:
        assert(!"Unknown graphics API");
        break;
    }

    std::filesystem::path findBytecodeFileName = relativeFilePath / shaderFileBasePath / (baseFileName + shaderFileSuffix);
	return FindDirectoryWithFile(fs, startPath, findBytecodeFileName, maxDepth);
}

std::filesystem::path donut::app::FindDirectoryWithFile(IFileSystem& fs, const std::filesystem::path& startPath, const std::filesystem::path& relativeFilePath, int maxDepth)
{
    std::filesystem::path searchPath = "";

    for (int depth = 0; depth < maxDepth; depth++)
    {
        std::filesystem::path currentPath = startPath / searchPath / relativeFilePath;

        if (fs.fileExists(currentPath))
        {
            return currentPath.parent_path().lexically_normal();
        }

        searchPath = ".." / searchPath;
    }

    return "";
}

std::filesystem::path donut::app::FindMediaFolder(const std::filesystem::path& sampleScene)
{
	const char* envvar = "DONUT_MEDIA_PATH";
    char buf[PATH_MAX] = {0};
#ifdef _WIN32
    if (GetEnvironmentVariableA(envvar, buf, sizeof(buf)))
#else // _WIN32
	if (getenv(envvar) && strncpy(buf, getenv(envvar), std::size(buf)-1))
#endif // _WIN32
    {
        return buf;
    }
    else
    {
		donut::vfs::NativeFileSystem fs;
        return FindDirectoryWithFile(fs, GetDirectoryWithExecutable(), sampleScene);
    }
}

std::filesystem::path donut::app::GetDirectoryWithExecutable()
{
    char path[PATH_MAX] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, path, dim(path)) == 0)
        return "";
#else // _WIN32
	// /proc/self/exe is mostly linux-only, but can't hurt to try it elsewhere
	if (readlink("/proc/self/exe", path, std::size(path)) <= 0)
	{
		// portable but assumes executable dir == cwd
		if (!getcwd(path, std::size(path)))
			return ""; // failure
	}
#endif // _WIN32

    std::filesystem::path result = path;
    result = result.parent_path();

    return result;
}

bool donut::app::FileDialog(bool bOpen, const char* pFilters, std::string& fileName)
{
#ifdef _WIN32
    OPENFILENAMEA ofn;
    CHAR chars[PATH_MAX] = "";
    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = GetForegroundWindow();
    ofn.lpstrFilter = pFilters;
    ofn.lpstrFile = chars;
    ofn.nMaxFile = ARRAYSIZE(chars);
    ofn.Flags = OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (bOpen)
    {
        ofn.Flags |= OFN_FILEMUSTEXIST;
    }
    ofn.lpstrDefExt = "";

    BOOL b = bOpen ? GetOpenFileNameA(&ofn) : GetSaveFileNameA(&ofn);
    if (b)
    {
        fileName = chars;
        return true;
    }

    return false;
#else // _WIN32
	// minimal implementation avoiding a GUI library, ignores filters for now,
	// and relies on external 'zenity' program commonly available on linuxoids
	char chars[PATH_MAX] = {0};
	std::string app = "zenity --file-selection";
	if (!bOpen)
	{
		app += " --save --confirm-overwrite";
	}
	FILE *f = popen(app.c_str(), "r");
	bool gotname = (nullptr != fgets(chars, PATH_MAX, f));
	pclose(f);
	
	if (gotname && chars[0] != '\0')
	{	
		fileName = chars;
		return true;
	}
	return false;
#endif // _WIN32
}

nvrhi::GraphicsAPI donut::app::GetGraphicsAPIFromCommandLine(int argc, const char* const* argv)
{
    for (int n = 1; n < argc; n++)
    {
        const char* arg = argv[n];

        if (!strcmp(arg, "-d3d11") || !strcmp(arg, "-dx11"))
            return nvrhi::GraphicsAPI::D3D11;
        else if (!strcmp(arg, "-d3d12") || !strcmp(arg, "-dx12"))
            return nvrhi::GraphicsAPI::D3D12;
        else if(!strcmp(arg, "-vk") || !strcmp(arg, "-vulkan"))
            return nvrhi::GraphicsAPI::VULKAN;
    }

#if USE_DX12
    return nvrhi::GraphicsAPI::D3D12;
#elif USE_DX11
    return nvrhi::GraphicsAPI::D3D11;
#elif USE_VK
    return nvrhi::GraphicsAPI::VULKAN;
#endif
}

void MediaFolder::EnumerateScenes(std::vector<std::string>& outScenes)
{
    m_fs->enumerate(m_path / "*.json", false, outScenes);
    m_fs->enumerate(m_path / "*.fscene", false, outScenes);

    std::sort(outScenes.begin(), outScenes.end());

    std::vector<std::string> directories;
    m_fs->enumerate(m_path / "*", true, directories);

    for (const std::string& directory : directories)
    {
        std::vector<std::string> scenesHere;
        m_fs->enumerate(m_path / std::filesystem::path(directory) / "*.fscene", false, scenesHere);

        for (const std::string& scene : scenesHere)
        {
            outScenes.push_back(directory + "/" + scene);
        }
    }
}

MediaFolder::MediaFolder(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& path)
    : m_fs(fs)
    , m_path(path)
{
    EnumerateScenes(m_SceneNames);

    if (m_SceneNames.size() == 0)
    {
        log::fatal("Can't find any scenes.\n"
            "Please make sure that the media folder is in the donut tree or that the DONUT_MEDIA_PATH environment variable is set correctly.");
    }
}

std::shared_ptr<IFileSystem> MediaFolder::GetFileSystem() const
{
    return m_fs;
}

std::filesystem::path MediaFolder::GetPath() const
{
    return m_path;
}

const std::vector<std::string>& MediaFolder::GetAvailableScenes() const
{
    return m_SceneNames;
}
