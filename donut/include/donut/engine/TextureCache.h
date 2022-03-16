#pragma once

#include <map>
#ifdef _WIN32
#include <ppl.h>
#endif // _WIN32
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <filesystem>
#include <nvrhi/nvrhi.h>
#include <donut/engine/SceneTypes.h>
#include <donut/core/taskgroup.h>

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine
{
    class CommonRenderPasses;

    struct TextureSubresourceData
    {
        size_t rowPitch = 0;
        size_t depthPitch = 0;
        ptrdiff_t dataOffset = 0;
        size_t dataSize = 0;
    };

    struct TextureData : public LoadedTexture
    {
        std::shared_ptr<vfs::IBlob> data;

        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Unknown;
        bool isRenderTarget = false;
        bool forceSRGB = false;

        // ArraySlice -> MipLevel -> TextureSubresourceData
        std::vector<std::vector<TextureSubresourceData>> dataLayout;
    };

    class TextureCache
    {
    protected:
        nvrhi::DeviceHandle m_Device;
        nvrhi::CommandListHandle m_CommandList;
        std::map<std::string, std::shared_ptr<TextureData>> m_LoadedTextures;
        std::mutex m_LoadedTexturesMutex;

        std::queue<std::shared_ptr<TextureData>> m_TexturesToFinalize;
        std::mutex m_TexturesToFinalizeMutex;

        std::shared_ptr<vfs::IFileSystem> m_fs;

        uint32_t m_MaxTextureSize = 0;

        bool m_GenerateMipmaps = true;

        std::atomic<uint32_t> m_TexturesRequested = 0;
        std::atomic<uint32_t> m_TexturesLoaded = 0;

        bool FindTextureInCache(const std::filesystem::path& path, std::shared_ptr<TextureData>& texture);
        bool FillTextureData(const std::filesystem::path& path, std::shared_ptr<TextureData> texture);
        void FinalizeTexture(std::shared_ptr<TextureData> texture, CommonRenderPasses* passes, nvrhi::ICommandList* commandList);
        void SendTextureLoadedMessage(std::shared_ptr<TextureData> texture);

    public:
        TextureCache(nvrhi::DeviceHandle device, std::shared_ptr<vfs::IFileSystem> fs);
        ~TextureCache();

        // Release all cached textures
        void Reset();

        // Synchronous read and decode, synchronous upload and mip generation on a given command list (must be open).
        // The `passes` argument is optional, and mip generation is disabled if it's NULL.
        std::shared_ptr<LoadedTexture> LoadTextureFromFile(const std::filesystem::path& path, bool sRGB, CommonRenderPasses* passes, nvrhi::ICommandList* commandList);

        // Synchronous read and decode, deferred upload and mip generation (in the ProcessRenderingThreadCommands queue).
        std::shared_ptr<LoadedTexture> LoadTextureFromFileDeferred(const std::filesystem::path& path, bool sRGB);

        // Asynchronous read and decode, deferred upload and mip generation (in the ProcessRenderingThreadCommands queue).
        std::shared_ptr<LoadedTexture> LoadTextureFromFileAsync(const std::filesystem::path& path, bool sRGB, concurrency::task_group& taskGroup);

        // Process a portion of the upload queue, taking up to `timeLimitMilliseconds` CPU time.
        // If `timeLimitMilliseconds` is 0, processes the entire queue.
        void ProcessRenderingThreadCommands(CommonRenderPasses& passes, float timeLimitMilliseconds);

        // Destroys the internal command list in order to release the upload buffers used in it.
        void LoadingFinished();

        // Set the maximum texture size allowed after load. Larger textures are resized to fit this constraint.
        // Currently does not affect DDS textures.
        void SetMaxTextureSize(uint32_t size);

        // Enables or disables automatic mip generation for loaded textures.
        void SetGenerateMipmaps(bool generateMipmaps);

        uint32_t GetNumberOfLoadedTextures() { return m_TexturesLoaded.load(); }
        uint32_t GetNumberOfRequestedTextures() { return m_TexturesRequested.load(); }
    };

    // Saves the contents of texture's slice 0 mip level 0 into a BMP file. 
    // Requires that no immediate command list is open at the time this function is called.
    // Creates and destroys temporary resources internally, so should NOT be called often.
    bool SaveTextureToFile(nvrhi::IDevice* device, CommonRenderPasses* pPasses, nvrhi::TextureHandle texture, nvrhi::ResourceStates::Enum textureState, const char* fileName);
}
