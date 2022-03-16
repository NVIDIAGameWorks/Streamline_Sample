#include <array>
#include <assert.h>

#include <nvrhi/vulkan.h>

namespace nvrhi {
namespace vulkan {

struct FormatMapping
{
    nvrhi::Format rhiFormat;

    vk::Format vkFormat;
    // An "element" in this case is a block (in the case of block-compressed formats)
    // or a single pixel (in the case of non-block-compressed formats)
    uint32_t elementSizeBits;
    uint32_t blockSize = 1;
};

static const std::array<FormatMapping, size_t(Format::COUNT)> formatMap = { {
    { Format::UNKNOWN,              vk::Format(VK_FORMAT_UNDEFINED),                 0, 0, },

    { Format::R8_UINT,              vk::Format(VK_FORMAT_R8_UINT),                   8, },
    { Format::R8_SINT,              vk::Format(VK_FORMAT_R8_SINT),                   8, },
    { Format::R8_UNORM,             vk::Format(VK_FORMAT_R8_UNORM),                  8, },
    { Format::R8_SNORM,             vk::Format(VK_FORMAT_R8_SNORM),                  8, },
    { Format::RG8_UINT,             vk::Format(VK_FORMAT_R8G8_UINT),                 8, },
    { Format::RG8_SINT,             vk::Format(VK_FORMAT_R8G8_SINT),                 8, },
    { Format::RG8_UNORM,            vk::Format(VK_FORMAT_R8G8_UNORM),                8, },
    { Format::RG8_SNORM,            vk::Format(VK_FORMAT_R8G8_SNORM),                8, },
    { Format::R16_UINT,             vk::Format(VK_FORMAT_R16_UINT),                  16, },
    { Format::R16_SINT,             vk::Format(VK_FORMAT_R16_SINT),                  16, },
    { Format::R16_UNORM,            vk::Format(VK_FORMAT_R16_UNORM),                 16, },
    { Format::R16_SNORM,            vk::Format(VK_FORMAT_R16_SNORM),                 16, },
    { Format::R16_FLOAT,            vk::Format(VK_FORMAT_R16_SFLOAT),                16, },
    { Format::BGRA4_UNORM,          vk::Format(VK_FORMAT_B4G4R4A4_UNORM_PACK16),     16, },
    { Format::B5G6R5_UNORM,         vk::Format(VK_FORMAT_B5G6R5_UNORM_PACK16),       16, },
    { Format::B5G5R5A1_UNORM,       vk::Format(VK_FORMAT_B5G5R5A1_UNORM_PACK16),     16, },
    { Format::RGBA8_UINT,           vk::Format(VK_FORMAT_R8G8B8A8_UINT),             32, },
    { Format::RGBA8_SINT,           vk::Format(VK_FORMAT_R8G8B8A8_SINT),             32, },
    { Format::RGBA8_UNORM,          vk::Format(VK_FORMAT_R8G8B8A8_UNORM),            32, },
    { Format::RGBA8_SNORM,          vk::Format(VK_FORMAT_R8G8B8A8_SNORM),            32, },
    { Format::BGRA8_UNORM,          vk::Format(VK_FORMAT_B8G8R8A8_UNORM),            32, },
    { Format::SRGBA8_UNORM,         vk::Format(VK_FORMAT_R8G8B8A8_SRGB),             32, },
    { Format::SBGRA8_UNORM,         vk::Format(VK_FORMAT_B8G8R8A8_SRGB),             32, },
    { Format::R10G10B10A2_UNORM,    vk::Format(VK_FORMAT_A2R10G10B10_UNORM_PACK32),  32, }, // xxxnsubtil: not quite right
    { Format::R11G11B10_FLOAT,      vk::Format(VK_FORMAT_B10G11R11_UFLOAT_PACK32),   32, }, // xxxnsubtil: not quite right
    { Format::RG16_UINT,            vk::Format(VK_FORMAT_R16G16_UINT),               32, },
    { Format::RG16_SINT,            vk::Format(VK_FORMAT_R16G16_SINT),               32, },
    { Format::RG16_UNORM,           vk::Format(VK_FORMAT_R16G16_UNORM),              32, },
    { Format::RG16_SNORM,           vk::Format(VK_FORMAT_R16G16_SNORM),              32, },
    { Format::RG16_FLOAT,           vk::Format(VK_FORMAT_R16G16_SFLOAT),             32, },
    { Format::R32_UINT,             vk::Format(VK_FORMAT_R32_UINT),                  32, },
    { Format::R32_SINT,             vk::Format(VK_FORMAT_R32_SINT),                  32, },
    { Format::R32_FLOAT,            vk::Format(VK_FORMAT_R32_SFLOAT),                32, },
    { Format::RGBA16_UINT,          vk::Format(VK_FORMAT_R16G16B16A16_UINT),         64, },
    { Format::RGBA16_SINT,          vk::Format(VK_FORMAT_R16G16B16A16_SINT),         64, },
    { Format::RGBA16_FLOAT,         vk::Format(VK_FORMAT_R16G16B16A16_SFLOAT),       64, },
    { Format::RGBA16_UNORM,         vk::Format(VK_FORMAT_R16G16B16A16_UNORM),        64, },
    { Format::RGBA16_SNORM,         vk::Format(VK_FORMAT_R16G16B16A16_SNORM),        64, },
    { Format::RG32_UINT,            vk::Format(VK_FORMAT_R32G32_UINT),               64, },
    { Format::RG32_SINT,            vk::Format(VK_FORMAT_R32G32_SINT),               64, },
    { Format::RG32_FLOAT,           vk::Format(VK_FORMAT_R32G32_SFLOAT),             64, },
    { Format::RGB32_UINT,           vk::Format(VK_FORMAT_R32G32B32_UINT),            96, },
    { Format::RGB32_SINT,           vk::Format(VK_FORMAT_R32G32B32_SINT),            96, },
    { Format::RGB32_FLOAT,          vk::Format(VK_FORMAT_R32G32B32_SFLOAT),          96, },
    { Format::RGBA32_UINT,          vk::Format(VK_FORMAT_R32G32B32A32_UINT),         128, },
    { Format::RGBA32_SINT,          vk::Format(VK_FORMAT_R32G32B32A32_SINT),         128, },
    { Format::RGBA32_FLOAT,         vk::Format(VK_FORMAT_R32G32B32A32_SFLOAT),       128, },

    { Format::D16,                  vk::Format(VK_FORMAT_D16_UNORM),                 16, },
    { Format::D24S8,                vk::Format(VK_FORMAT_D24_UNORM_S8_UINT),         32, },
    { Format::X24G8_UINT,           vk::Format(VK_FORMAT_D24_UNORM_S8_UINT),         32, }, // xxxnsubtil: is this correct?
    { Format::D32,                  vk::Format(VK_FORMAT_D32_SFLOAT),                32, },
    { Format::D32S8,                vk::Format(VK_FORMAT_D32_SFLOAT_S8_UINT),        64, },
    { Format::X32G8_UINT,           vk::Format(VK_FORMAT_D32_SFLOAT_S8_UINT),        64, },

    { Format::BC1_UNORM,            vk::Format(VK_FORMAT_BC1_RGB_UNORM_BLOCK),       64,  4, },
    { Format::BC1_UNORM_SRGB,       vk::Format(VK_FORMAT_BC1_RGB_SRGB_BLOCK),        64,  4, },
    { Format::BC2_UNORM,            vk::Format(VK_FORMAT_BC2_UNORM_BLOCK),           128, 4, },
    { Format::BC2_UNORM_SRGB,       vk::Format(VK_FORMAT_BC2_SRGB_BLOCK),            128, 4, },
    { Format::BC3_UNORM,            vk::Format(VK_FORMAT_BC3_UNORM_BLOCK),           128, 4, },
    { Format::BC3_UNORM_SRGB,       vk::Format(VK_FORMAT_BC3_SRGB_BLOCK),            128, 4, },
    { Format::BC4_UNORM,            vk::Format(VK_FORMAT_BC4_UNORM_BLOCK),           64,  4, },
    { Format::BC4_SNORM,            vk::Format(VK_FORMAT_BC4_SNORM_BLOCK),           64,  4, },
    { Format::BC5_UNORM,            vk::Format(VK_FORMAT_BC5_UNORM_BLOCK),           128, 4, },
    { Format::BC5_SNORM,            vk::Format(VK_FORMAT_BC5_SNORM_BLOCK),           128, 4, },
    { Format::BC6H_UFLOAT,          vk::Format(VK_FORMAT_BC6H_UFLOAT_BLOCK),         128, 4, },
    { Format::BC6H_SFLOAT,          vk::Format(VK_FORMAT_BC6H_SFLOAT_BLOCK),         128, 4, },
    { Format::BC7_UNORM,            vk::Format(VK_FORMAT_BC7_UNORM_BLOCK),           128, 4, },
    { Format::BC7_UNORM_SRGB,       vk::Format(VK_FORMAT_BC7_SRGB_BLOCK),            128, 4, },

} };

vk::Format convertFormat(nvrhi::Format format)
{
    assert(format < nvrhi::Format::COUNT);
    assert(formatMap[uint32_t(format)].rhiFormat == format);

    return formatMap[uint32_t(format)].vkFormat;
}

uint32_t formatElementSizeBits(nvrhi::Format format)
{
    assert(format < nvrhi::Format::COUNT);
    assert(formatMap[uint32_t(format)].rhiFormat == format);

    return formatMap[uint32_t(format)].elementSizeBits;
}

uint32_t formatBlockSize(nvrhi::Format format)
{
    assert(format < nvrhi::Format::COUNT);
    assert(formatMap[uint32_t(format)].rhiFormat == format);

    return formatMap[uint32_t(format)].blockSize;
}

vk::SamplerAddressMode convertSamplerAddressMode(nvrhi::SamplerDesc::WrapMode mode)
{
    switch(mode)
    {
        case SamplerDesc::WRAP_MODE_CLAMP:
            return vk::SamplerAddressMode::eClampToEdge;

        case SamplerDesc::WRAP_MODE_WRAP:
            return vk::SamplerAddressMode::eRepeat;

        case SamplerDesc::WRAP_MODE_BORDER:
            return vk::SamplerAddressMode::eClampToBorder;

        default:
            assert(!"can't happen");
            return vk::SamplerAddressMode(0);
    }
}

} // namespace vulkan
} // namespace nvrhi
