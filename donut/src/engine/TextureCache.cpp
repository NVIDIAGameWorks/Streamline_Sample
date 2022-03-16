
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/DDSFile.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/taskgroup.h>
#include <chrono>
#include <algorithm>
#include "stb_image.h"
#include "stb_image_write.h"

using namespace donut::math;
using namespace donut::vfs;
using namespace donut::engine;

class StbImageBlob : public IBlob
{
private:
    unsigned char* m_data = nullptr;

public:
    StbImageBlob(unsigned char* _data) : m_data(_data) 
    {
    }

    virtual ~StbImageBlob()
    {
        if (m_data)
        {
            stbi_image_free(m_data);
            m_data = nullptr;
        }
    }

    virtual const void* data() const override
    {
        return m_data;
    }

    virtual size_t size() const override
    {
        return 0; // nobody cares
    }
};


TextureCache::TextureCache(nvrhi::DeviceHandle device, std::shared_ptr<IFileSystem> fs)
    : m_Device(device)
    , m_fs(fs)
{
}

TextureCache::~TextureCache()
{
    Reset();
}

void TextureCache::Reset()
{
	std::lock_guard<std::mutex> guard(m_LoadedTexturesMutex);

	m_LoadedTextures.clear();

    m_TexturesRequested = 0;
    m_TexturesLoaded = 0;
}

void TextureCache::SetGenerateMipmaps(bool generateMipmaps)
{
    m_GenerateMipmaps = generateMipmaps;
}

bool TextureCache::FindTextureInCache(const std::filesystem::path& path, std::shared_ptr<TextureData>& texture)
{
    std::lock_guard<std::mutex> guard(m_LoadedTexturesMutex);

    // First see if this texture is already loaded (or being loaded).

    texture = m_LoadedTextures[path.generic_string()];
    if (texture)
    {
        return true;
    }

    // Allocate a new texture slot for this file name and return it. Load the file later in a thread pool.
    // LoadTextureFromFileAsync function for a given scene is only called from one thread, so there is no 
    // chance of loading the same texture twice.

    texture = std::make_shared<TextureData>();
    m_LoadedTextures[path.generic_string()] = texture;

    ++m_TexturesRequested;

    return false;
}

bool TextureCache::FillTextureData(const std::filesystem::path& path, std::shared_ptr<TextureData> texture)
{
    auto extension = path.extension();

    auto fileData = m_fs->readFile(path);
    if (!fileData)
    {
        log::warning("Couldn't read texture file `%s`", path.generic_string().c_str());
        return false;
    }

    if (extension == ".dds" || extension == ".DDS")
    {
        texture->data = fileData;
        if (!LoadDDSTextureFromMemory(*texture))
        {
            log::warning("Couldn't load texture file `%s`", path.generic_string().c_str());
            return false;
        }
    }
    else
    {
        int width = 0, height = 0, originalChannels = 0, channels = 0;

        if (!stbi_info_from_memory(
            static_cast<const stbi_uc*>(fileData->data()), 
            static_cast<int>(fileData->size()), 
            &width, &height, &originalChannels))
        {
            log::warning("Couldn't get info about texture file `%s`", path.generic_string().c_str());
            return false;
        }

        bool is_hdr = stbi_is_hdr_from_memory(
            static_cast<const stbi_uc*>(fileData->data()),
            static_cast<int>(fileData->size()));

        if (originalChannels == 3)
        {
            channels = 4;
        }
        else {
            channels = originalChannels;
        }

        unsigned char* bitmap;
        int bytesPerPixel = channels * (is_hdr ? 4 : 1);
        
        if (is_hdr)
        {
            float* floatmap = stbi_loadf_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);

            bitmap = reinterpret_cast<unsigned char*>(floatmap);
        }
        else
        {
            bitmap = stbi_load_from_memory(
                static_cast<const stbi_uc*>(fileData->data()),
                static_cast<int>(fileData->size()),
                &width, &height, &originalChannels, channels);
        }

        if (!bitmap)
        {
            log::warning("Couldn't load texture file `%s`", path.generic_string().c_str());
            return false;
        }

        texture->originalBitsPerPixel = static_cast<uint32_t>(originalChannels) * (is_hdr ? 32 : 8);
        texture->width = static_cast<uint32_t>(width);
        texture->height = static_cast<uint32_t>(height);
        texture->isRenderTarget = true;
        texture->mipLevels = 1;
        texture->dimension = nvrhi::TextureDimension::Texture2D;

        texture->dataLayout.resize(1);
        texture->dataLayout[0].resize(1);
        texture->dataLayout[0][0].dataOffset = 0;
        texture->dataLayout[0][0].rowPitch = static_cast<size_t>(width * bytesPerPixel);
        texture->dataLayout[0][0].dataSize = static_cast<size_t>(width * height * bytesPerPixel);

        texture->data = std::make_shared<StbImageBlob>(bitmap);
        bitmap = nullptr; // ownership transferred to the blob

        switch (channels)
        {
        case 1:
            texture->format = is_hdr ? nvrhi::Format::R32_FLOAT : nvrhi::Format::R8_UNORM;
            break;
        case 2:
            texture->format = is_hdr ? nvrhi::Format::RG32_FLOAT : nvrhi::Format::RG8_UNORM;
            break;
        case 4:
            texture->format = is_hdr ? nvrhi::Format::RGBA32_FLOAT : (texture->forceSRGB ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM);
            break;
        default:
            texture->data.reset(); // release the bitmap data

            log::warning("Unsupported number of components (%d) for texture `%s`", channels, path.generic_string().c_str());
            return false;
        }
    }

    return true;
}

uint GetMipLevelsNum(uint width, uint height)
{
    uint size = std::min(width, height);
    uint levelsNum = (uint)(logf((float)size) / logf(2.0f)) + 1;

    return levelsNum;
}

void TextureCache::FinalizeTexture(std::shared_ptr<TextureData> texture, CommonRenderPasses* passes, nvrhi::ICommandList* commandList)
{
    assert(texture->data);
    assert(commandList);

    uint originalWidth = texture->width;
    uint originalHeight = texture->height;
    uint scaledWidth = originalWidth;
    uint scaledHeight = originalHeight;

    if (m_MaxTextureSize > 0 && int(std::max(originalWidth, originalHeight)) > m_MaxTextureSize && texture->isRenderTarget && texture->dimension == nvrhi::TextureDimension::Texture2D)
    {
        if (originalWidth >= originalHeight)
        {
            scaledHeight = originalHeight * m_MaxTextureSize / originalWidth;
            scaledWidth = m_MaxTextureSize;
        }
        else
        {
            scaledWidth = originalWidth * m_MaxTextureSize / originalHeight;
            scaledHeight = m_MaxTextureSize;
        }
    }

    const char* dataPointer = static_cast<const char*>(texture->data->data());

    nvrhi::TextureDesc textureDesc;
    textureDesc.format = texture->format;
    textureDesc.width = scaledWidth;
    textureDesc.height = scaledHeight;
    textureDesc.depth = texture->depth;
    textureDesc.arraySize = texture->arraySize;
    textureDesc.dimension = texture->dimension;
    textureDesc.mipLevels = m_GenerateMipmaps && texture->isRenderTarget && passes
        ? GetMipLevelsNum(textureDesc.width, textureDesc.height)
        : texture->mipLevels;
    textureDesc.debugName = texture->relativePath.c_str();
    textureDesc.isRenderTarget = texture->isRenderTarget;
    texture->texture = m_Device->createTexture(textureDesc);

    commandList->beginTrackingTextureState(texture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);

    if (scaledWidth != originalWidth || scaledHeight != originalHeight)
    {
        nvrhi::TextureDesc tempTextureDesc;
        tempTextureDesc.format = texture->format;
        tempTextureDesc.width = originalWidth;
        tempTextureDesc.height = originalHeight;
        tempTextureDesc.depth = textureDesc.depth;
        tempTextureDesc.arraySize = textureDesc.arraySize;
        tempTextureDesc.mipLevels = 1;
        tempTextureDesc.dimension = textureDesc.dimension;

        nvrhi::TextureHandle tempTexture = m_Device->createTexture(tempTextureDesc);
        assert(tempTexture);
        commandList->beginTrackingTextureState(tempTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::COMMON);

        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            const TextureSubresourceData& layout = texture->dataLayout[arraySlice][0];

            commandList->writeTexture(tempTexture, arraySlice, 0, dataPointer + layout.dataOffset, layout.rowPitch, layout.depthPitch);
        }

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(texture->texture)));

        nvrhi::BindingSetHandle tempTextureBindingSet = passes->CreateBlitBindingSet(tempTexture, 0, 0);

        passes->BlitTexture(commandList, framebuffer, nvrhi::Viewport(float(scaledWidth), float(scaledHeight)), tempTextureBindingSet);
    }
    else
    {
        for (uint32_t arraySlice = 0; arraySlice < texture->arraySize; arraySlice++)
        {
            for (uint32_t mipLevel = 0; mipLevel < texture->mipLevels; mipLevel++)
            {
                const TextureSubresourceData& layout = texture->dataLayout[arraySlice][mipLevel];

                commandList->writeTexture(texture->texture, arraySlice, mipLevel, dataPointer + layout.dataOffset, layout.rowPitch, layout.depthPitch);
            }
        }
    }

    texture->data.reset();

    uint width = scaledWidth;
    uint height = scaledHeight;
    for (uint mipLevel = texture->mipLevels; mipLevel < textureDesc.mipLevels; mipLevel++)
    {
        width /= 2;
        height /= 2;

        nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
            .addColorAttachment(nvrhi::FramebufferAttachment(texture->texture, 0, mipLevel)));

        nvrhi::BindingSetHandle sourceBindingSet = passes->CreateBlitBindingSet(texture->texture, 0, mipLevel - 1);

        passes->BlitTexture(commandList, framebuffer, nvrhi::Viewport(float(width), float(height)), sourceBindingSet);
    }

    commandList->endTrackingTextureState(texture->texture, nvrhi::AllSubresources, nvrhi::ResourceStates::SHADER_RESOURCE, true);
}

void TextureCache::SendTextureLoadedMessage(std::shared_ptr<TextureData> texture)
{
    log::info("Loaded %d x %d, %d bpp: %s", texture->width, texture->height, texture->originalBitsPerPixel, texture->relativePath.c_str());
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFile(const std::filesystem::path& path, bool sRGB, CommonRenderPasses* passes, nvrhi::ICommandList* commandList)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->relativePath = path.generic_string();

    if (FillTextureData(path, texture))
    {
        FinalizeTexture(texture, passes, commandList);

        SendTextureLoadedMessage(texture);
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileDeferred(const std::filesystem::path& path, bool sRGB)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->relativePath = path.generic_string();

    if (FillTextureData(path, texture))
    {
        std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

        m_TexturesToFinalize.push(texture);

        SendTextureLoadedMessage(texture);
    }

    ++m_TexturesLoaded;

    return texture;
}

std::shared_ptr<LoadedTexture> TextureCache::LoadTextureFromFileAsync(const std::filesystem::path& path, bool sRGB, concurrency::task_group& taskGroup)
{
    std::shared_ptr<TextureData> texture;

    if (FindTextureInCache(path, texture))
        return texture;

    texture->forceSRGB = sRGB;
    texture->relativePath = path.generic_string();

    taskGroup.run([this, sRGB, texture, path]()
    {
        if (FillTextureData(path, texture))
        {
            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            m_TexturesToFinalize.push(texture);

            SendTextureLoadedMessage(texture);
        }

        ++m_TexturesLoaded;
	});

	return texture;
}


void TextureCache::ProcessRenderingThreadCommands(CommonRenderPasses& passes, float timeLimitMilliseconds)
{
    using namespace std::chrono;

    time_point<high_resolution_clock> startTime = high_resolution_clock::now();

    uint commandsExecuted = 0;
    while (true)
    {
        std::shared_ptr<TextureData> pTexture;

        if (timeLimitMilliseconds > 0)
        {
            time_point<high_resolution_clock> now = high_resolution_clock::now();

            if (float(duration_cast<microseconds>(now - startTime).count()) > timeLimitMilliseconds * 1e3)
                break;
        }

        {
            std::lock_guard<std::mutex> guard(m_TexturesToFinalizeMutex);

            if (m_TexturesToFinalize.empty())
                break;

            pTexture = m_TexturesToFinalize.front();
            m_TexturesToFinalize.pop();
        }

        if (pTexture->data)
        {
            //LOG("Finalizing texture %s", pTexture->fileName.c_str());
            commandsExecuted += 1;

            if (!m_CommandList)
            {
                m_CommandList = m_Device->createCommandList();
            }

            m_CommandList->open();

            FinalizeTexture(pTexture, &passes, m_CommandList);

            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_Device->runGarbageCollection();
        }
    }

    if (commandsExecuted > 0)
    {
        //LOG("Finalized %d textures.", commandsExecuted);
    }
}

void TextureCache::LoadingFinished()
{
    m_CommandList = nullptr;
}

void TextureCache::SetMaxTextureSize(uint32_t size)
{
	m_MaxTextureSize = size;
}

namespace donut::engine
{
    bool WriteBMPToFile(
        const uint * pPixels,
        int2_arg dims,
        const char* path)
    {
        assert(pPixels);
        assert(all(dims > 0));
        return stbi_write_bmp(path, dims.x, dims.y, 4, pPixels) != 0;
    }

    bool SaveTextureToFile(nvrhi::IDevice* device, CommonRenderPasses* pPasses, nvrhi::TextureHandle texture, nvrhi::ResourceStates::Enum textureState, const char* fileName)
    {
        nvrhi::TextureDesc desc = texture->GetDesc();
        nvrhi::TextureHandle tempTexture;
        nvrhi::FramebufferHandle tempFramebuffer;
        nvrhi::StagingTextureHandle stagingTexture;

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();
        commandList->beginTrackingTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);

        switch (desc.format)
        {
        case nvrhi::Format::RGBA8_UNORM:
        case nvrhi::Format::SRGBA8_UNORM:
            tempTexture = texture;
            break;
        default:
            desc.format = nvrhi::Format::SRGBA8_UNORM;
            desc.isRenderTarget = true;
            desc.initialState = nvrhi::ResourceStates::RENDER_TARGET;
            desc.keepInitialState = true;

            tempTexture = device->createTexture(desc);
            tempFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tempTexture));
            pPasses->BlitTexture(commandList, tempFramebuffer, nvrhi::Viewport(float(desc.width), float(desc.height)), texture, 0);
        }

        stagingTexture = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        commandList->copyTexture(stagingTexture, nvrhi::TextureSlice(), tempTexture, nvrhi::TextureSlice());

        commandList->endTrackingTextureState(texture, nvrhi::TextureSubresourceSet(0, 1, 0, 1), textureState);
        commandList->close();
        device->executeCommandList(commandList);

        size_t rowPitch = 0;
        void* pData = device->mapStagingTexture(stagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);

        if (!pData)
            return false;

        uint32_t* newData = nullptr;

        if (rowPitch != desc.width * 4)
        {
            newData = new uint32_t[desc.width * desc.height];

            for (uint32_t row = 0; row < desc.height; row++)
            {
                memcpy(newData + row * desc.width, reinterpret_cast<char*>(pData) + row * rowPitch, desc.width * sizeof(uint32_t));
            }

            pData = newData;
        }

        bool writeSuccess = WriteBMPToFile(reinterpret_cast<uint*>(pData), int2(desc.width, desc.height), fileName);

        if (newData)
        {
            delete[] newData;
            newData = nullptr;
        }

        device->unmapStagingTexture(stagingTexture);

        return writeSuccess;
    }

}
