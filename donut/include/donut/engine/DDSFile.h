#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>
#include <memory>

namespace donut::vfs
{
    class IBlob;
}

namespace donut::engine
{
    struct TextureData;

    // Initialized the TextureInfo from the 'data' array, which must be populated with DDS data
    bool LoadDDSTextureFromMemory(TextureData& textureInfo);

    // Creates a texture based on DDS data in memory
    nvrhi::TextureHandle CreateDDSTextureFromMemory(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, std::shared_ptr<vfs::IBlob> data, const char* debugName = nullptr, bool forceSRGB = false);

    std::shared_ptr<vfs::IBlob> SaveStagingTextureAsDDS(nvrhi::IDevice* device, nvrhi::IStagingTexture* stagingTexture);
}