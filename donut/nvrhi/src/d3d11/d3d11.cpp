/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/d3d11/d3d11.h>
#include <nvrhi/common/crc.h>
#include <nvrhi/common/containers.h>
#include <nvrhi/common/shader-blob.h>

#include <assert.h>
#include <algorithm>

#ifndef NVRHI_D3D11_WITH_NVAPI
#define NVRHI_D3D11_WITH_NVAPI 1
#endif

#if NVRHI_D3D11_WITH_NVAPI
#include <nvapi.h>
#ifdef _WIN64
#pragma comment(lib, "nvapi64.lib")
#else
#pragma comment(lib, "nvapi.lib")
#endif
#endif

#pragma comment(lib, "dxguid.lib")

#ifdef _DEBUG
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg, __FILE__, __LINE__)
#else
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg)
#endif

#pragma warning(disable:4127) // conditional expression is constant

namespace nvrhi 
{
namespace d3d11
{
    // Format mapping table. The rows must be in the exactly same order as Format enum members are defined.
    const FormatMapping FormatMappings[] = {
        { Format::UNKNOWN,              DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                DXGI_FORMAT_UNKNOWN,                0, false },

        { Format::R8_UINT,              DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UINT,                DXGI_FORMAT_R8_UINT,                8, false },
        { Format::R8_SINT,              DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SINT,                DXGI_FORMAT_R8_SINT,                8, false },
        { Format::R8_UNORM,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_UNORM,               DXGI_FORMAT_R8_UNORM,               8, false },
        { Format::R8_SNORM,             DXGI_FORMAT_R8_TYPELESS,            DXGI_FORMAT_R8_SNORM,               DXGI_FORMAT_R8_SNORM,               8, false },
        { Format::RG8_UINT,             DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UINT,              DXGI_FORMAT_R8G8_UINT,              16, false },
        { Format::RG8_SINT,             DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SINT,              DXGI_FORMAT_R8G8_SINT,              16, false },
        { Format::RG8_UNORM,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_UNORM,             DXGI_FORMAT_R8G8_UNORM,             16, false },
        { Format::RG8_SNORM,            DXGI_FORMAT_R8G8_TYPELESS,          DXGI_FORMAT_R8G8_SNORM,             DXGI_FORMAT_R8G8_SNORM,             16, false },
        { Format::R16_UINT,             DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UINT,               DXGI_FORMAT_R16_UINT,               16, false },
        { Format::R16_SINT,             DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SINT,               DXGI_FORMAT_R16_SINT,               16, false },
        { Format::R16_UNORM,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,              DXGI_FORMAT_R16_UNORM,              16, false },
        { Format::R16_SNORM,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_SNORM,              DXGI_FORMAT_R16_SNORM,              16, false },
        { Format::R16_FLOAT,            DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_FLOAT,              DXGI_FORMAT_R16_FLOAT,              16, false },
        { Format::BGRA4_UNORM,          DXGI_FORMAT_B4G4R4A4_UNORM,         DXGI_FORMAT_B4G4R4A4_UNORM,         DXGI_FORMAT_B4G4R4A4_UNORM,         16, false },
        { Format::B5G6R5_UNORM,         DXGI_FORMAT_B5G6R5_UNORM,           DXGI_FORMAT_B5G6R5_UNORM,           DXGI_FORMAT_B5G6R5_UNORM,           16, false },
        { Format::B5G5R5A1_UNORM,       DXGI_FORMAT_B5G5R5A1_UNORM,         DXGI_FORMAT_B5G5R5A1_UNORM,         DXGI_FORMAT_B5G5R5A1_UNORM,         16, false },
        { Format::RGBA8_UINT,           DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UINT,          DXGI_FORMAT_R8G8B8A8_UINT,          32, false },
        { Format::RGBA8_SINT,           DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SINT,          DXGI_FORMAT_R8G8B8A8_SINT,          32, false },
        { Format::RGBA8_UNORM,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM,         DXGI_FORMAT_R8G8B8A8_UNORM,         32, false },
        { Format::RGBA8_SNORM,          DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_SNORM,         DXGI_FORMAT_R8G8B8A8_SNORM,         32, false },
        { Format::BGRA8_UNORM,          DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM,         DXGI_FORMAT_B8G8R8A8_UNORM,         32, false },
        { Format::SRGBA8_UNORM,         DXGI_FORMAT_R8G8B8A8_TYPELESS,      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,    32, false },
        { Format::SBGRA8_UNORM,         DXGI_FORMAT_B8G8R8A8_TYPELESS,      DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,    32, false },
        { Format::R10G10B10A2_UNORM,    DXGI_FORMAT_R10G10B10A2_TYPELESS,   DXGI_FORMAT_R10G10B10A2_UNORM,      DXGI_FORMAT_R10G10B10A2_UNORM,      32, false },
        { Format::R11G11B10_FLOAT,      DXGI_FORMAT_R11G11B10_FLOAT,        DXGI_FORMAT_R11G11B10_FLOAT,        DXGI_FORMAT_R11G11B10_FLOAT,        32, false },
        { Format::RG16_UINT,            DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UINT,            DXGI_FORMAT_R16G16_UINT,            32, false },
        { Format::RG16_SINT,            DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SINT,            DXGI_FORMAT_R16G16_SINT,            32, false },
        { Format::RG16_UNORM,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_UNORM,           DXGI_FORMAT_R16G16_UNORM,           32, false },
        { Format::RG16_SNORM,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_SNORM,           DXGI_FORMAT_R16G16_SNORM,           32, false },
        { Format::RG16_FLOAT,           DXGI_FORMAT_R16G16_TYPELESS,        DXGI_FORMAT_R16G16_FLOAT,           DXGI_FORMAT_R16G16_FLOAT,           32, false },
        { Format::R32_UINT,             DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_UINT,               DXGI_FORMAT_R32_UINT,               32, false },
        { Format::R32_SINT,             DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_SINT,               DXGI_FORMAT_R32_SINT,               32, false },
        { Format::R32_FLOAT,            DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,              DXGI_FORMAT_R32_FLOAT,              32, false },
        { Format::RGBA16_UINT,          DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UINT,      DXGI_FORMAT_R16G16B16A16_UINT,      64, false },
        { Format::RGBA16_SINT,          DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SINT,      DXGI_FORMAT_R16G16B16A16_SINT,      64, false },
        { Format::RGBA16_FLOAT,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_FLOAT,     DXGI_FORMAT_R16G16B16A16_FLOAT,     64, false },
        { Format::RGBA16_UNORM,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_UNORM,     DXGI_FORMAT_R16G16B16A16_UNORM,     64, false },
        { Format::RGBA16_SNORM,         DXGI_FORMAT_R16G16B16A16_TYPELESS,  DXGI_FORMAT_R16G16B16A16_SNORM,     DXGI_FORMAT_R16G16B16A16_SNORM,     64, false },
        { Format::RG32_UINT,            DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_UINT,            DXGI_FORMAT_R32G32_UINT,            64, false },
        { Format::RG32_SINT,            DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_SINT,            DXGI_FORMAT_R32G32_SINT,            64, false },
        { Format::RG32_FLOAT,           DXGI_FORMAT_R32G32_TYPELESS,        DXGI_FORMAT_R32G32_FLOAT,           DXGI_FORMAT_R32G32_FLOAT,           64, false },
        { Format::RGB32_UINT,           DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_UINT,         DXGI_FORMAT_R32G32B32_UINT,         96, false },
        { Format::RGB32_SINT,           DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_SINT,         DXGI_FORMAT_R32G32B32_SINT,         96, false },
        { Format::RGB32_FLOAT,          DXGI_FORMAT_R32G32B32_TYPELESS,     DXGI_FORMAT_R32G32B32_FLOAT,        DXGI_FORMAT_R32G32B32_FLOAT,        96, false },
        { Format::RGBA32_UINT,          DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_UINT,      DXGI_FORMAT_R32G32B32A32_UINT,      128, false },
        { Format::RGBA32_SINT,          DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_SINT,      DXGI_FORMAT_R32G32B32A32_SINT,      128, false },
        { Format::RGBA32_FLOAT,         DXGI_FORMAT_R32G32B32A32_TYPELESS,  DXGI_FORMAT_R32G32B32A32_FLOAT,     DXGI_FORMAT_R32G32B32A32_FLOAT,     128, false },

        { Format::D16,                  DXGI_FORMAT_R16_TYPELESS,           DXGI_FORMAT_R16_UNORM,              DXGI_FORMAT_D16_UNORM,              16, true },
        { Format::D24S8,                DXGI_FORMAT_R24G8_TYPELESS,         DXGI_FORMAT_R24_UNORM_X8_TYPELESS,  DXGI_FORMAT_D24_UNORM_S8_UINT,      32, true },
        { Format::X24G8_UINT,           DXGI_FORMAT_R24G8_TYPELESS,         DXGI_FORMAT_X24_TYPELESS_G8_UINT,   DXGI_FORMAT_D24_UNORM_S8_UINT,      32, true },
        { Format::D32,                  DXGI_FORMAT_R32_TYPELESS,           DXGI_FORMAT_R32_FLOAT,              DXGI_FORMAT_D32_FLOAT,              32, true },
        { Format::D32S8,                DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 64, true },
        { Format::X32G8_UINT,           DXGI_FORMAT_R32G8X24_TYPELESS,      DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 64, true },

        { Format::BC1_UNORM,            DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM,              DXGI_FORMAT_BC1_UNORM,              4, true },
        { Format::BC1_UNORM_SRGB,       DXGI_FORMAT_BC1_TYPELESS,           DXGI_FORMAT_BC1_UNORM_SRGB,         DXGI_FORMAT_BC1_UNORM_SRGB,         4, true },
        { Format::BC2_UNORM,            DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM,              DXGI_FORMAT_BC2_UNORM,              8, true },
        { Format::BC2_UNORM_SRGB,       DXGI_FORMAT_BC2_TYPELESS,           DXGI_FORMAT_BC2_UNORM_SRGB,         DXGI_FORMAT_BC2_UNORM_SRGB,         8, true },
        { Format::BC3_UNORM,            DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM,              DXGI_FORMAT_BC3_UNORM,              8, true },
        { Format::BC3_UNORM_SRGB,       DXGI_FORMAT_BC3_TYPELESS,           DXGI_FORMAT_BC3_UNORM_SRGB,         DXGI_FORMAT_BC3_UNORM_SRGB,         8, true },
        { Format::BC4_UNORM,            DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_UNORM,              DXGI_FORMAT_BC4_UNORM,              4, true },
        { Format::BC4_SNORM,            DXGI_FORMAT_BC4_TYPELESS,           DXGI_FORMAT_BC4_SNORM,              DXGI_FORMAT_BC4_SNORM,              4, true },
        { Format::BC5_UNORM,            DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_UNORM,              DXGI_FORMAT_BC5_UNORM,              8, true },
        { Format::BC5_SNORM,            DXGI_FORMAT_BC5_TYPELESS,           DXGI_FORMAT_BC5_SNORM,              DXGI_FORMAT_BC5_SNORM,              8, true },
        { Format::BC6H_UFLOAT,          DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_UF16,              DXGI_FORMAT_BC6H_UF16,              8, true },
        { Format::BC6H_SFLOAT,          DXGI_FORMAT_BC6H_TYPELESS,          DXGI_FORMAT_BC6H_SF16,              DXGI_FORMAT_BC6H_SF16,              8, true },
        { Format::BC7_UNORM,            DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM,              DXGI_FORMAT_BC7_UNORM,              8, true },
        { Format::BC7_UNORM_SRGB,       DXGI_FORMAT_BC7_TYPELESS,           DXGI_FORMAT_BC7_UNORM_SRGB,         DXGI_FORMAT_BC7_UNORM_SRGB,         8, true },
    };

    const FormatMapping& GetFormatMapping(Format abstractFormat)
    {
        static_assert(sizeof(FormatMappings) / sizeof(FormatMapping) == size_t(Format::COUNT), "The format mapping table doesn't have the right number of elements");

        const FormatMapping& mapping = FormatMappings[uint32_t(abstractFormat)];
        assert(mapping.abstractFormat == abstractFormat);
        return mapping;
    }

    void SetDebugName(ID3D11DeviceChild* pObject, const char* name)
    {
        D3D_SET_OBJECT_NAME_N_A(pObject, UINT(strlen(name)), name);
    }

    Object Texture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Resource:
            return Object(resource);
        default:
            return nullptr;
        }
    }

    Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV)
    {
        switch (objectType)
        {
        case nvrhi::ObjectTypes::D3D11_RenderTargetView:
            return parent->getRTVForTexture(this, format, subresources);
        case nvrhi::ObjectTypes::D3D11_DepthStencilView:
            return parent->getDSVForTexture(this, subresources, isReadOnlyDSV);
        case nvrhi::ObjectTypes::D3D11_ShaderResourceView:
            return parent->getSRVForTexture(this, format, subresources);
        case nvrhi::ObjectTypes::D3D11_UnorderedAccessView:
            return parent->getUAVForTexture(this, format, subresources);
        default:
            return nullptr;
        }
    }

    Object Buffer::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Resource:
            return Object(static_cast<ID3D11Resource*>(resource.Get()));
        case ObjectTypes::D3D11_Buffer:
            return Object(resource.Get());
        default:
            return nullptr;
        }
    }


    Device::~Device()
    {
        clearCachedData();
    }


    Device::Device(IMessageCallback* messageCallback, ID3D11DeviceContext* context)
        : context(context)
        , messageCallback(messageCallback)
        , nvapiIsInitalized(false)
        , m_CurrentGraphicsStateValid(false)
        , m_CurrentComputeStateValid(false)
        , m_SinglePassStereoSupported(false)
    {
        this->context->GetDevice(&device);

#if NVRHI_D3D11_WITH_NVAPI
        //We need to use NVAPI to set resource hints for SLI
        nvapiIsInitalized = NvAPI_Initialize() == NVAPI_OK;

        if (nvapiIsInitalized)
        {
            NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams = { 0 };
            stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;

            if (NvAPI_D3D_QuerySinglePassStereoSupport(device, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
            {
                m_SinglePassStereoSupported = true;
            }
        }
#endif

        context->QueryInterface(IID_PPV_ARGS(&userDefinedAnnotation));
    }

    TextureHandle Device::createTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        D3D11_USAGE usage = (cpuAccess == CpuAccessMode::None ? D3D11_USAGE_DEFAULT : D3D11_USAGE_STAGING);

		const FormatMapping& formatMapping = GetFormatMapping(d.format);

        //convert flags
        UINT bindFlags;
        if (cpuAccess != CpuAccessMode::None)
        {
            bindFlags = 0;
        } else {
            bindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (d.isRenderTarget)
                bindFlags |= (d.format == Format::D16 || d.format == Format::D24S8 || d.format == Format::D32) ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
            if (d.isUAV)
                bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        UINT cpuAccessFlags = 0;
        if (cpuAccess == CpuAccessMode::Read)
            cpuAccessFlags = D3D11_CPU_ACCESS_READ;
        if (cpuAccess == CpuAccessMode::Write)
            cpuAccessFlags = D3D11_CPU_ACCESS_WRITE;

        nvrhi::RefCountPtr<ID3D11Resource> pResource;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray: {

            D3D11_TEXTURE1D_DESC desc11;
            desc11.Width = d.width;
            desc11.MipLevels = d.mipLevels;
            desc11.ArraySize = d.arraySize;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;
            desc11.MiscFlags = 0;

            RefCountPtr<ID3D11Texture1D> newTexture;
            HRESULT r = device->CreateTexture1D(&desc11, nullptr, &newTexture);
            CHECK_ERROR(SUCCEEDED(r), "Creating a Texture1D failed");

            pResource = newTexture;
            break;
        }
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray: {

            D3D11_TEXTURE2D_DESC desc11;
            desc11.Width = d.width;
            desc11.Height = d.height;
            desc11.MipLevels = d.mipLevels;
            desc11.ArraySize = d.arraySize;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.SampleDesc.Count = d.sampleCount;
            desc11.SampleDesc.Quality = d.sampleQuality;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;

            if (d.dimension == TextureDimension::TextureCube || d.dimension == TextureDimension::TextureCubeArray)
                desc11.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            else
                desc11.MiscFlags = 0;

            RefCountPtr<ID3D11Texture2D> newTexture;
            HRESULT r = device->CreateTexture2D(&desc11, nullptr, &newTexture);
            CHECK_ERROR(SUCCEEDED(r), "Creating a Texture2D failed");

            pResource = newTexture;
            break;
        }

        case TextureDimension::Texture3D: {

            D3D11_TEXTURE3D_DESC desc11;
            desc11.Width = d.width;
            desc11.Height = d.height;
            desc11.Depth = d.depth;
            desc11.MipLevels = d.mipLevels;
            desc11.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
            desc11.Usage = usage;
            desc11.BindFlags = bindFlags;
            desc11.CPUAccessFlags = cpuAccessFlags;
            desc11.MiscFlags = 0;

            RefCountPtr<ID3D11Texture3D> newTexture;
            HRESULT r = device->CreateTexture3D(&desc11, nullptr, &newTexture);
            CHECK_ERROR(SUCCEEDED(r), "Creating a Texture3D failed");

            pResource = newTexture;
            break;
        }

        default:
            message(MessageSeverity::Error, "Can't create a texture of unknown dimension");
            return nullptr;
        }

        if (d.disableGPUsSync)
            disableSLIResouceSync(pResource);

        if (d.debugName)
            SetDebugName(pResource, d.debugName);
        
        Texture* texture = new Texture(this);
        texture->desc = d;
        texture->resource = pResource;
        return TextureHandle::Create(texture);
    }

    TextureHandle Device::createTexture(const TextureDesc& d)
    {
        return createTexture(d, CpuAccessMode::None);
    }

    nvrhi::TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
    {
        if (!_texture.pointer)
            return nullptr;

        if (objectType != ObjectTypes::D3D11_Resource)
            return nullptr;

        Texture* texture = new Texture(this);
        texture->desc = desc;
        texture->resource = static_cast<ID3D11Resource*>(_texture.pointer);

        return TextureHandle::Create(texture);
    }
    
    void Device::clearState()
    {
        context->ClearState();

#if NVRHI_D3D11_WITH_NVAPI
        if (m_CurrentGraphicsStateValid && m_CurrentSinglePassStereoState.enabled)
        {
            NvAPI_D3D_SetSinglePassStereoMode(context, 1, 0, 0);
        }
#endif

        m_CurrentGraphicsStateValid = false;
        m_CurrentComputeStateValid = false;

        // Release the strong references to pipeline objects
        m_CurrentGraphicsPipeline = nullptr;
        m_CurrentFramebuffer = nullptr;
        m_CurrentBindings.resize(0);
        m_CurrentVertexBuffers.resize(0);
        m_CurrentIndexBuffer = nullptr;
        m_CurrentComputePipeline = nullptr;
        m_CurrentIndirectBuffer = nullptr;
    }

    void Device::open()
    {
        clearState();
    }

    void Device::close()
    {
        while (m_numUAVOverlapCommands > 0)
            leaveUAVOverlapSection();

        clearState();
    }

    void Device::clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor)
    {
        ID3D11UnorderedAccessView* uav = NULL;
        ID3D11RenderTargetView* rtv = NULL;
        ID3D11DepthStencilView* dsv = NULL;

        subresources = subresources.resolve(texture->GetDesc(), false);

        for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);

            getClearViewForTexture(texture, currentMipSlice, uav, rtv, dsv);

            if (uav)
            {
                context->ClearUnorderedAccessViewFloat(uav, &clearColor.r);
            }
            else if (rtv)
            {
                context->ClearRenderTargetView(rtv, &clearColor.r);
            }
            else if (dsv)
            {
                // interpret clearColor.y as integer stencil
                context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, clearColor.r, static_cast<UINT8>(clearColor.g));
            }
            else
            {
                break; //no more sub-views
            }
        }
    }

    void Device::clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil)
    {
        if (!clearDepth && !clearStencil)
        {
            return;
        }

        Texture* texture = static_cast<Texture*>(t);

        const TextureDesc& textureDesc = texture->desc;

        if ((!textureDesc.isRenderTarget) || 
            (textureDesc.format != Format::D16 && textureDesc.format != Format::D24S8 && textureDesc.format != Format::D32))
        {
            CHECK_ERROR(0, "This resource is not depth/stencil texture");
        }

        subresources = subresources.resolve(texture->GetDesc(), false);

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);

            ID3D11DepthStencilView* dsv = getDSVForTexture(texture, subresources);

            if (dsv)
            {
                UINT clearFlags = D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL;
                if (!clearDepth)
                {
                    clearFlags = D3D11_CLEAR_STENCIL;
                }
                else if (!clearStencil)
                {
                    clearFlags = D3D11_CLEAR_DEPTH;
                }
                context->ClearDepthStencilView(dsv, clearFlags, depth, stencil);
            }
        }
    }

    void Device::clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, uint32_t clearColor)
    {
        ID3D11UnorderedAccessView* uav = NULL;
        ID3D11RenderTargetView* rtv = NULL;
        ID3D11DepthStencilView* dsv = NULL;

        subresources = subresources.resolve(texture->GetDesc(), false);

        for (MipLevel mipLevel = subresources.baseMipLevel; mipLevel < subresources.baseMipLevel + subresources.numMipLevels; mipLevel++)
        {
            TextureSubresourceSet currentMipSlice = TextureSubresourceSet(mipLevel, 1, subresources.baseArraySlice, subresources.numArraySlices);

            getClearViewForTexture(texture, currentMipSlice, uav, rtv, dsv);

            if (uav)
            {
                UINT clearValues[4] = { clearColor, clearColor, clearColor, clearColor };
                context->ClearUnorderedAccessViewUint(uav, clearValues);
            }
            else if (rtv)
            {
                float clearValues[4] = { float(clearColor), float(clearColor), float(clearColor), float(clearColor) };
                context->ClearRenderTargetView(rtv, clearValues);
            }
            else if (dsv)
            {
                context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, float(clearColor), UINT8(clearColor));
            }
            else
            {
                break; //no more sub-views
            }
        }
    }

    StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        assert(cpuAccess != CpuAccessMode::None);
        StagingTexture *ret = new StagingTexture(this);
        TextureHandle t = createTexture(d, cpuAccess);
        ret->texture = static_cast<Texture*>(t.Get());
        ret->cpuAccess = cpuAccess;
        return StagingTextureHandle::Create(ret);
    }

    void *Device::mapStagingTexture(IStagingTexture* _stagingTexture, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        StagingTexture* stagingTexture = static_cast<StagingTexture*>(_stagingTexture);

        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != CpuAccessMode::None);

        Texture *t = stagingTexture->texture;
        auto resolvedSlice = slice.resolve(t->desc);

        D3D11_MAP mapType;
        switch(cpuAccess)
        {
            case CpuAccessMode::Read:
                assert(stagingTexture->cpuAccess == CpuAccessMode::Read);
                mapType = D3D11_MAP_READ;
                break;

            case CpuAccessMode::Write:
                assert(stagingTexture->cpuAccess == CpuAccessMode::Write);
                mapType = D3D11_MAP_WRITE;
                break;

            default:
                message(MessageSeverity::Error, "Unsupported CpuAccessMode in mapStagingTexture");
                return nullptr;
        }

        UINT subresource = D3D11CalcSubresource(resolvedSlice.mipLevel, resolvedSlice.arraySlice, t->desc.mipLevels);

        D3D11_MAPPED_SUBRESOURCE res;
        if (SUCCEEDED(context->Map(t->resource, subresource, mapType, 0, &res)))
        {
            stagingTexture->mappedSubresource = subresource;
            *outRowPitch = (size_t) res.RowPitch;
            return res.pData;
        } else {
            return nullptr;
        }
    }

    void Device::unmapStagingTexture(IStagingTexture* _t)
    {
        StagingTexture* t = static_cast<StagingTexture*>(_t);

        assert(t->mappedSubresource != UINT(-1));
        context->Unmap(t->texture->resource, t->mappedSubresource);
        t->mappedSubresource = UINT(-1);
    }
    
    void Device::copyTexture(ID3D11Resource *dst, const TextureDesc& dstDesc, const TextureSlice& dstSlice,
                                             ID3D11Resource *src, const TextureDesc& srcDesc, const TextureSlice& srcSlice)
    {
        auto resolvedSrcSlice = srcSlice.resolve(srcDesc);
        auto resolvedDstSlice = dstSlice.resolve(dstDesc);

        assert(resolvedDstSlice.width == resolvedSrcSlice.width);
        assert(resolvedDstSlice.height == resolvedSrcSlice.height);

        UINT srcSubresource = D3D11CalcSubresource(resolvedSrcSlice.mipLevel, resolvedSrcSlice.arraySlice, srcDesc.mipLevels);
        UINT dstSubresource = D3D11CalcSubresource(resolvedDstSlice.mipLevel, resolvedDstSlice.arraySlice, dstDesc.mipLevels);

        D3D11_BOX srcBox;
        srcBox.left = resolvedSrcSlice.x;
        srcBox.top = resolvedSrcSlice.y;
        srcBox.front = resolvedSrcSlice.z;
        srcBox.right = resolvedSrcSlice.x + resolvedSrcSlice.width;
        srcBox.bottom = resolvedSrcSlice.y + resolvedSrcSlice.height;
        srcBox.back = resolvedSrcSlice.z + resolvedSrcSlice.depth;

        context->CopySubresourceRegion(dst,
                                       dstSubresource,
                                       resolvedDstSlice.x, resolvedDstSlice.y, resolvedDstSlice.z,
                                       src,
                                       srcSubresource,
                                       &srcBox);
    }

    void Device::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = static_cast<Texture*>(_src);
        Texture* dst = static_cast<Texture*>(_dst);

        copyTexture(dst->resource, dst->desc, dstSlice,
                    src->resource, src->desc, srcSlice);
    }

    void Device::copyTexture(IStagingTexture* _dst, const TextureSlice& dstSlice, ITexture* _src, const TextureSlice& srcSlice)
    {
        Texture* src = static_cast<Texture*>(_src);
        StagingTexture* dst = static_cast<StagingTexture*>(_dst);

        copyTexture(dst->texture->resource, dst->texture->desc, dstSlice,
                    src->resource, src->desc, srcSlice);
    }

    void Device::copyTexture(ITexture* _dst, const TextureSlice& dstSlice, IStagingTexture* _src, const TextureSlice& srcSlice)
    {
        StagingTexture* src = static_cast<StagingTexture*>(_src);
        Texture* dst = static_cast<Texture*>(_dst);

        copyTexture(dst->resource, dst->desc, dstSlice,
                    src->texture->resource, src->texture->desc, srcSlice);
    }

    void Device::writeTexture(ITexture* _dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch)
    {
        Texture* dest = static_cast<Texture*>(_dest);

        UINT subresource = D3D11CalcSubresource(mipLevel, arraySlice, dest->desc.mipLevels);

        context->UpdateSubresource(dest->resource, subresource, nullptr, data, UINT(rowPitch), UINT(depthPitch));
    }

    void Device::resolveTexture(ITexture* _dest, const TextureSubresourceSet& dstSubresources, ITexture* _src, const TextureSubresourceSet& srcSubresources)
    {
        Texture* dest = static_cast<Texture*>(_dest);
        Texture* src = static_cast<Texture*>(_src);

        TextureSubresourceSet dstSR = dstSubresources.resolve(dest->desc, false);
        TextureSubresourceSet srcSR = srcSubresources.resolve(src->desc, false);

        if (dstSR.numArraySlices != srcSR.numArraySlices || dstSR.numMipLevels != srcSR.numMipLevels)
            // let the validation layer handle the messages
            return;

        const FormatMapping& formatMapping = GetFormatMapping(dest->desc.format);

        for (ArraySlice arrayIndex = 0; arrayIndex < dstSR.numArraySlices; arrayIndex++)
        {
            for (MipLevel mipLevel = 0; mipLevel < dstSR.numMipLevels; mipLevel++)
            {
                uint32_t dstSubresource = D3D11CalcSubresource(mipLevel + dstSR.baseMipLevel, arrayIndex + dstSR.baseArraySlice, dest->desc.mipLevels);
                uint32_t srcSubresource = D3D11CalcSubresource(mipLevel + srcSR.baseMipLevel, arrayIndex + srcSR.baseArraySlice, src->desc.mipLevels);
                context->ResolveSubresource(dest->resource, dstSubresource, src->resource, srcSubresource, formatMapping.rtvFormat);
            }
        }
    }

    BufferHandle Device::createBuffer(const BufferDesc& d)
    {
        D3D11_BUFFER_DESC desc11 = {};
        desc11.ByteWidth = d.byteSize;

        //These don't map exactly, but it should be generally correct
        switch(d.cpuAccess)
        {
            case CpuAccessMode::None:
                desc11.Usage = D3D11_USAGE_DEFAULT;
                desc11.CPUAccessFlags = 0;
                break;

            case CpuAccessMode::Read:
                desc11.Usage = D3D11_USAGE_STAGING;
                desc11.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                break;

            case CpuAccessMode::Write:
                desc11.Usage = D3D11_USAGE_DYNAMIC;
                desc11.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                break;
        }

        if (d.isConstantBuffer)
        {
            desc11.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        } else 
        {
            desc11.BindFlags = 0;

            if (desc11.Usage != D3D11_USAGE_STAGING)
                desc11.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
            
            if (d.canHaveUAVs)
                desc11.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

            if (d.isIndexBuffer)
                desc11.BindFlags |= D3D11_BIND_INDEX_BUFFER;

            if (d.isVertexBuffer)
                desc11.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        }

        desc11.MiscFlags = 0;
        if (d.isDrawIndirectArgs)
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

        if (d.structStride != 0)
            desc11.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        desc11.StructureByteStride = (UINT)d.structStride;

        RefCountPtr<ID3D11Buffer> newBuffer;
        CHECK_ERROR(SUCCEEDED(device->CreateBuffer(&desc11, NULL, &newBuffer)), "Creation failed");

        if (d.disableGPUsSync)
            disableSLIResouceSync(newBuffer);

        if (d.debugName)
            SetDebugName(newBuffer, d.debugName);

        Buffer* buffer = new Buffer(this);
        buffer->desc = d;
        buffer->resource = newBuffer;
        return BufferHandle::Create(buffer);
    }

    void Device::writeBuffer(IBuffer* _buffer, const void* data, size_t dataSize, size_t destOffsetBytes)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (buffer->desc.cpuAccess == CpuAccessMode::Write)
        {
            // we can map if it it's D3D11_USAGE_DYNAMIC, but not UpdateSubresource
            D3D11_MAPPED_SUBRESOURCE mappedData;
            D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD;
            if (destOffsetBytes > 0 || dataSize + destOffsetBytes < buffer->desc.byteSize)
                mapType = D3D11_MAP_WRITE;
            CHECK_ERROR(SUCCEEDED(context->Map(buffer->resource, 0, mapType, 0, &mappedData)), "Map failed");
            memcpy((char*)mappedData.pData + destOffsetBytes, data, dataSize);
            context->Unmap(buffer->resource, 0);
        }
        else
        {
            D3D11_BOX box = { UINT(destOffsetBytes), 0, 0, UINT(destOffsetBytes + dataSize), 1, 1 };
            bool useBox = destOffsetBytes > 0 || dataSize < buffer->desc.byteSize;

            context->UpdateSubresource(buffer->resource, 0, useBox ? &box : nullptr, data, (UINT)dataSize, 0);
        }
    }

    void Device::clearBufferUInt(IBuffer* buffer, uint32_t clearValue)
    {
        ID3D11UnorderedAccessView* uav = getUAVForBuffer(buffer, Format::UNKNOWN, EntireBuffer);

        UINT clearValues[4] = { clearValue, clearValue, clearValue, clearValue };
        context->ClearUnorderedAccessViewUint(uav, clearValues);
    }

    void Device::copyBuffer(IBuffer* _dest, uint32_t destOffsetBytes, IBuffer* _src, uint32_t srcOffsetBytes, size_t dataSizeBytes)
    {
        Buffer* dest = static_cast<Buffer*>(_dest);
        Buffer* src = static_cast<Buffer*>(_src);

        //Do a 1D copy
        D3D11_BOX srcBox;
        srcBox.left = (UINT)srcOffsetBytes;
        srcBox.right = (UINT)(srcOffsetBytes + dataSizeBytes);
        srcBox.bottom = 1;
        srcBox.top = 0;
        srcBox.front = 0;
        srcBox.back = 1;
        context->CopySubresourceRegion(dest->resource, 0, (UINT)destOffsetBytes, 0, 0, src->resource, 0, &srcBox);
    }
    
    void *Device::mapBuffer(IBuffer* _buffer, CpuAccessMode flags)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        D3D11_MAP mapType;
        switch(flags)
        {
            case CpuAccessMode::Read:
                assert(buffer->desc.cpuAccess == CpuAccessMode::Read);
                mapType = D3D11_MAP_READ;
                break;

            case CpuAccessMode::Write:
                assert(buffer->desc.cpuAccess == CpuAccessMode::Write);
                mapType = D3D11_MAP_WRITE_DISCARD;
                break;

            default:
                message(MessageSeverity::Error, "Unsupported CpuAccessMode in mapBuffer");
                return nullptr;
        }

        D3D11_MAPPED_SUBRESOURCE res;
        if (SUCCEEDED(context->Map(buffer->resource, 0, mapType, 0, &res)))
        {
            return res.pData;
        } else {
            return nullptr;
        }
    }

    void Device::unmapBuffer(IBuffer* _buffer)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        context->Unmap(buffer->resource, 0);
    }

    nvrhi::BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc)
    {
        if (!_buffer.pointer)
            return nullptr;

        if (objectType != ObjectTypes::D3D11_Buffer)
            return nullptr;

        ID3D11Buffer* pBuffer = static_cast<ID3D11Buffer*>(_buffer.pointer);

        Buffer* buffer = new Buffer(this);
        buffer->desc = desc;
        buffer->resource = pBuffer;
        return BufferHandle::Create(buffer);
    }

    ShaderHandle Device::createShader(const ShaderDesc& d, const void* binary, const size_t binarySize)
    {
        // Attach a RefCountPtr right away so that it's destroyed on an error exit
        RefCountPtr<Shader> shader = RefCountPtr<Shader>::Create(new Shader(this));

        switch (d.shaderType)
        {
        case ShaderType::SHADER_VERTEX:
        {
            // Save the bytecode for potential input layout creation later
            shader->bytecode.resize(binarySize);
            memcpy(shader->bytecode.data(), binary, binarySize);

            if (d.numCustomSemantics == 0)
            {
                CHECK_ERROR(SUCCEEDED(device->CreateVertexShader(binary, binarySize, NULL, &shader->VS)), "Creating VS failed");
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                NvAPI_D3D11_CREATE_VERTEX_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEVERTEXSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = d.pCustomSemantics;
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateVertexShaderEx(device, binary, binarySize, nullptr, &Args, &shader->VS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::SHADER_HULL:
        {
            if (d.numCustomSemantics == 0)
            {
                CHECK_ERROR(SUCCEEDED(device->CreateHullShader(binary, binarySize, NULL, &shader->HS)), "Creating HS failed");
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                NvAPI_D3D11_CREATE_HULL_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEHULLSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = d.pCustomSemantics;
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateHullShaderEx(device, binary, binarySize, nullptr, &Args, &shader->HS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::SHADER_DOMAIN:
        {
            if (d.numCustomSemantics == 0)
            {
                CHECK_ERROR(SUCCEEDED(device->CreateDomainShader(binary, binarySize, NULL, &shader->DS)), "Creating DS failed");
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                NvAPI_D3D11_CREATE_DOMAIN_SHADER_EX Args = {};
                Args.version = NVAPI_D3D11_CREATEDOMAINSHADEREX_VERSION;
                Args.NumCustomSemantics = d.numCustomSemantics;
                Args.pCustomSemantics = d.pCustomSemantics;
                Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                if (NvAPI_D3D11_CreateDomainShaderEx(device, binary, binarySize, nullptr, &Args, &shader->DS) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }
        }
        break;
        case ShaderType::SHADER_GEOMETRY:
        {
            if (d.numCustomSemantics == 0 && uint32_t(d.fastGSFlags) == 0 && d.pCoordinateSwizzling == nullptr)
            {
                CHECK_ERROR(SUCCEEDED(device->CreateGeometryShader(binary, binarySize, NULL, &shader->GS)), "Creating GS failed");
            }
            else
            {
#if NVRHI_D3D11_WITH_NVAPI
                if ((d.fastGSFlags & FastGeometryShaderFlags::COMPATIBILITY_MODE) && (d.fastGSFlags & FastGeometryShaderFlags::FORCE_FAST_GS))
                {
                    CHECK_ERROR(d.numCustomSemantics == 0, "Compatibility mode FastGS does not support custom semantics");

                    NvAPI_D3D11_CREATE_FASTGS_EXPLICIT_DESC Args = {};
                    Args.version = NVAPI_D3D11_CREATEFASTGSEXPLICIT_VER;
                    Args.pCoordinateSwizzling = d.pCoordinateSwizzling;
                    Args.flags = 0;

                    if (d.fastGSFlags & FastGeometryShaderFlags::USE_VIEWPORT_MASK)
                        Args.flags |= NV_FASTGS_USE_VIEWPORT_MASK;

                    if (d.fastGSFlags & FastGeometryShaderFlags::OFFSET_RT_INDEX_BY_VP_INDEX)
                        Args.flags |= NV_FASTGS_OFFSET_RT_INDEX_BY_VP_INDEX;

                    if (d.fastGSFlags & FastGeometryShaderFlags::STRICT_API_ORDER)
                        Args.flags |= NV_FASTGS_STRICT_API_ORDER;

                    if (NvAPI_D3D11_CreateFastGeometryShaderExplicit(device, binary, binarySize, nullptr, &Args, &shader->GS) != NVAPI_OK)
                        return nullptr;
                }
                else
                {
                    NvAPI_D3D11_CREATE_GEOMETRY_SHADER_EX Args = {};
                    Args.version = NVAPI_D3D11_CREATEGEOMETRYSHADEREX_2_VERSION;
                    Args.NumCustomSemantics = d.numCustomSemantics;
                    Args.pCustomSemantics = d.pCustomSemantics;
                    Args.UseCoordinateSwizzle = d.pCoordinateSwizzling != nullptr;
                    Args.pCoordinateSwizzling = d.pCoordinateSwizzling;
                    Args.ForceFastGS = (d.fastGSFlags & FastGeometryShaderFlags::FORCE_FAST_GS) != 0;
                    Args.UseViewportMask = (d.fastGSFlags & FastGeometryShaderFlags::USE_VIEWPORT_MASK) != 0;
                    Args.OffsetRtIndexByVpIndex = (d.fastGSFlags & FastGeometryShaderFlags::OFFSET_RT_INDEX_BY_VP_INDEX) != 0;
                    Args.DontUseViewportOrder = (d.fastGSFlags & FastGeometryShaderFlags::STRICT_API_ORDER) != 0;
                    Args.UseSpecificShaderExt = d.useSpecificShaderExt;

                    if (NvAPI_D3D11_CreateGeometryShaderEx_2(device, binary, binarySize, nullptr, &Args, &shader->GS) != NVAPI_OK)
                        return nullptr;
                }
#else
                 return nullptr;
#endif
            }
        }
        break;
        case ShaderType::SHADER_PIXEL:
        {
            if (d.hlslExtensionsUAV >= 0)
            {
#if NVRHI_D3D11_WITH_NVAPI
                if (NvAPI_D3D11_SetNvShaderExtnSlot(device, d.hlslExtensionsUAV) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }

            CHECK_ERROR(SUCCEEDED(device->CreatePixelShader(binary, binarySize, NULL, &shader->PS)), "Creating PS failed");

#if NVRHI_D3D11_WITH_NVAPI
            if (d.hlslExtensionsUAV >= 0)
            {
                NvAPI_D3D11_SetNvShaderExtnSlot(device, ~0u);
            }
#endif
        }
        break;
        case ShaderType::SHADER_COMPUTE:
        {
            if (d.hlslExtensionsUAV >= 0)
            {
#if NVRHI_D3D11_WITH_NVAPI
                if (NvAPI_D3D11_SetNvShaderExtnSlot(device, d.hlslExtensionsUAV) != NVAPI_OK)
                    return nullptr;
#else
                return nullptr;
#endif
            }

            CHECK_ERROR(SUCCEEDED(device->CreateComputeShader(binary, binarySize, NULL, &shader->CS)), "Creating CS failed");

#if NVRHI_D3D11_WITH_NVAPI
            if (d.hlslExtensionsUAV >= 0)
            {
                NvAPI_D3D11_SetNvShaderExtnSlot(device, ~0u);
            }
#endif
        }
        break;
        }

        shader->desc = d;
        return shader;
    }
    
    ShaderHandle Device::createShaderPermutation(const ShaderDesc& d, const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound)
    {
        const void* binary = nullptr;
        size_t binarySize = 0;

        if (FindPermutationInBlob(blob, blobSize, constants, numConstants, &binary, &binarySize))
        {
            return createShader(d, binary, binarySize);
        }
        else
        {
            if (errorIfNotFound)
            {
                message(MessageSeverity::Error, FormatShaderNotFoundMessage(blob, blobSize, constants, numConstants).c_str());
            }

            return nullptr;
        }
    }

    SamplerHandle Device::createSampler(const SamplerDesc& d)
    {

        //convert the sampler desc
        D3D11_SAMPLER_DESC desc11;

        UINT reductionType;
        switch (d.reductionType)
        {
        case SamplerDesc::REDUCTION_COMPARISON:
            reductionType = D3D11_FILTER_REDUCTION_TYPE_COMPARISON;
            break;
        case SamplerDesc::REDUCTION_MINIMUM:
            reductionType = D3D11_FILTER_REDUCTION_TYPE_MINIMUM;
            break;
        case SamplerDesc::REDUCTION_MAXIMUM:
            reductionType = D3D11_FILTER_REDUCTION_TYPE_MAXIMUM;
            break;
        default:
            reductionType = D3D11_FILTER_REDUCTION_TYPE_STANDARD;
            break;
        }

        if (d.anisotropy > 1.0f)
        {
            desc11.Filter = D3D11_ENCODE_ANISOTROPIC_FILTER(reductionType);
        }
        else
        {
            desc11.Filter = D3D11_ENCODE_BASIC_FILTER(d.minFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT, d.magFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT, d.mipFilter ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT, reductionType);
        }
        D3D11_TEXTURE_ADDRESS_MODE* dest[] = { &desc11.AddressU, &desc11.AddressV, &desc11.AddressW };
        for (int i = 0; i < 3; i++)
        {
            switch (d.wrapMode[i])
            {
            case SamplerDesc::WRAP_MODE_BORDER:
                *dest[i] = D3D11_TEXTURE_ADDRESS_BORDER;
                break;
            case SamplerDesc::WRAP_MODE_CLAMP:
                *dest[i] = D3D11_TEXTURE_ADDRESS_CLAMP;
                break;
            case SamplerDesc::WRAP_MODE_WRAP:
                *dest[i] = D3D11_TEXTURE_ADDRESS_WRAP;
                break;
            }
        }

        desc11.MipLODBias = d.mipBias;
        desc11.MaxAnisotropy = std::max((UINT)d.anisotropy, 1U);
        desc11.ComparisonFunc = D3D11_COMPARISON_LESS;
        desc11.BorderColor[0] = d.borderColor.r;
        desc11.BorderColor[1] = d.borderColor.g;
        desc11.BorderColor[2] = d.borderColor.b;
        desc11.BorderColor[3] = d.borderColor.a;
        desc11.MinLOD = 0;
        desc11.MaxLOD = D3D11_FLOAT32_MAX;

        RefCountPtr<ID3D11SamplerState> sState;
        CHECK_ERROR(SUCCEEDED(device->CreateSamplerState(&desc11, &sState)), "Creating sampler state failed");

        Sampler* sampler = new Sampler(this);
        sampler->sampler = sState;
        sampler->desc = d;
        return SamplerHandle::Create(sampler);
    }

    InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* _vertexShader)
    {
        Shader* vertexShader = static_cast<Shader*>(_vertexShader);

        if (vertexShader == nullptr)
        {
            message(MessageSeverity::Error, "No vertex shader provided to createInputLayout");
            return nullptr;
        }

        if (vertexShader->desc.shaderType != ShaderType::SHADER_VERTEX)
        {
            message(MessageSeverity::Error, "A non-vertex shader provided to createInputLayout");
            return nullptr;
        }

        InputLayout *inputLayout = new InputLayout(this);

        inputLayout->attributes.resize(attributeCount);

        static_vector<D3D11_INPUT_ELEMENT_DESC, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> elementDesc;
        for (uint32_t i = 0; i < attributeCount; i++)
        {
            inputLayout->attributes[i] = d[i];

            assert(d[i].arraySize > 0);

            const FormatMapping& formatMapping = GetFormatMapping(d[i].format);

            for (uint32_t semanticIndex = 0; semanticIndex < d[i].arraySize; semanticIndex++)
            {
                D3D11_INPUT_ELEMENT_DESC desc;

                desc.SemanticName = d[i].name;
                desc.SemanticIndex = semanticIndex;
                desc.Format = formatMapping.srvFormat;
                desc.InputSlot = d[i].bufferIndex;
                desc.AlignedByteOffset = d[i].offset + semanticIndex * (formatMapping.bitsPerPixel / 8);
                desc.InputSlotClass = d[i].isInstanced ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
                desc.InstanceDataStepRate = d[i].isInstanced ? 1 : 0;

                elementDesc.push_back(desc);
            }
        }

        CHECK_ERROR(SUCCEEDED(device->CreateInputLayout(elementDesc.data(), uint32_t(elementDesc.size()), vertexShader->bytecode.data(), vertexShader->bytecode.size(), &inputLayout->layout)), "CreateInputLayout() failed");

        for(uint32_t i = 0; i < attributeCount; i++)
        {
            const auto index = d[i].bufferIndex;

            if (inputLayout->elementStrides.find(index) == inputLayout->elementStrides.end())
            {
                inputLayout->elementStrides[index] = d[i].elementStride;
            } else {
                assert(inputLayout->elementStrides[index] == d[i].elementStride);
            }
        }

        return InputLayoutHandle::Create(inputLayout);
    }

    GraphicsAPI Device::getGraphicsAPI()
    {
        return GraphicsAPI::D3D11;
    }

    Object Device::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D11_Device:
            return Object(device);
        case ObjectTypes::D3D11_DeviceContext:
            return Object(context);
        case ObjectTypes::Nvrhi_D3D11_Device:
            return this;
        default:
            return nullptr;
        }
    }

    FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc)
    {
        Framebuffer *ret = new Framebuffer(this);
        ret->desc = desc;
        ret->framebufferInfo = FramebufferInfo(desc);

        for(auto colorAttachment : desc.colorAttachments)
        {
            assert(colorAttachment.valid());
            ret->RTVs.push_back(getRTVForAttachment(colorAttachment));
        }

        if (desc.depthAttachment.valid())
        {
            ret->pDSV = getDSVForAttachment(desc.depthAttachment);
        }

        return FramebufferHandle::Create(ret);
    }
    
    nvrhi::CommandListHandle Device::createCommandList(const CommandListParameters& params)
    {
        if (!params.enableImmediateExecution)
        {
            CHECK_ERROR(false, "Deferred command lists are not supported by this implementation.");
            return nullptr;
        }

        // create a new RefCountPtr and add a reference to this
        return CommandListHandle(this);
    }

    static DX11_ViewportState convertViewportState(const ViewportState& vpState)
    {
        DX11_ViewportState ret;

        ret.numViewports = UINT(vpState.viewports.size());
        for (size_t rt = 0; rt < vpState.viewports.size(); rt++)
        {
            ret.viewports[rt].TopLeftX = vpState.viewports[rt].minX;
            ret.viewports[rt].TopLeftY = vpState.viewports[rt].minY;
            ret.viewports[rt].Width = vpState.viewports[rt].maxX - vpState.viewports[rt].minX;
            ret.viewports[rt].Height = vpState.viewports[rt].maxY - vpState.viewports[rt].minY;
            ret.viewports[rt].MinDepth = vpState.viewports[rt].minZ;
            ret.viewports[rt].MaxDepth = vpState.viewports[rt].maxZ;
        }

        ret.numScissorRects = UINT(vpState.scissorRects.size());
        for(size_t rt = 0; rt < vpState.scissorRects.size(); rt++)
        {
            ret.scissorRects[rt].left = (LONG)vpState.scissorRects[rt].minX;
            ret.scissorRects[rt].top = (LONG)vpState.scissorRects[rt].minY;
            ret.scissorRects[rt].right = (LONG)vpState.scissorRects[rt].maxX;
            ret.scissorRects[rt].bottom = (LONG)vpState.scissorRects[rt].maxY;
        }

        return ret;
    }

    GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb)
    {
        const RenderState& renderState = desc.renderState;

        if (desc.renderState.singlePassStereo.enabled && !m_SinglePassStereoSupported)
        {
            CHECK_ERROR(0, "Single-pass stereo is not supported by this device");
            return nullptr;
        }

        GraphicsPipeline *pso = new GraphicsPipeline(this);
        pso->desc = desc;
        pso->framebufferInfo = fb->GetFramebufferInfo();

        pso->primitiveTopology = getPrimType(desc.primType);
        pso->inputLayout = static_cast<InputLayout*>(desc.inputLayout.Get());

        pso->viewportState = convertViewportState(renderState.viewportState);

        pso->pRS = getRasterizerState(renderState.rasterState);
        pso->pBlendState = getBlendState(renderState.blendState);
        pso->pDepthStencilState = getDepthStencilState(renderState.depthStencilState);

        pso->blendFactor[0] = FLOAT(renderState.blendState.blendFactor.r);
        pso->blendFactor[1] = FLOAT(renderState.blendState.blendFactor.g);
        pso->blendFactor[2] = FLOAT(renderState.blendState.blendFactor.b);
        pso->blendFactor[3] = FLOAT(renderState.blendState.blendFactor.a);

        pso->stencilRef = renderState.depthStencilState.stencilRefValue;

        if (desc.VS) pso->pVS = static_cast<Shader*>(desc.VS.Get())->VS;
        if (desc.HS) pso->pHS = static_cast<Shader*>(desc.HS.Get())->HS;
        if (desc.DS) pso->pDS = static_cast<Shader*>(desc.DS.Get())->DS;
        if (desc.GS) pso->pGS = static_cast<Shader*>(desc.GS.Get())->GS;
        if (desc.PS) pso->pPS = static_cast<Shader*>(desc.PS.Get())->PS;

        // Set a flag if the PS has any UAV bindings in the layout
        for (auto& _layout : desc.bindingLayouts)
        {
            PipelineBindingLayout* layout = static_cast<PipelineBindingLayout*>(_layout.Get());
            for (const auto& item : layout->desc.PS)
            {
                if (item.type == ResourceType::Buffer_UAV || item.type == ResourceType::Texture_UAV || item.type == ResourceType::StructuredBuffer_UAV)
                {
                    pso->pixelShaderHasUAVs = true;
                    break;
                }
            }

            if (pso->pixelShaderHasUAVs)
                break;
        }
        
        return GraphicsPipelineHandle::Create(pso);
    }

    bool Device::queryFeatureSupport(Feature feature)
    {
        switch (feature)
        {
        case nvrhi::Feature::DeferredCommandLists:
            return false;
        case nvrhi::Feature::SinglePassStereo:
            return m_SinglePassStereoSupported;
        default:
            return false;
        }
    }

    void Device::bindGraphicsPipeline(const GraphicsPipeline* pso)
    {
        context->IASetPrimitiveTopology(pso->primitiveTopology);
        context->IASetInputLayout(pso->inputLayout ? pso->inputLayout->layout : nullptr);

        if (pso->viewportState.numViewports)
        {
            context->RSSetViewports(pso->viewportState.numViewports, pso->viewportState.viewports);
        }

        if (pso->viewportState.numScissorRects)
        {
            context->RSSetScissorRects(pso->viewportState.numViewports, pso->viewportState.scissorRects);
        }

        context->RSSetState(pso->pRS);

        context->VSSetShader(pso->pVS, NULL, 0);
        context->HSSetShader(pso->pHS, NULL, 0);
        context->DSSetShader(pso->pDS, NULL, 0);
        context->GSSetShader(pso->pGS, NULL, 0);
        context->PSSetShader(pso->pPS, NULL, 0);

        context->OMSetBlendState(pso->pBlendState, pso->blendFactor, D3D11_DEFAULT_SAMPLE_MASK);
        context->OMSetDepthStencilState(pso->pDepthStencilState, pso->stencilRef);
    }

    template<typename T, typename U> bool arraysAreDifferent(const T& a, const U& b)
    {
        if (a.size() != b.size())
            return true;

        for (uint32_t i = 0; i < uint32_t(a.size()); i++)
        {
            if (a[i] != b[i])
                return true;
        }

        return false;
    }

    void Device::setGraphicsState(const GraphicsState& state)
    {
        GraphicsPipeline* pipeline = static_cast<GraphicsPipeline*>(state.pipeline);
        Framebuffer* framebuffer = static_cast<Framebuffer*>(state.framebuffer);

        if (m_CurrentComputeStateValid)
        {
            // If the previous operation has been a Dispatch call, there is a possibility of RT/UAV/SRV hazards.
            // Unbind everything to be sure, and to avoid checking the binding sets against each other. 
            // This only happens on switches between compute and graphics modes.

            clearState();
        }

        bool updateFramebuffer = !m_CurrentGraphicsStateValid || m_CurrentFramebuffer != state.framebuffer;
        bool updatePipeline = !m_CurrentGraphicsStateValid || m_CurrentGraphicsPipeline != state.pipeline;
        bool updateBindings = updateFramebuffer || arraysAreDifferent(m_CurrentBindings, state.bindings);

        bool updateDynamicViewports = false;
        bool previousViewportsWereDynamic = m_CurrentGraphicsStateValid && m_CurrentDynamicViewports.viewports.size();
        if (state.viewport.viewports.size())
        {
            if (previousViewportsWereDynamic)
            {
                updateDynamicViewports = 
                    arraysAreDifferent(m_CurrentDynamicViewports.viewports, state.viewport.viewports) ||
                    arraysAreDifferent(m_CurrentDynamicViewports.scissorRects, state.viewport.scissorRects);
            }
            else
            {
                updateDynamicViewports = true;
            }
        }
        else
        {
            if (previousViewportsWereDynamic)
            {
                updatePipeline = true; // which sets the static viewports
            }
        }

        bool updateIndexBuffer = !m_CurrentGraphicsStateValid || m_CurrentIndexBufferBinding != state.indexBuffer;
        bool updateVertexBuffers = !m_CurrentGraphicsStateValid || arraysAreDifferent(m_CurrentVertexBufferBindings, state.vertexBuffers);

        BindingSetVector setsToBind;
        if (updateBindings)
        {
            prepareToBindGraphicsResourceSets(state.bindings, m_CurrentGraphicsStateValid ? &m_CurrentBindings : nullptr, updateFramebuffer, setsToBind);
        }

        if (updateFramebuffer || static_cast<GraphicsPipeline*>(m_CurrentGraphicsPipeline.Get())->pixelShaderHasUAVs != pipeline->pixelShaderHasUAVs)
        {
            if (pipeline->pixelShaderHasUAVs)
            {
                context->OMSetRenderTargetsAndUnorderedAccessViews(UINT(framebuffer->RTVs.size()), framebuffer->RTVs.data(), framebuffer->pDSV, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);
            }
            else
            {
                context->OMSetRenderTargets(UINT(framebuffer->RTVs.size()), framebuffer->RTVs.data(), framebuffer->pDSV);
            }
        }

        if (updatePipeline)
        {
            bindGraphicsPipeline(pipeline);
        }

        if (updateBindings)
        {
            bindGraphicsResourceSets(setsToBind);

            if (pipeline->pixelShaderHasUAVs)
            {
                ID3D11UnorderedAccessView* UAVs[D3D11_1_UAV_SLOT_COUNT] = {};
                static const UINT initialCounts[D3D11_1_UAV_SLOT_COUNT] = {};
                uint32_t minUAVSlot = D3D11_1_UAV_SLOT_COUNT;
                uint32_t maxUAVSlot = 0;
                for (auto _bindingSet : state.bindings)
                {
                    PipelineBindingSet* bindingSet = static_cast<PipelineBindingSet*>(_bindingSet);
                    for (uint32_t slot = bindingSet->PS.minUAVSlot; slot <= bindingSet->PS.maxUAVSlot; slot++)
                    {
                        UAVs[slot] = bindingSet->PS.UAVs[slot];
                    }
                    minUAVSlot = std::min(minUAVSlot, bindingSet->PS.minUAVSlot);
                    maxUAVSlot = std::max(maxUAVSlot, bindingSet->PS.maxUAVSlot);
                }

                context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, minUAVSlot, maxUAVSlot - minUAVSlot + 1, UAVs + minUAVSlot, initialCounts);
            }
        }

        if (updateDynamicViewports)
        {
            DX11_ViewportState vpState = convertViewportState(state.viewport);

            if (vpState.numViewports)
            {
                assert(pipeline->viewportState.numViewports == 0);
                context->RSSetViewports(vpState.numViewports, vpState.viewports);
            }

            if (vpState.numScissorRects)
            {
                assert(pipeline->viewportState.numScissorRects == 0);
                context->RSSetScissorRects(vpState.numScissorRects, vpState.scissorRects);
            }
        }

#if NVRHI_D3D11_WITH_NVAPI
        bool updateSPS = m_CurrentSinglePassStereoState != pipeline->desc.renderState.singlePassStereo;

        if (updateSPS)
        {
            const SinglePassStereoState& spsState = pipeline->desc.renderState.singlePassStereo;

            NvAPI_Status Status = NvAPI_D3D_SetSinglePassStereoMode(context, spsState.enabled ? 2 : 1, spsState.renderTargetIndexOffset, spsState.independentViewportMask);

            CHECK_ERROR(Status == NVAPI_OK, "NvAPI_D3D_SetSinglePassStereoMode call failed");

            m_CurrentSinglePassStereoState = spsState;
        }
#endif

        if (updateVertexBuffers)
        {
            ID3D11Buffer *pVertexBuffers[GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT] = {};
            UINT pVertexBufferStrides[GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT] = {};
            UINT pVertexBufferOffsets[GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT] = {};

            const auto *inputLayout = pipeline->inputLayout;
            for (size_t i = 0; i < state.vertexBuffers.size(); i++)
            {
                const VertexBufferBinding& binding = state.vertexBuffers[i];

                pVertexBuffers[i] = static_cast<Buffer*>(binding.buffer)->resource;
                pVertexBufferStrides[i] = inputLayout->elementStrides.at(binding.slot);
                pVertexBufferOffsets[i] = binding.offset;
            }

            uint32_t numVertexBuffers = m_CurrentGraphicsStateValid
                ? uint32_t(std::max(m_CurrentVertexBufferBindings.size(), state.vertexBuffers.size()))
                : GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT;

            context->IASetVertexBuffers(0, numVertexBuffers,
                pVertexBuffers,
                pVertexBufferStrides,
                pVertexBufferOffsets);
        }

        if (updateIndexBuffer)
        {
            if (state.indexBuffer.handle)
            {
                context->IASetIndexBuffer(static_cast<Buffer*>(state.indexBuffer.handle)->resource,
                    GetFormatMapping(state.indexBuffer.format).srvFormat,
                    state.indexBuffer.offset);
            }
            else
            {
                context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            }
        }

        m_CurrentIndirectBuffer = state.indirectParams;

        m_CurrentGraphicsStateValid = true;
        if (updatePipeline || updateFramebuffer || updateBindings || updateDynamicViewports || updateVertexBuffers || updateIndexBuffer)
        {
            m_CurrentGraphicsPipeline = state.pipeline;
            m_CurrentFramebuffer = state.framebuffer;
            m_CurrentDynamicViewports = state.viewport;

            m_CurrentBindings.resize(state.bindings.size());
            for(size_t i = 0; i < state.bindings.size(); i++)
            {
                m_CurrentBindings[i] = state.bindings[i];
            }

            m_CurrentVertexBufferBindings = state.vertexBuffers;
            m_CurrentIndexBufferBinding = state.indexBuffer;

            m_CurrentVertexBuffers.resize(state.vertexBuffers.size());
            for (size_t i = 0; i < state.vertexBuffers.size(); i++)
            {
                m_CurrentVertexBuffers[i] = state.vertexBuffers[i].buffer;
            }
            m_CurrentIndexBuffer = state.indexBuffer.handle;
        }
    }

    void Device::draw(const DrawArguments& args)
    {
        context->DrawInstanced(args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
    }

    void Device::drawIndexed(const DrawArguments& args)
    {
        context->DrawIndexedInstanced(args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
    }

    void Device::drawIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = static_cast<Buffer*>(m_CurrentIndirectBuffer.Get());
        CHECK_ERROR(indirectParams, "DrawIndirect parameters buffer is not set");

        context->DrawInstancedIndirect(indirectParams->resource, (UINT)offsetBytes);
    }


    ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
    {
        ComputePipeline *pso = new ComputePipeline(this);
        pso->desc = desc;

        if (desc.CS) pso->shader = static_cast<Shader*>(desc.CS.Get())->CS;

        return ComputePipelineHandle::Create(pso);
    }
    
    void Device::setComputeState(const ComputeState& state)
    {
        ComputePipeline* pso = static_cast<ComputePipeline*>(state.pipeline);

        if (m_CurrentGraphicsStateValid)
        {
            // If the previous operation has been a Draw call, there is a possibility of RT/UAV/SRV hazards.
            // Unbind everything to be sure, and to avoid checking the binding sets against each other. 
            // This only happens on switches between compute and graphics modes.

            clearState();
        }

        bool updatePipeline = !m_CurrentComputeStateValid || pso != m_CurrentComputePipeline;
        bool updateBindings = updatePipeline || arraysAreDifferent(m_CurrentBindings, state.bindings);

        if (updatePipeline) context->CSSetShader(pso->shader, nullptr, 0);
        if (updateBindings) bindComputeResourceSets(state.bindings, m_CurrentComputeStateValid ? &m_CurrentBindings : nullptr);

        m_CurrentIndirectBuffer = state.indirectParams;

        if (updatePipeline || updateBindings)
        {
            m_CurrentComputePipeline = pso;

            m_CurrentBindings.resize(state.bindings.size());
            for (size_t i = 0; i < state.bindings.size(); i++)
            {
                m_CurrentBindings[i] = state.bindings[i];
            }

            m_CurrentComputeStateValid = true;
        }
    }

    void Device::dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
    {
        context->Dispatch(groupsX, groupsY, groupsZ);
    }

    void Device::dispatchIndirect(uint32_t offsetBytes)
    {
        Buffer* indirectParams = static_cast<Buffer*>(m_CurrentIndirectBuffer.Get());
        CHECK_ERROR(indirectParams, "DispatchIndirect parameters buffer is not set");

        context->DispatchIndirect(indirectParams->resource, (UINT)offsetBytes);
    }

    void Device::getClearViewForTexture(ITexture* _texture, TextureSubresourceSet subresources, ID3D11UnorderedAccessView*& outUAV, ID3D11RenderTargetView*& outRTV, ID3D11DepthStencilView*& outDSV)
    {
        outUAV = NULL;
        outRTV = NULL;
        outDSV = NULL;

        Texture* texture = static_cast<Texture*>(_texture);

        const TextureDesc& textureDesc = texture->desc;

        // Try UAVs first since they are more flexible
        if (textureDesc.isUAV)
        {
            outUAV = getUAVForTexture(texture, Format::UNKNOWN, subresources);
        }
        else if (textureDesc.isRenderTarget)
        {
            if (textureDesc.format == Format::D16 || textureDesc.format == Format::D24S8 || textureDesc.format == Format::D32)
                outDSV = getDSVForTexture(texture, subresources); //return or create a RT
            else
                outRTV = getRTVForTexture(texture, Format::UNKNOWN, subresources); //return or create a RT
        }
        else
        {
            CHECK_ERROR(0, "This resource cannot be cleared");
        }
    }

    ID3D11ShaderResourceView* Device::getSRVForTexture(ITexture* _texture, Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

		if (format == Format::UNKNOWN)
		{
            format = textureDesc.format;
		}

        subresources = subresources.resolve(textureDesc, false);

        RefCountPtr<ID3D11ShaderResourceView>& srvPtr = texture->shaderResourceViews[TextureBindingKey(subresources, format)];
        if (srvPtr == NULL)
        {
            //we haven't seen this one before
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
            viewDesc.Format = GetFormatMapping(format).srvFormat;

            switch (textureDesc.dimension)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture1D.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture1DArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture2D.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2DArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture2DArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::TextureCube:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
                viewDesc.TextureCube.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.TextureCube.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
                viewDesc.TextureCubeArray.First2DArrayFace = subresources.baseArraySlice;
                viewDesc.TextureCubeArray.NumCubes = subresources.numArraySlices / 6;
                viewDesc.TextureCubeArray.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.TextureCubeArray.MipLevels = subresources.numMipLevels;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.MostDetailedMip = subresources.baseMipLevel;
                viewDesc.Texture3D.MipLevels = subresources.numMipLevels;
                break;
            default:
                return nullptr;
            }

            CHECK_ERROR(SUCCEEDED(device->CreateShaderResourceView(texture->resource, &viewDesc, &srvPtr)), "Creating the view failed");
        }
        return srvPtr;
    }

    ID3D11RenderTargetView* Device::getRTVForAttachment(const FramebufferAttachment& attachment)
    {
        return getRTVForTexture(attachment.texture, attachment.format, attachment.subresources);
    }

    ID3D11RenderTargetView* Device::getRTVForTexture(ITexture* _texture, Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        if (format == Format::UNKNOWN)
        {
            format = textureDesc.format;
        }

        subresources = subresources.resolve(textureDesc, true);

        RefCountPtr<ID3D11RenderTargetView>& rtvPtr = texture->renderTargetViews[TextureBindingKey(subresources, format)];
        if (rtvPtr == NULL)
        {
            //we haven't seen this one before
            D3D11_RENDER_TARGET_VIEW_DESC viewDesc;
            viewDesc.Format = GetFormatMapping(format).rtvFormat;

            switch (textureDesc.dimension)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.FirstWSlice = subresources.baseArraySlice;
                viewDesc.Texture3D.WSize = subresources.numArraySlices;
                viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
                break;
            default:
                return nullptr;
            }

            CHECK_ERROR(SUCCEEDED(device->CreateRenderTargetView(texture->resource, &viewDesc, &rtvPtr)), "Creating the view failed");
        }
        return rtvPtr;
    }

    ID3D11DepthStencilView* Device::getDSVForAttachment(const FramebufferAttachment& attachment)
    {
        return getDSVForTexture(attachment.texture, attachment.subresources, attachment.isReadOnly);
    }

    ID3D11DepthStencilView* Device::getDSVForTexture(ITexture* _texture, TextureSubresourceSet subresources, bool isReadOnly)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        subresources = subresources.resolve(textureDesc, true);


        RefCountPtr<ID3D11DepthStencilView>& dsvPtr = texture->depthStencilViews[TextureBindingKey(subresources, textureDesc.format, isReadOnly)];
        if (dsvPtr == NULL)
        {
            //we haven't seen this one before
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc;
            viewDesc.Format = GetFormatMapping(textureDesc.format).rtvFormat;
            viewDesc.Flags = 0;

            if (isReadOnly)
            {
                viewDesc.Flags |= D3D11_DSV_READ_ONLY_DEPTH;
                if (viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                    viewDesc.Flags |= D3D11_DSV_READ_ONLY_STENCIL;
            }

            switch (textureDesc.dimension)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DMS:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
                break;
            case TextureDimension::Texture2DMSArray:
                viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
                viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
                break;
            default:
                return nullptr;
            }

            CHECK_ERROR(SUCCEEDED(device->CreateDepthStencilView(texture->resource, &viewDesc, &dsvPtr)), "Creating the view failed");
        }
        return dsvPtr;
    }

    ID3D11UnorderedAccessView* Device::getUAVForTexture(ITexture* _texture, Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        if (format == Format::UNKNOWN)
        {
            format = textureDesc.format;
        }

        subresources = subresources.resolve(textureDesc, true);

        RefCountPtr<ID3D11UnorderedAccessView>& uavPtr = texture->unorderedAccessViews[TextureBindingKey(subresources, format)];
        if (uavPtr == NULL)
        {
            CHECK_ERROR(textureDesc.sampleCount <= 1, "You cannot access a multisample UAV");

            D3D11_UNORDERED_ACCESS_VIEW_DESC viewDesc;
            viewDesc.Format = GetFormatMapping(format).srvFormat;

            switch (textureDesc.dimension)
            {
            case TextureDimension::Texture1D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
                viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture1DArray:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
                viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
                viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
                viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
                break;
            case TextureDimension::Texture3D:
                viewDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                viewDesc.Texture3D.FirstWSlice = 0;
                viewDesc.Texture3D.WSize = textureDesc.depth;
                viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
                break;
            default:
                return nullptr;
            }

            CHECK_ERROR(SUCCEEDED(device->CreateUnorderedAccessView(texture->resource, &viewDesc, &uavPtr)), "Creating the view failed");
        }
        return uavPtr;
    }

    ID3D11ShaderResourceView* Device::getSRVForBuffer(IBuffer* _buffer, Format format, BufferRange range)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (!buffer)
            return nullptr;

        range = range.resolve(buffer->desc);

        RefCountPtr<ID3D11ShaderResourceView>& srv = buffer->shaderResourceViews[BufferBindingKey(range, format)];
        if (srv)
            return srv;


        D3D11_SHADER_RESOURCE_VIEW_DESC desc11;
        desc11.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        desc11.BufferEx.Flags = 0;

        if(buffer->desc.structStride != 0)
        {
            desc11.Format = DXGI_FORMAT_UNKNOWN;
            desc11.BufferEx.FirstElement = range.byteOffset / buffer->desc.structStride;
            desc11.BufferEx.NumElements = range.byteSize / buffer->desc.structStride;
        }
        else
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc11.Format = mapping.srvFormat;
            uint32_t bytesPerElement = mapping.bitsPerPixel / 8;
            desc11.BufferEx.FirstElement = range.byteOffset / bytesPerElement;
            desc11.BufferEx.NumElements = range.byteSize / bytesPerElement;
        }

        CHECK_ERROR(SUCCEEDED(device->CreateShaderResourceView(buffer->resource, &desc11, &srv)), "Creation failed");
        return srv;
    }

    ID3D11UnorderedAccessView* Device::getUAVForBuffer(IBuffer* _buffer, Format format, BufferRange range)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (!buffer)
            return nullptr;

        range = range.resolve(buffer->desc);

        RefCountPtr<ID3D11UnorderedAccessView>& uav = buffer->unorderedAccessViews[BufferBindingKey(range, format)];
        if (uav)
            return uav;
        
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc11;
        desc11.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc11.Buffer.Flags = 0;

        if (buffer->desc.structStride != 0)
        {
            desc11.Format = DXGI_FORMAT_UNKNOWN;
            desc11.Buffer.FirstElement = range.byteOffset / buffer->desc.structStride;
            desc11.Buffer.NumElements = range.byteSize / buffer->desc.structStride;
        }
        else
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc11.Format = mapping.srvFormat;
            uint32_t bytesPerElement = mapping.bitsPerPixel / 8;
            desc11.Buffer.FirstElement = range.byteOffset / bytesPerElement;
            desc11.Buffer.NumElements = range.byteSize / bytesPerElement;
        }

        CHECK_ERROR(SUCCEEDED(device->CreateUnorderedAccessView(buffer->resource, &desc11, &uav)), "Creation failed");
        return uav;
    }
    
    void Device::clearCachedData()
    {
        rasterizerStates.clear();
        blendStates.clear();
        depthStencilStates.clear();
    }

    namespace
    {
        //Unfortunately we can't memcmp the structs since they have padding bytes in them
        inline bool operator!=(const D3D11_RENDER_TARGET_BLEND_DESC& lhsrt, const D3D11_RENDER_TARGET_BLEND_DESC& rhsrt)
        {
            if (lhsrt.BlendEnable != rhsrt.BlendEnable ||
                lhsrt.SrcBlend != rhsrt.SrcBlend ||
                lhsrt.DestBlend != rhsrt.DestBlend ||
                lhsrt.BlendOp != rhsrt.BlendOp ||
                lhsrt.SrcBlendAlpha != rhsrt.SrcBlendAlpha ||
                lhsrt.DestBlendAlpha != rhsrt.DestBlendAlpha ||
                lhsrt.BlendOpAlpha != rhsrt.BlendOpAlpha ||
                lhsrt.RenderTargetWriteMask != rhsrt.RenderTargetWriteMask)
                return true;
            return false;
        }

        inline bool operator!=(const D3D11_BLEND_DESC& lhs, const D3D11_BLEND_DESC& rhs)
        {
            if (lhs.AlphaToCoverageEnable != rhs.AlphaToCoverageEnable ||
                lhs.IndependentBlendEnable != rhs.IndependentBlendEnable)
                return true;
            for (size_t i = 0; i < sizeof(lhs.RenderTarget) / sizeof(lhs.RenderTarget[0]); i++)
            {
                if (lhs.RenderTarget[i] != rhs.RenderTarget[i])
                    return true;
            }
            return false;
        }

        inline bool operator!=(const D3D11_RASTERIZER_DESC& lhs, const D3D11_RASTERIZER_DESC& rhs)
        {
            if (lhs.FillMode != rhs.FillMode ||
                lhs.CullMode != rhs.CullMode ||
                lhs.FrontCounterClockwise != rhs.FrontCounterClockwise ||
                lhs.DepthBias != rhs.DepthBias ||
                lhs.DepthBiasClamp != rhs.DepthBiasClamp ||
                lhs.SlopeScaledDepthBias != rhs.SlopeScaledDepthBias ||
                lhs.DepthClipEnable != rhs.DepthClipEnable ||
                lhs.ScissorEnable != rhs.ScissorEnable ||
                lhs.MultisampleEnable != rhs.MultisampleEnable ||
                lhs.AntialiasedLineEnable != rhs.AntialiasedLineEnable)
                return true;

            return false;
        }

        inline bool operator!=(const D3D11_DEPTH_STENCILOP_DESC& lhs, const D3D11_DEPTH_STENCILOP_DESC& rhs)
        {
            if (lhs.StencilFailOp != rhs.StencilFailOp ||
                lhs.StencilDepthFailOp != rhs.StencilDepthFailOp ||
                lhs.StencilPassOp != rhs.StencilPassOp ||
                lhs.StencilFunc != rhs.StencilFunc)
                return true;
            return false;
        }
        inline bool operator!=(const D3D11_DEPTH_STENCIL_DESC& lhs, const D3D11_DEPTH_STENCIL_DESC& rhs)
        {
            if (lhs.DepthEnable != rhs.DepthEnable ||
                lhs.DepthWriteMask != rhs.DepthWriteMask ||
                lhs.DepthFunc != rhs.DepthFunc ||
                lhs.StencilEnable != rhs.StencilEnable ||
                lhs.StencilReadMask != rhs.StencilReadMask ||
                lhs.StencilWriteMask != rhs.StencilWriteMask ||
                lhs.FrontFace != rhs.FrontFace ||
                lhs.FrontFace != rhs.BackFace)
                return true;

            return false;
        }
    }

    ID3D11BlendState* Device::getBlendState(const BlendState& blendState)
    {
        CrcHash hasher;
        hasher.Add(blendState);
        uint32_t hash = hasher.Get();
        RefCountPtr<ID3D11BlendState> d3dBlendState = blendStates[hash];

        if (d3dBlendState)
            return d3dBlendState;

        D3D11_BLEND_DESC desc11New;
        desc11New.AlphaToCoverageEnable = blendState.alphaToCoverage ? TRUE : FALSE;
        //we always use this and set the states for each target explicitly
        desc11New.IndependentBlendEnable = TRUE;

        for (uint32_t i = 0; i < FramebufferDesc::MAX_RENDER_TARGETS; i++)
        {
            desc11New.RenderTarget[i].BlendEnable = blendState.blendEnable[i] ? TRUE : FALSE;
            desc11New.RenderTarget[i].SrcBlend = convertBlendValue(blendState.srcBlend[i]);
            desc11New.RenderTarget[i].DestBlend = convertBlendValue(blendState.destBlend[i]);
            desc11New.RenderTarget[i].BlendOp = convertBlendOp(blendState.blendOp[i]);
            desc11New.RenderTarget[i].SrcBlendAlpha = convertBlendValue(blendState.srcBlendAlpha[i]);
            desc11New.RenderTarget[i].DestBlendAlpha = convertBlendValue(blendState.destBlendAlpha[i]);
            desc11New.RenderTarget[i].BlendOpAlpha = convertBlendOp(blendState.blendOpAlpha[i]);
            desc11New.RenderTarget[i].RenderTargetWriteMask =
                (blendState.colorWriteEnable[i] & BlendState::COLOR_MASK_RED   ? D3D11_COLOR_WRITE_ENABLE_RED   : 0) |
                (blendState.colorWriteEnable[i] & BlendState::COLOR_MASK_GREEN ? D3D11_COLOR_WRITE_ENABLE_GREEN : 0) |
                (blendState.colorWriteEnable[i] & BlendState::COLOR_MASK_BLUE  ? D3D11_COLOR_WRITE_ENABLE_BLUE  : 0) |
                (blendState.colorWriteEnable[i] & BlendState::COLOR_MASK_ALPHA ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0);
        }

        CHECK_ERROR(SUCCEEDED(device->CreateBlendState(&desc11New, &d3dBlendState)), "Creating blend state failed");

        blendStates[hash] = d3dBlendState;
        return d3dBlendState;
    }

    ID3D11DepthStencilState* Device::getDepthStencilState(const DepthStencilState& depthState)
    {
        CrcHash hasher;
        hasher.Add(depthState);
        uint32_t hash = hasher.Get();
        RefCountPtr<ID3D11DepthStencilState> d3dDepthStencilState = depthStencilStates[hash];

        if (d3dDepthStencilState)
            return d3dDepthStencilState;

        D3D11_DEPTH_STENCIL_DESC desc11New;
        desc11New.DepthEnable = depthState.depthEnable ? TRUE : FALSE;
        desc11New.DepthWriteMask = depthState.depthWriteMask == DepthStencilState::DEPTH_WRITE_MASK_ALL ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc11New.DepthFunc = convertComparisonFunc(depthState.depthFunc);
        desc11New.StencilEnable = depthState.stencilEnable ? TRUE : FALSE;
        desc11New.StencilReadMask = (UINT8)depthState.stencilReadMask;
        desc11New.StencilWriteMask = (UINT8)depthState.stencilWriteMask;
        desc11New.FrontFace.StencilFailOp = convertStencilOp(depthState.frontFace.stencilFailOp);
        desc11New.FrontFace.StencilDepthFailOp = convertStencilOp(depthState.frontFace.stencilDepthFailOp);
        desc11New.FrontFace.StencilPassOp = convertStencilOp(depthState.frontFace.stencilPassOp);
        desc11New.FrontFace.StencilFunc = convertComparisonFunc(depthState.frontFace.stencilFunc);
        desc11New.BackFace.StencilFailOp = convertStencilOp(depthState.backFace.stencilFailOp);
        desc11New.BackFace.StencilDepthFailOp = convertStencilOp(depthState.backFace.stencilDepthFailOp);
        desc11New.BackFace.StencilPassOp = convertStencilOp(depthState.backFace.stencilPassOp);
        desc11New.BackFace.StencilFunc = convertComparisonFunc(depthState.backFace.stencilFunc);

        CHECK_ERROR(SUCCEEDED(device->CreateDepthStencilState(&desc11New, &d3dDepthStencilState)), "Creating depth-stencil state failed");

        depthStencilStates[hash] = d3dDepthStencilState;
        return d3dDepthStencilState;
    }

    ID3D11RasterizerState* Device::getRasterizerState(const RasterState& rasterState)
    {
        CrcHash hasher;
        hasher.Add(rasterState);
        uint32_t hash = hasher.Get();
        RefCountPtr<ID3D11RasterizerState> d3dRasterizerState = rasterizerStates[hash];

        if (d3dRasterizerState)
            return d3dRasterizerState;

        D3D11_RASTERIZER_DESC desc11New;
        switch (rasterState.fillMode)
        {
        case RasterState::FILL_SOLID:
            desc11New.FillMode = D3D11_FILL_SOLID;
            break;
        case RasterState::FILL_LINE:
            desc11New.FillMode = D3D11_FILL_WIREFRAME;
            break;
        default:
            CHECK_ERROR(0, "Unknown fillMode");
        }

        switch (rasterState.cullMode)
        {
        case RasterState::CULL_BACK:
            desc11New.CullMode = D3D11_CULL_BACK;
            break;
        case RasterState::CULL_FRONT:
            desc11New.CullMode = D3D11_CULL_FRONT;
            break;
        case RasterState::CULL_NONE:
            desc11New.CullMode = D3D11_CULL_NONE;
            break;
        default:
            CHECK_ERROR(0, "Unknown cullMode");
        }

        desc11New.FrontCounterClockwise = rasterState.frontCounterClockwise ? TRUE : FALSE;
        desc11New.DepthBias = rasterState.depthBias;
        desc11New.DepthBiasClamp = rasterState.depthBiasClamp;
        desc11New.SlopeScaledDepthBias = rasterState.slopeScaledDepthBias;
        desc11New.DepthClipEnable = rasterState.depthClipEnable ? TRUE : FALSE;
        desc11New.ScissorEnable = rasterState.scissorEnable ? TRUE : FALSE;
        desc11New.MultisampleEnable = rasterState.multisampleEnable ? TRUE : FALSE;
        desc11New.AntialiasedLineEnable = rasterState.antialiasedLineEnable ? TRUE : FALSE;

        bool extendedState = rasterState.conservativeRasterEnable || rasterState.forcedSampleCount || rasterState.programmableSamplePositionsEnable || rasterState.quadFillEnable;

        if (extendedState)
        {
#if NVRHI_D3D11_WITH_NVAPI
            NvAPI_D3D11_RASTERIZER_DESC_EX descEx;
            memset(&descEx, 0, sizeof(descEx));
            memcpy(&descEx, &desc11New, sizeof(desc11New));

            descEx.ConservativeRasterEnable = rasterState.conservativeRasterEnable;
            descEx.ProgrammableSamplePositionsEnable = rasterState.programmableSamplePositionsEnable;
            descEx.SampleCount = rasterState.forcedSampleCount;
            descEx.ForcedSampleCount = rasterState.forcedSampleCount;
            descEx.QuadFillMode = rasterState.quadFillEnable ? NVAPI_QUAD_FILLMODE_BBOX : NVAPI_QUAD_FILLMODE_DISABLED;
            memcpy(descEx.SamplePositionsX, rasterState.samplePositionsX, sizeof(rasterState.samplePositionsX));
            memcpy(descEx.SamplePositionsY, rasterState.samplePositionsY, sizeof(rasterState.samplePositionsY));

            CHECK_ERROR(NVAPI_OK == NvAPI_D3D11_CreateRasterizerState(device, &descEx, &d3dRasterizerState), "Creating extended rasterizer state failed");
#else
            CHECK_ERROR(false, "Cannot create an extended rasterizer state without NVAPI support");
#endif
        }
        else
        {
            CHECK_ERROR(SUCCEEDED(device->CreateRasterizerState(&desc11New, &d3dRasterizerState)), "Creating rasterizer state failed");
        }

        rasterizerStates[hash] = d3dRasterizerState;
        return d3dRasterizerState;
    }

    D3D11_BLEND Device::convertBlendValue(BlendState::BlendValue value)
    {
        switch (value)
        {
        case BlendState::BLEND_ZERO:
            return D3D11_BLEND_ZERO;
        case BlendState::BLEND_ONE:
            return D3D11_BLEND_ONE;
        case BlendState::BLEND_SRC_COLOR:
            return D3D11_BLEND_SRC_COLOR;
        case BlendState::BLEND_INV_SRC_COLOR:
            return D3D11_BLEND_INV_SRC_COLOR;
        case BlendState::BLEND_SRC_ALPHA:
            return D3D11_BLEND_SRC_ALPHA;
        case BlendState::BLEND_INV_SRC_ALPHA:
            return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendState::BLEND_DEST_ALPHA:
            return D3D11_BLEND_DEST_ALPHA;
        case BlendState::BLEND_INV_DEST_ALPHA:
            return D3D11_BLEND_INV_DEST_ALPHA;
        case BlendState::BLEND_DEST_COLOR:
            return D3D11_BLEND_DEST_COLOR;
        case BlendState::BLEND_INV_DEST_COLOR:
            return D3D11_BLEND_INV_DEST_COLOR;
        case BlendState::BLEND_SRC_ALPHA_SAT:
            return D3D11_BLEND_SRC_ALPHA_SAT;
        case BlendState::BLEND_BLEND_FACTOR:
            return D3D11_BLEND_BLEND_FACTOR;
        case BlendState::BLEND_INV_BLEND_FACTOR:
            return D3D11_BLEND_INV_BLEND_FACTOR;
        case BlendState::BLEND_SRC1_COLOR:
            return D3D11_BLEND_SRC1_COLOR;
        case BlendState::BLEND_INV_SRC1_COLOR:
            return D3D11_BLEND_INV_SRC1_COLOR;
        case BlendState::BLEND_SRC1_ALPHA:
            return D3D11_BLEND_SRC1_ALPHA;
        case BlendState::BLEND_INV_SRC1_ALPHA:
            return D3D11_BLEND_INV_SRC1_ALPHA;
        default:
            CHECK_ERROR(0, "Unknown blend value");
            return D3D11_BLEND_ZERO;
        }
    }

    D3D11_BLEND_OP Device::convertBlendOp(BlendState::BlendOp value)
    {
        switch (value)
        {
        case BlendState::BLEND_OP_ADD:
            return D3D11_BLEND_OP_ADD;
        case BlendState::BLEND_OP_SUBTRACT:
            return D3D11_BLEND_OP_SUBTRACT;
        case BlendState::BLEND_OP_REV_SUBTRACT:
            return D3D11_BLEND_OP_REV_SUBTRACT;
        case BlendState::BLEND_OP_MIN:
            return D3D11_BLEND_OP_MIN;
        case BlendState::BLEND_OP_MAX:
            return D3D11_BLEND_OP_MAX;
        default:
            CHECK_ERROR(0, "Unknown blend op");
            return D3D11_BLEND_OP_ADD;
        }
    }

    D3D11_STENCIL_OP Device::convertStencilOp(DepthStencilState::StencilOp value)
    {
        switch (value)
        {
        case DepthStencilState::STENCIL_OP_KEEP:
            return D3D11_STENCIL_OP_KEEP;
        case DepthStencilState::STENCIL_OP_ZERO:
            return D3D11_STENCIL_OP_ZERO;
        case DepthStencilState::STENCIL_OP_REPLACE:
            return D3D11_STENCIL_OP_REPLACE;
        case DepthStencilState::STENCIL_OP_INCR_SAT:
            return D3D11_STENCIL_OP_INCR_SAT;
        case DepthStencilState::STENCIL_OP_DECR_SAT:
            return D3D11_STENCIL_OP_DECR_SAT;
        case DepthStencilState::STENCIL_OP_INVERT:
            return D3D11_STENCIL_OP_INVERT;
        case DepthStencilState::STENCIL_OP_INCR:
            return D3D11_STENCIL_OP_INCR;
        case DepthStencilState::STENCIL_OP_DECR:
            return D3D11_STENCIL_OP_DECR;
        default:
            CHECK_ERROR(0, "Unknown stencil op");
            return D3D11_STENCIL_OP_KEEP;
        }
    }

    D3D11_COMPARISON_FUNC Device::convertComparisonFunc(DepthStencilState::ComparisonFunc value)
    {
        switch (value)
        {
        case DepthStencilState::COMPARISON_NEVER:
            return D3D11_COMPARISON_NEVER;
        case DepthStencilState::COMPARISON_LESS:
            return D3D11_COMPARISON_LESS;
        case DepthStencilState::COMPARISON_EQUAL:
            return D3D11_COMPARISON_EQUAL;
        case DepthStencilState::COMPARISON_LESS_EQUAL:
            return D3D11_COMPARISON_LESS_EQUAL;
        case DepthStencilState::COMPARISON_GREATER:
            return D3D11_COMPARISON_GREATER;
        case DepthStencilState::COMPARISON_NOT_EQUAL:
            return D3D11_COMPARISON_NOT_EQUAL;
        case DepthStencilState::COMPARISON_GREATER_EQUAL:
            return D3D11_COMPARISON_GREATER_EQUAL;
        case DepthStencilState::COMPARISON_ALWAYS:
            return D3D11_COMPARISON_ALWAYS;
        default:
            CHECK_ERROR(0, "Unknown comparison func");
            return D3D11_COMPARISON_NEVER;
        }
    }

    void Device::message(MessageSeverity severity, const char* messageText, const char* file, int line)
    {
        if (messageCallback)
            messageCallback->message(severity, messageText, file, line);
        else if (severity == MessageSeverity::Error || severity == MessageSeverity::Fatal)
            abort();
    }

    D3D_PRIMITIVE_TOPOLOGY Device::getPrimType(PrimitiveType::Enum pt)
    {
        //setup the primitive type
        switch (pt)
        {
        case PrimitiveType::POINT_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
		case PrimitiveType::LINE_LIST:
			return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
			break;
        case PrimitiveType::TRIANGLE_LIST:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case PrimitiveType::TRIANGLE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        case PrimitiveType::PATCH_1_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST;
            break;
        case PrimitiveType::PATCH_3_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
            break;
        case PrimitiveType::PATCH_4_CONTROL_POINT:
            return D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
            break;
        default:
            CHECK_ERROR(0, "Unsupported type");
        }
        return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }

    void Device::disableSLIResouceSync(ID3D11Resource* resource)
    {
#if NVRHI_D3D11_WITH_NVAPI
        if (!nvapiIsInitalized)
            return;

        NVDX_ObjectHandle resouceHandle = nullptr;
        NvAPI_Status status = NvAPI_D3D_GetObjectHandleForResource(device, resource, &resouceHandle);
		if (status != NVAPI_OK)
			return;

        // If the value is 1, the driver will not track any rendering operations that would mark this resource as dirty,
        // avoiding any form of synchronization across frames rendered in parallel in multiple GPUs in AFR mode.
        NvU32 contentSyncMode = 1;
        status = NvAPI_D3D_SetResourceHint(device, resouceHandle, NVAPI_D3D_SRH_CATEGORY_SLI, NVAPI_D3D_SRH_SLI_APP_CONTROLLED_INTERFRAME_CONTENT_SYNC, &contentSyncMode);
		if (status != NVAPI_OK)
			return;
#else
        (void)resource;
#endif
    }

    uint32_t Device::getNumberOfAFRGroups()
    {
#if NVRHI_D3D11_WITH_NVAPI
        if (!nvapiIsInitalized)
            return 1; //No NVAPI

        NV_GET_CURRENT_SLI_STATE sliState = { 0 };
        sliState.version = NV_GET_CURRENT_SLI_STATE_VER;
        if (NvAPI_D3D_GetCurrentSLIState(device, &sliState) != NVAPI_OK)
            return 1;

        return sliState.numAFRGroups;
#else
        return 1;
#endif
    }

    uint32_t Device::getAFRGroupOfCurrentFrame(uint32_t numAFRGroups)
    {
#if NVRHI_D3D11_WITH_NVAPI
        if (!nvapiIsInitalized)
            return 0; //No NVAPI

        NV_GET_CURRENT_SLI_STATE sliState = { 0 };
        sliState.version = NV_GET_CURRENT_SLI_STATE_VER;
        if (NvAPI_D3D_GetCurrentSLIState(device, &sliState) != NVAPI_OK)
            return 0;

        CHECK_ERROR(sliState.numAFRGroups == numAFRGroups, "Mismatched AFR group count");
        return sliState.currentAFRIndex;
#else
        (void)numAFRGroups;
        return 0;
#endif
    }

    void Device::waitForIdle()
    {
        D3D11_QUERY_DESC queryDesc;
        queryDesc.Query = D3D11_QUERY_EVENT;
        queryDesc.MiscFlags = 0;

        ID3D11Query *query;
        HRESULT ret;

        ret = device->CreateQuery(&queryDesc, &query);
        CHECK_ERROR(SUCCEEDED(ret), "failed to create query");

        context->End(query);

        for(;;)
        {
            ret = context->GetData(query, nullptr, 0, 0);
            if (SUCCEEDED(ret))
                break;
        }

        query->Release();
    }

    void Device::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)
    {
        (void)texture; 

        if (enableBarriers)
            leaveUAVOverlapSection();
        else
            enterUAVOverlapSection();
    }

    void Device::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)
    {
        (void)buffer;

        if (enableBarriers)
            leaveUAVOverlapSection();
        else
            enterUAVOverlapSection();
    }

    void Device::enterUAVOverlapSection()
    {
#if NVRHI_D3D11_WITH_NVAPI
        if (m_numUAVOverlapCommands == 0)
            NvAPI_D3D11_BeginUAVOverlap(context);
#endif

        m_numUAVOverlapCommands += 1;
    }

    void Device::leaveUAVOverlapSection()
    {
#if NVRHI_D3D11_WITH_NVAPI
        if (m_numUAVOverlapCommands == 1)
            NvAPI_D3D11_EndUAVOverlap(context);
#endif

        m_numUAVOverlapCommands = std::max(0, m_numUAVOverlapCommands - 1);
    }

} // namespace d3d11
} // namespace nvrhi
