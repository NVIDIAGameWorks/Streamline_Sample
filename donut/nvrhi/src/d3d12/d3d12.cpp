/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <nvrhi/common/crc.h>
#include <nvrhi/common/containers.h>
#include <nvrhi/common/shader-blob.h>
#include <nvrhi/d3d12/d3d12.h>
#include <nvrhi/d3d12/internals.h>

#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <assert.h>
#include <list>
#include <sstream>


#if NVRHI_D3D12_WITH_NVAPI
#ifdef _WIN64
#pragma comment(lib, "nvapi64.lib")
#else
#pragma comment(lib, "nvapi.lib")
#endif
#endif

#pragma comment(lib, "dxguid.lib")

#define ENABLE_D3D_REFLECTION 0

#if ENABLE_D3D_REFLECTION
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif

#define HR_RETURN(hr) if(FAILED(hr)) return hr;

#ifdef _DEBUG
#define DEBUG_PRINT(x) OutputDebugStringA(x)
#define DEBUG_PRINTF(format, ...) { char buf[256]; sprintf_s(buf, format, ## __VA_ARGS__); OutputDebugStringA(buf); }
#else
#define DEBUG_PRINT(x) (void)(x)
#define DEBUG_PRINTF(format, ...) (void)(format)
#endif

#define START_CPU_PERF LARGE_INTEGER timeBegin, timeEnd, timeFreq; QueryPerformanceCounter(&timeBegin);
#define END_CPU_PERF(Target) QueryPerformanceCounter(&timeEnd); QueryPerformanceFrequency(&timeFreq); double Target = double(timeEnd.QuadPart - timeBegin.QuadPart) / double(timeFreq.QuadPart);

namespace nvrhi
{
namespace d3d12
{
    ResourceType GetNormalizedResourceType(ResourceType type)
    {
        switch (type)
        {
        case ResourceType::StructuredBuffer_UAV:
            return ResourceType::Buffer_UAV;
        case ResourceType::StructuredBuffer_SRV:
            return ResourceType::Buffer_SRV;
        default:
            return type;
        }
    }

    void WaitForFence(ID3D12Fence* fence, uint64_t value, HANDLE event)
    {
        // Test if the fence has been reached
        if (fence->GetCompletedValue() < value)
        {
            // If it's not, wait for it to finish using an event
            ResetEvent(event);
            fence->SetEventOnCompletion(value, event);
            WaitForSingleObject(event, INFINITE);
        }
    }

    StagingTexture::SliceRegion StagingTexture::getSliceRegion(ID3D12Device *device, const TextureSlice& slice)
    {
        SliceRegion ret;
        const UINT subresource = calcSubresource(slice.mipLevel, slice.arraySlice, 0,
            desc.mipLevels, desc.arraySize);

        assert(subresource < subresourceOffsets.size());

		UINT64 size = 0;
        device->GetCopyableFootprints(&resourceDesc, subresource, 1, subresourceOffsets[subresource], &ret.footprint, nullptr, nullptr, &size);
        ret.offset = off_t(ret.footprint.Offset);
		ret.size = static_cast<size_t>(size);
        return ret;
    }

    size_t StagingTexture::getSizeInBytes(ID3D12Device *device)
    {
        // figure out the index of the last subresource
        const UINT lastSubresource = calcSubresource(desc.mipLevels - 1, desc.arraySize - 1, 0,
            desc.mipLevels, desc.arraySize);
        assert(lastSubresource < subresourceOffsets.size());

        // compute size of last subresource
        UINT64 lastSubresourceSize;
        device->GetCopyableFootprints(&resourceDesc, lastSubresource, 1, 0,
            nullptr, nullptr, nullptr, &lastSubresourceSize);

        return static_cast<size_t>(subresourceOffsets[lastSubresource] + lastSubresourceSize);
    }

    void StagingTexture::computeSubresourceOffsets(ID3D12Device *device)
    {
        const UINT lastSubresource = calcSubresource(desc.mipLevels - 1, desc.arraySize - 1, 0,
            desc.mipLevels, desc.arraySize);

        const UINT numSubresources = lastSubresource + 1;
        subresourceOffsets.resize(numSubresources);

        UINT64 baseOffset = 0;
        for (UINT i = 0; i < lastSubresource + 1; i++)
        {
            UINT64 subresourceSize;
            device->GetCopyableFootprints(&resourceDesc, i, 1, 0,
                nullptr, nullptr, nullptr, &subresourceSize);

            subresourceOffsets[i] = baseOffset;
            baseOffset += subresourceSize;
            baseOffset = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT * ((baseOffset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        }
    }

    static bool AreResourceTypesCompatible(ResourceType a, ResourceType b)
    {
        if (a == b)
            return true;

        a = GetNormalizedResourceType(a);
        b = GetNormalizedResourceType(b);

        if (a == ResourceType::Buffer_SRV && b == ResourceType::Texture_SRV ||
            b == ResourceType::Buffer_SRV && a == ResourceType::Texture_SRV ||
            a == ResourceType::Buffer_SRV && b == ResourceType::RayTracingAccelStruct ||
            a == ResourceType::Texture_SRV && b == ResourceType::RayTracingAccelStruct ||
            b == ResourceType::Buffer_SRV && a == ResourceType::RayTracingAccelStruct ||
            b == ResourceType::Texture_SRV && a == ResourceType::RayTracingAccelStruct)
            return true;

        if (a == ResourceType::Buffer_UAV && b == ResourceType::Texture_UAV ||
            b == ResourceType::Buffer_UAV && a == ResourceType::Texture_UAV)
            return true;

        return false;
    }


    StageBindingLayout::StageBindingLayout(const StageBindingLayoutDesc& layout, ShaderType::Enum _shaderType)
        : shaderType(_shaderType)
    {
        // Start with some invalid values, to make sure that we start a new range on the first binding
        ResourceType currentType = ResourceType(-1);
        uint32_t currentSlot = ~0u;

        for (const BindingLayoutItem& binding : layout)
        {
            if (binding.type == ResourceType::VolatileConstantBuffer)
            {
                D3D12_ROOT_DESCRIPTOR1 rootDescriptor;
                rootDescriptor.ShaderRegister = binding.slot;
                rootDescriptor.RegisterSpace = binding.registerSpace;

                // Volatile CBs are static descriptors, however strange that may seem.
                // A volatile CB can only be bound to a command list after it's been written into, and 
                // after that the data will not change until the command list has finished executing.
                // Subsequent writes will be made into a newly allocated portion of an upload buffer.
                rootDescriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
                    
                rootParametersVolatileCB.push_back(std::make_pair(-1, rootDescriptor));
            }
            else if (!AreResourceTypesCompatible(binding.type, currentType) || binding.slot != currentSlot + 1)
            {
                // Start a new range

                if (binding.type == ResourceType::Sampler)
                {
                    descriptorRangesSamplers.resize(descriptorRangesSamplers.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];
                        
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    range.NumDescriptors = 1;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = binding.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSamplers;
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

                    descriptorTableSizeSamplers += 1;
                }
                else
                {
                    descriptorRangesSRVetc.resize(descriptorRangesSRVetc.size() + 1);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];

                    switch (GetNormalizedResourceType(binding.type))
                    {
                    case ResourceType::Texture_SRV:
                    case ResourceType::Buffer_SRV:
                    case ResourceType::RayTracingAccelStruct:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                        break;
                    case ResourceType::Texture_UAV:
                    case ResourceType::Buffer_UAV:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                        break;
                    case ResourceType::ConstantBuffer:
                        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                        break;
                    default:
                        // Unknown binding type
                        assert(false);
                    }
                    range.NumDescriptors = 1;
                    range.BaseShaderRegister = binding.slot;
                    range.RegisterSpace = binding.registerSpace;
                    range.OffsetInDescriptorsFromTableStart = descriptorTableSizeSRVetc;

                    // We don't know how apps will use resources referenced in a binding set. They may bind 
                    // a buffer to the command list and then copy data into it.
                    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;

                    descriptorTableSizeSRVetc += 1;

                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentType = binding.type;
                currentSlot = binding.slot;
            }
            else
            {
                // Extend the current range

                if (binding.type == ResourceType::Sampler)
                {
                    assert(descriptorRangesSamplers.size() > 0);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSamplers[descriptorRangesSamplers.size() - 1];

                    range.NumDescriptors += 1;
                    descriptorTableSizeSamplers += 1; 
                }
                else
                {
                    assert(descriptorRangesSRVetc.size() > 0);
                    D3D12_DESCRIPTOR_RANGE1& range = descriptorRangesSRVetc[descriptorRangesSRVetc.size() - 1];

                    range.NumDescriptors += 1;
                    descriptorTableSizeSRVetc += 1;

                    bindingLayoutsSRVetc.push_back(binding);
                }

                currentSlot = binding.slot;
            }
        }
    }

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
    
    StaticDescriptorHeap::StaticDescriptorHeap(Device* pParent)
        : m_pParent(pParent)
    {
    }

    HRESULT StaticDescriptorHeap::AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE heapType, uint32_t numDescriptors, bool shaderVisible)
    {
        HRESULT hr;

        m_Heap = nullptr;
        m_ShaderVisibleHeap = nullptr;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = heapType;
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = m_pParent->m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_Heap));

        HR_RETURN(hr);

        if (shaderVisible)
        {
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            hr = m_pParent->m_pDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_ShaderVisibleHeap));

            HR_RETURN(hr);

            m_StartCpuHandleShaderVisible = m_ShaderVisibleHeap->GetCPUDescriptorHandleForHeapStart();
            m_StartGpuHandleShaderVisible = m_ShaderVisibleHeap->GetGPUDescriptorHandleForHeapStart();
        }

        m_NumDescriptors = heapDesc.NumDescriptors;
        m_HeapType = heapDesc.Type;
        m_StartCpuHandle = m_Heap->GetCPUDescriptorHandleForHeapStart();
        m_Stride = m_pParent->m_pDevice->GetDescriptorHandleIncrementSize(heapDesc.Type);
        m_AllocatedDescriptors.resize(m_NumDescriptors);

        return S_OK;
    }

    HRESULT StaticDescriptorHeap::Grow()
    {
        // TODO: make this method thread-safe

        uint32_t oldSize = m_NumDescriptors;

        RefCountPtr<ID3D12DescriptorHeap> oldHeap = m_Heap; 

        HRESULT hr = AllocateResources(m_HeapType, m_NumDescriptors * 2, m_ShaderVisibleHeap != nullptr);

        HR_RETURN(hr);

        m_pParent->m_pDevice->CopyDescriptorsSimple(oldSize, m_StartCpuHandle, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);

        if (m_ShaderVisibleHeap != nullptr)
        {
            m_pParent->m_pDevice->CopyDescriptorsSimple(oldSize, m_StartCpuHandleShaderVisible, oldHeap->GetCPUDescriptorHandleForHeapStart(), m_HeapType);
        }

        return S_OK;
    }

    DescriptorIndex StaticDescriptorHeap::AllocateDescriptors(uint32_t count)
    {
        DescriptorIndex foundIndex = 0;
        uint32_t freeCount = 0;
        bool found = false;

        // Find a contiguous range of 'count' indices for which m_AllocatedDescriptors[index] is false

        for (DescriptorIndex index = m_SearchStart; index < m_NumDescriptors; index++)
        {
            if (m_AllocatedDescriptors[index])
                freeCount = 0;
            else
                freeCount += 1;

            if (freeCount >= count)
            {
                foundIndex = index - count + 1;
                found = true;
                break;
            }
        }

        if (!found)
        {
            foundIndex = m_NumDescriptors;

            if(FAILED(Grow()))
            {
                m_pParent->message(MessageSeverity::Fatal, "Failed to grow a descriptor heap!");
                return INVALID_DESCRIPTOR_INDEX;
            }
        }

        for (DescriptorIndex index = foundIndex; index < foundIndex + count; index++)
        {
            m_AllocatedDescriptors[index] = true;
        }

        m_NumAllocatedDescriptors += count;

        m_SearchStart = foundIndex + count;
        return foundIndex;
    }

    DescriptorIndex StaticDescriptorHeap::AllocateDescriptor()
    {
        return AllocateDescriptors(1);
    }

    void StaticDescriptorHeap::ReleaseDescriptors(DescriptorIndex baseIndex, uint32_t count)
    {
        for (DescriptorIndex index = baseIndex; index < baseIndex + count; index++)
        {
#ifdef _DEBUG
            if (!m_AllocatedDescriptors[index])
            {
                m_pParent->message(MessageSeverity::Error, "Attempted to release an un-allocated descriptor");
            }
#endif

            m_AllocatedDescriptors[index] = false;
        }

        m_NumAllocatedDescriptors -= count;

        if (m_SearchStart > baseIndex)
            m_SearchStart = baseIndex;
    }

    void StaticDescriptorHeap::ReleaseDescriptor(DescriptorIndex index)
    {
        ReleaseDescriptors(index, 1);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::GetCpuHandle(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandle;
        handle.ptr += index * m_Stride;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::GetCpuHandleShaderVisible(DescriptorIndex index)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_StartCpuHandleShaderVisible;
        handle.ptr += index * m_Stride;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE StaticDescriptorHeap::GetGpuHandle(DescriptorIndex index)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = m_StartGpuHandleShaderVisible;
        handle.ptr += index * m_Stride;
        return handle;
    }

    ID3D12DescriptorHeap* StaticDescriptorHeap::GetHeap() const
    {
        return m_Heap;
    }

    ID3D12DescriptorHeap* StaticDescriptorHeap::GetShaderVisibleHeap() const
    {
        return m_ShaderVisibleHeap;
    }

    void StaticDescriptorHeap::CopyToShaderVisibleHeap(DescriptorIndex index, uint32_t count)
    {
        m_pParent->m_pDevice->CopyDescriptorsSimple(count, GetCpuHandleShaderVisible(index), GetCpuHandle(index), m_HeapType);
    }

    struct UploadManager::Chunk 
    {
        static const uint32_t sizeAlignment = 4096; // GPU page size

        RefCountPtr<ID3D12Resource> buffer;
        UINT64 fenceValue = 0;
        size_t bufferSize = 0;
        size_t writePointer = 0;
        void* cpuVA = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;
        uint32_t identifier = 0;

        ~Chunk()
        {
            if (buffer && cpuVA)
            {
                buffer->Unmap(0, nullptr);
                cpuVA = nullptr;
            }
        }

        bool TryToAllocate(size_t size, ID3D12Resource** pBuffer, size_t* pOffset, void** pCpuVA, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, uint32_t alignment, UINT64 currentFenceValue)
        {
            size_t alignedWritePointer = Align(writePointer, alignment);

            if (alignedWritePointer + size > bufferSize)
                return false;

            if (pCpuVA) *pCpuVA = (char*)cpuVA + alignedWritePointer;
            if (pGpuVA) *pGpuVA = gpuVA + alignedWritePointer;
            if (pBuffer) *pBuffer = buffer;
            if (pOffset) *pOffset = alignedWritePointer;
            fenceValue = currentFenceValue;
            writePointer = alignedWritePointer + size;

            return true;
        }
    };

    
    UploadManager::UploadManager(Device* pParent, size_t defaultChunkSize)
        : m_pParent(pParent)
        , m_DefaultChunkSize(defaultChunkSize)
    {
    }

    std::shared_ptr<UploadManager::Chunk> UploadManager::CreateChunk(size_t size)
    {
        auto chunk = std::make_shared<Chunk>();

        size = Align(size, Chunk::sizeAlignment);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = size;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = m_pParent->m_pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            NULL,
            IID_PPV_ARGS(&chunk->buffer));

        if (FAILED(hr))
            return nullptr;

        hr = chunk->buffer->Map(0, nullptr, &chunk->cpuVA);

        if (FAILED(hr))
            return nullptr;

        chunk->bufferSize = size;
        chunk->gpuVA = chunk->buffer->GetGPUVirtualAddress();
        chunk->identifier = uint32_t(m_ChunkPool.size());

        return chunk;
    }
        
    bool UploadManager::SuballocateBuffer(size_t size, ID3D12Resource** pBuffer, size_t* pOffset, void** pCpuVA, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, UINT64 currentFence, UINT64 completedFence, uint32_t alignment)
    {
        if (m_CurrentChunk != nullptr)
        {
            // Try to allocate from the current chunk first
            if (m_CurrentChunk->TryToAllocate(size, pBuffer, pOffset, pCpuVA, pGpuVA, alignment, currentFence))
                return true;

            // No luck, put the current chunk into the pool
            m_ChunkPool.push_back(m_CurrentChunk);
            m_CurrentChunk = nullptr;
        }

        // Try to find a chunk in the pool that's no longer used and is large enough to allocate our buffer
        for (auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); it++)
        {
            std::shared_ptr<Chunk> chunk = *it;
            if (chunk->fenceValue <= completedFence && chunk->bufferSize >= size)
            {
                chunk->writePointer = 0;

                if (chunk->TryToAllocate(size, pBuffer, pOffset, pCpuVA, pGpuVA, alignment, currentFence))
                {
                    m_CurrentChunk = chunk;
                    m_ChunkPool.erase(it);
                    return true;
                }
            }
        }

        m_CurrentChunk = CreateChunk(std::max(size, m_DefaultChunkSize));

        if (m_CurrentChunk == nullptr)
            return false;

        if (m_CurrentChunk->TryToAllocate(size, pBuffer, pOffset, pCpuVA, pGpuVA, alignment, currentFence))
            return true;

        // shouldn't happen
        return false;
    }

    std::shared_ptr<UploadManager::Chunk> DxrScratchManager::CreateChunk(size_t size)
    {
        auto chunk = std::make_shared<UploadManager::Chunk>();

        size = Align(size, UploadManager::Chunk::sizeAlignment);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = size;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = m_pParent->m_pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            NULL,
            IID_PPV_ARGS(&chunk->buffer));

        if (FAILED(hr))
            return nullptr;

        chunk->bufferSize = size;
        chunk->gpuVA = chunk->buffer->GetGPUVirtualAddress();
        chunk->identifier = uint32_t(m_ChunkPool.size());

        std::wstringstream wss;
        wss << L"DXR Scratch Buffer " << chunk->identifier;
        chunk->buffer->SetName(wss.str().c_str());

        return chunk;
    }

    DxrScratchManager::DxrScratchManager(Device* pParent, size_t defaultChunkSize, size_t maxTotalMemory)
        : m_pParent(pParent)
        , m_DefaultChunkSize(defaultChunkSize)
        , m_MaxTotalMemory(maxTotalMemory)
    {
    }

    bool DxrScratchManager::SuballocateBuffer(ID3D12GraphicsCommandList* pCommandList, size_t size, D3D12_GPU_VIRTUAL_ADDRESS* pGpuVA, UINT64 currentFence, UINT64 completedFence, uint32_t alignment /*= 256*/)
    {
        if (m_CurrentChunk != nullptr)
        {
            // Try to allocate from the current chunk first
            if (m_CurrentChunk->TryToAllocate(size, nullptr, nullptr, nullptr, pGpuVA, alignment, currentFence))
                return true;

            // No luck, put the current chunk into the pool
            m_ChunkPool.push_back(m_CurrentChunk);
            m_CurrentChunk = nullptr;
        }

        // Try to find a chunk in the pool that's no longer used and is large enough to allocate our buffer
        for (auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); it++)
        {
            std::shared_ptr<UploadManager::Chunk> chunk = *it;
            if (chunk->fenceValue <= completedFence && chunk->bufferSize >= size)
            {
                chunk->writePointer = 0;

                if (chunk->TryToAllocate(size, nullptr, nullptr, nullptr, pGpuVA, alignment, currentFence))
                {
                    m_CurrentChunk = chunk;
                    m_ChunkPool.erase(it);
                    return true;
                }
            }
        }

        // Not found - see if we're allowed to allocate more memory
        size_t newChunkSize = Align(size, UploadManager::Chunk::sizeAlignment);
        newChunkSize = std::max(newChunkSize, m_DefaultChunkSize);
        if (m_AllocatedMemory + newChunkSize <= m_MaxTotalMemory)
        {
            // We're allowed: allocate it.
            m_CurrentChunk = CreateChunk(newChunkSize);
            m_AllocatedMemory += m_CurrentChunk->bufferSize;
        }
        else
        {
            // Nope, need to reuse something.
            // Find the least recently used chunk that can fit our buffer.

            std::shared_ptr<UploadManager::Chunk> candidate;
            for (auto it = m_ChunkPool.begin(); it != m_ChunkPool.end(); it++)
            {
                std::shared_ptr<UploadManager::Chunk> chunk = *it;
                if (chunk->bufferSize >= size && (!candidate || (chunk->fenceValue < candidate->fenceValue)))
                {
                    candidate = chunk;
                }
            }

            if (!candidate)
            {
                // No chunk found that's large enough. And we can't allocate. :(
                return false;
            }

            // Found - now it's the current chunk; reset it.
            m_CurrentChunk = candidate;
            m_CurrentChunk->writePointer = 0;

            // Place a UAV barrier on the chunk.
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = candidate->buffer;
            pCommandList->ResourceBarrier(1, &barrier);
        }

        if (m_CurrentChunk == nullptr)
            return false;

        if (m_CurrentChunk->TryToAllocate(size, nullptr, nullptr, nullptr, pGpuVA, alignment, currentFence))
            return true;

        // shouldn't happen
        return false;
    }

    // returns -1 on failure
    uint32_t Device::allocateTimerQuerySlot()
    {
        uint32_t slot = m_nextTimerQueryIndex;

        while (slot < numTimerQueries && m_allocatedQueries.test(slot))
        {
            slot++;
        }

        if (slot >= numTimerQueries)
        {
            return uint32_t(-1);
        }

        m_allocatedQueries.set(slot);
        m_nextTimerQueryIndex = slot + 1;
        return slot;
    }

    void Device::releaseTimerQuerySlot(uint32_t slot)
    {
        if (slot != uint32_t(-1))
        {
            m_allocatedQueries.reset(slot);
            m_nextTimerQueryIndex = std::min(m_nextTimerQueryIndex, slot);
        }
    }

    Device::Device(IMessageCallback * errorCB, ID3D12Device * pDevice, ID3D12CommandQueue * pCommandQueue)
        : m_pMessageCallback(errorCB)
        , m_pDevice(pDevice)
        , m_pCommandQueue(pCommandQueue)
        , m_dhRTV(this)
        , m_dhDSV(this)
        , m_dhSRVetc(this)
        , m_dhSamplers(this)
        , m_nextTimerQueryIndex(0)
        , m_NvapiIsInitialized(false)
        , m_SinglePassStereoSupported(false)
    {
        m_dhDSV.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1024, false);
        m_dhRTV.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1024, false);
        m_dhSRVetc.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, true);
        m_dhSamplers.AllocateResources(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1024, true);

#if NVRHI_WITH_DXR
        if (FAILED(m_pDevice->QueryInterface(&m_pDevice5)))
        {
            m_pDevice5 = nullptr;
        }
        else
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5;

            if (SUCCEEDED(m_pDevice5->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
            {
                m_RayTracingSupported = options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
            }
        }
#endif
        
        {
            D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
            D3D12_COMMAND_SIGNATURE_DESC csDesc = {};
            csDesc.NumArgumentDescs = 1;
            csDesc.pArgumentDescs = &argDesc;

            csDesc.ByteStride = 16;
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            m_pDevice->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_drawIndirectSignature));

            csDesc.ByteStride = 12;
            argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            m_pDevice->CreateCommandSignature(&csDesc, nullptr, IID_PPV_ARGS(&m_dispatchIndirectSignature));
        }
        
        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.Count = numTimerQueries;
        m_pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_timerQueryHeap));

        BufferDesc qbDesc;
        qbDesc.byteSize = queryHeapDesc.Count * 8;
        qbDesc.cpuAccess = CpuAccessMode::Read;

        BufferHandle timerQueryBuffer = createBuffer(qbDesc);
        m_timerQueryResolveBuffer = static_cast<Buffer*>(timerQueryBuffer.Get());

        m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);

#if NVRHI_D3D12_WITH_NVAPI
        //We need to use NVAPI to set resource hints for SLI
        m_NvapiIsInitialized = NvAPI_Initialize() == NVAPI_OK;

        if (m_NvapiIsInitialized)
        {
            NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS stereoParams = { 0 };
            stereoParams.version = NV_QUERY_SINGLE_PASS_STEREO_SUPPORT_PARAMS_VER;

            if (NvAPI_D3D12_QuerySinglePassStereoSupport(m_pDevice, &stereoParams) == NVAPI_OK && stereoParams.bSinglePassStereoSupported)
            {
                m_SinglePassStereoSupported = true;
            }
        }
#endif
    }

    Device::~Device()
    {
        waitForIdle();

        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = 0;
        }
    }

    void Device::waitForIdle()
    {
        // Trivial return if there is nothing in-flight
        if (m_CommandListsInFlight.empty())
            return;

        // Get the last launched CL. When that one is finished, every CL before it is finished too.
        std::shared_ptr<CommandListInstance> lastCommandList = m_CommandListsInFlight.back();

        // Wait for the last CL to be finished
        WaitForFence(lastCommandList->fence, lastCommandList->instanceID, m_fenceEvent);

        // Release all resources used by all in-flight CLs
        while (!m_CommandListsInFlight.empty())
            m_CommandListsInFlight.pop();
    }

    void Device::message(MessageSeverity severity, const char* messageText, const char* file, int line)
    {
        if (m_pMessageCallback)
            m_pMessageCallback->message(severity, messageText, file, line);
        else if(severity == MessageSeverity::Error || severity == MessageSeverity::Fatal)
            abort();
    }

    D3D12_SHADER_VISIBILITY convertShaderStage(ShaderType::Enum s)
    {
        switch (s)
        {
        case ShaderType::SHADER_VERTEX:
            return D3D12_SHADER_VISIBILITY_VERTEX;
        case ShaderType::SHADER_HULL:
            return D3D12_SHADER_VISIBILITY_HULL;
        case ShaderType::SHADER_DOMAIN:
            return D3D12_SHADER_VISIBILITY_DOMAIN;
        case ShaderType::SHADER_GEOMETRY:
            return D3D12_SHADER_VISIBILITY_GEOMETRY;
        case ShaderType::SHADER_PIXEL:
            return D3D12_SHADER_VISIBILITY_PIXEL;

        case ShaderType::SHADER_COMPUTE:
        case ShaderType::SHADER_ALL_GRAPHICS:
        default:
            return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    BindingLayout::BindingLayout(const BindingLayoutDesc& _desc)
        : desc(_desc)
    {
        if (desc.VS.size()) stages[ShaderType::SHADER_VERTEX] = std::make_shared<StageBindingLayout>(desc.VS, ShaderType::SHADER_VERTEX);
        if (desc.HS.size()) stages[ShaderType::SHADER_HULL] = std::make_shared<StageBindingLayout>(desc.HS, ShaderType::SHADER_HULL);
        if (desc.DS.size()) stages[ShaderType::SHADER_DOMAIN] = std::make_shared<StageBindingLayout>(desc.DS, ShaderType::SHADER_DOMAIN);
        if (desc.GS.size()) stages[ShaderType::SHADER_GEOMETRY] = std::make_shared<StageBindingLayout>(desc.GS, ShaderType::SHADER_GEOMETRY);
        if (desc.PS.size()) stages[ShaderType::SHADER_PIXEL] = std::make_shared<StageBindingLayout>(desc.PS, ShaderType::SHADER_PIXEL);
        if (desc.CS.size()) stages[ShaderType::SHADER_COMPUTE] = std::make_shared<StageBindingLayout>(desc.CS, ShaderType::SHADER_COMPUTE);
        if (desc.ALL.size()) stages[ShaderType::SHADER_ALL_GRAPHICS] = std::make_shared<StageBindingLayout>(desc.ALL, ShaderType::SHADER_ALL_GRAPHICS);

        // A PipelineBindingLayout occupies a contiguous segment of a root signature.
        // The root parameter indices stored here are relative to the beginning of that segment, not to the RS item 0.

        rootParameters.resize(0);
        for (std::shared_ptr<StageBindingLayout>& stageLayout : stages)
        {
            if (stageLayout == nullptr)
                continue;

            for (std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& rootParameterVolatileCB : stageLayout->rootParametersVolatileCB)
            {
                rootParameters.resize(rootParameters.size() + 1);
                D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                param.ShaderVisibility = convertShaderStage(stageLayout->shaderType);
                param.Descriptor = rootParameterVolatileCB.second;

                rootParameterVolatileCB.first = RootParameterIndex(rootParameters.size() - 1);
            }

            if (stageLayout->descriptorTableSizeSamplers > 0)
            {
                rootParameters.resize(rootParameters.size() + 1);
                D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = convertShaderStage(stageLayout->shaderType);
                param.DescriptorTable.NumDescriptorRanges = UINT(stageLayout->descriptorRangesSamplers.size());
                param.DescriptorTable.pDescriptorRanges = &stageLayout->descriptorRangesSamplers[0];

                stageLayout->rootParameterSamplers = RootParameterIndex(rootParameters.size() - 1);
            }

            if (stageLayout->descriptorTableSizeSRVetc > 0)
            {
                rootParameters.resize(rootParameters.size() + 1);
                D3D12_ROOT_PARAMETER1& param = rootParameters[rootParameters.size() - 1];

                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.ShaderVisibility = convertShaderStage(stageLayout->shaderType);
                param.DescriptorTable.NumDescriptorRanges = UINT(stageLayout->descriptorRangesSRVetc.size());
                param.DescriptorTable.pDescriptorRanges = &stageLayout->descriptorRangesSRVetc[0];

                stageLayout->rootParameterSRVetc = RootParameterIndex(rootParameters.size() - 1);
            }
        }
    }

    RootSignatureHandle Device::buildRootSignature(const static_vector<BindingLayoutHandle, MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout, bool isLocal, const D3D12_ROOT_PARAMETER1* pCustomParameters, uint32_t numCustomParameters)
    {
        HRESULT hr;

        RootSignature* rootsig = new RootSignature(this);
        
        // Assemble the root parameter table from the pipeline binding layouts
        // Also attach the root parameter offsets to the pipeline layouts

        std::vector<D3D12_ROOT_PARAMETER1> rootParameters;

        // Add custom parameters in the beginning of the RS
        for (uint32_t index = 0; index < numCustomParameters; index++)
        {
            rootParameters.push_back(pCustomParameters[index]);
        }

        for(uint32_t layoutIndex = 0; layoutIndex < uint32_t(pipelineLayouts.size()); layoutIndex++)
        {
            BindingLayout* layout = static_cast<BindingLayout*>(pipelineLayouts[layoutIndex].Get());
            RootParameterIndex rootParameterOffset = RootParameterIndex(rootParameters.size());

            rootsig->pipelineLayouts.push_back(std::make_pair(layout, rootParameterOffset));

            rootParameters.insert(rootParameters.end(), layout->rootParameters.begin(), layout->rootParameters.end());
        }

        // Build the description structure

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (allowInputLayout)
        {
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        }
#if NVRHI_WITH_DXR
        if (isLocal)
        {
            rsDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
        }
#else
        (void)isLocal;
        assert(!isLocal);
#endif

        if (!rootParameters.empty())
        {
            rsDesc.Desc_1_1.pParameters = rootParameters.data();
            rsDesc.Desc_1_1.NumParameters = UINT(rootParameters.size());
        }

        // Serialize the root signature

        RefCountPtr<ID3DBlob> rsBlob;
        RefCountPtr<ID3DBlob> errorBlob;
        hr = D3D12SerializeVersionedRootSignature(&rsDesc, &rsBlob, &errorBlob);

        if (FAILED(hr))
        {
            SIGNAL_ERROR("Failed to serialize a root signature");

            if (errorBlob)
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());

            return nullptr;
        }

        // Create the RS object

        hr = m_pDevice->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&rootsig->handle));

        CHECK_ERROR(SUCCEEDED(hr), "Failed to create a root signature object");

        return RootSignatureHandle::Create(rootsig);
    }

    RefCountPtr<RootSignature> Device::getRootSignature(const static_vector<BindingLayoutHandle, MaxBindingLayouts>& pipelineLayouts, bool allowInputLayout)
    {
        CrcHash hasher;

        for (const BindingLayoutHandle& pipelineLayout : pipelineLayouts)
            hasher.Add(pipelineLayout.Get());
        
        hasher.Add(allowInputLayout ? 1u : 0u);

        uint32_t hash = hasher.Get();

        // Get a cached RS and AddRef it (if it exists)
        RefCountPtr<RootSignature> rootsig = m_rootsigCache[hash];

        if (!rootsig)
        {
            // Does not exist - build a new one, take ownership
            rootsig = static_cast<RootSignature*>(buildRootSignature(pipelineLayouts, allowInputLayout, false).Get());
            rootsig->hash = hash;

            m_rootsigCache[hash] = rootsig;
        }

        // Pass ownership of the RS to caller
        return rootsig;
    }

    RootSignature::~RootSignature()
    {
        parent->removeRootSignatureFromCache(this);
    }

    Object RootSignature::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_RootSignature:
            return Object(handle.Get());
        default:
            return nullptr;
        }
    }

    Object GraphicsPipeline::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_RootSignature:
            return rootSignature->getNativeObject(objectType);
        case ObjectTypes::D3D12_PipelineState:
            return Object(pipelineState.Get());
        default:
            return nullptr;
        }
    }

    Object ComputePipeline::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_RootSignature:
            return rootSignature->getNativeObject(objectType);
        case ObjectTypes::D3D12_PipelineState:
            return Object(pipelineState.Get());
        default:
            return nullptr;
        }
    }

    D3D12_BLEND convertBlendValue(BlendState::BlendValue value)
    {
        switch (value)
        {
        case BlendState::BLEND_ZERO:
            return D3D12_BLEND_ZERO;
        case BlendState::BLEND_ONE:
            return D3D12_BLEND_ONE;
        case BlendState::BLEND_SRC_COLOR:
            return D3D12_BLEND_SRC_COLOR;
        case BlendState::BLEND_INV_SRC_COLOR:
            return D3D12_BLEND_INV_SRC_COLOR;
        case BlendState::BLEND_SRC_ALPHA:
            return D3D12_BLEND_SRC_ALPHA;
        case BlendState::BLEND_INV_SRC_ALPHA:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendState::BLEND_DEST_ALPHA:
            return D3D12_BLEND_DEST_ALPHA;
        case BlendState::BLEND_INV_DEST_ALPHA:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendState::BLEND_DEST_COLOR:
            return D3D12_BLEND_DEST_COLOR;
        case BlendState::BLEND_INV_DEST_COLOR:
            return D3D12_BLEND_INV_DEST_COLOR;
        case BlendState::BLEND_SRC_ALPHA_SAT:
            return D3D12_BLEND_SRC_ALPHA_SAT;
        case BlendState::BLEND_BLEND_FACTOR:
            return D3D12_BLEND_BLEND_FACTOR;
        case BlendState::BLEND_INV_BLEND_FACTOR:
            return D3D12_BLEND_INV_BLEND_FACTOR;
        case BlendState::BLEND_SRC1_COLOR:
            return D3D12_BLEND_SRC1_COLOR;
        case BlendState::BLEND_INV_SRC1_COLOR:
            return D3D12_BLEND_INV_SRC1_COLOR;
        case BlendState::BLEND_SRC1_ALPHA:
            return D3D12_BLEND_SRC1_ALPHA;
        case BlendState::BLEND_INV_SRC1_ALPHA:
            return D3D12_BLEND_INV_SRC1_ALPHA;
        default:
            return D3D12_BLEND_ZERO;
        }
    }

    D3D12_BLEND_OP convertBlendOp(BlendState::BlendOp value)
    {
        switch (value)
        {
        case BlendState::BLEND_OP_ADD:
            return D3D12_BLEND_OP_ADD;
        case BlendState::BLEND_OP_SUBTRACT:
            return D3D12_BLEND_OP_SUBTRACT;
        case BlendState::BLEND_OP_REV_SUBTRACT:
            return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendState::BLEND_OP_MIN:
            return D3D12_BLEND_OP_MIN;
        case BlendState::BLEND_OP_MAX:
            return D3D12_BLEND_OP_MAX;
        default:
            return D3D12_BLEND_OP_ADD;
        }
    }

    D3D12_STENCIL_OP convertStencilOp(DepthStencilState::StencilOp value)
    {
        switch (value)
        {
        case DepthStencilState::STENCIL_OP_KEEP:
            return D3D12_STENCIL_OP_KEEP;
        case DepthStencilState::STENCIL_OP_ZERO:
            return D3D12_STENCIL_OP_ZERO;
        case DepthStencilState::STENCIL_OP_REPLACE:
            return D3D12_STENCIL_OP_REPLACE;
        case DepthStencilState::STENCIL_OP_INCR_SAT:
            return D3D12_STENCIL_OP_INCR_SAT;
        case DepthStencilState::STENCIL_OP_DECR_SAT:
            return D3D12_STENCIL_OP_DECR_SAT;
        case DepthStencilState::STENCIL_OP_INVERT:
            return D3D12_STENCIL_OP_INVERT;
        case DepthStencilState::STENCIL_OP_INCR:
            return D3D12_STENCIL_OP_INCR;
        case DepthStencilState::STENCIL_OP_DECR:
            return D3D12_STENCIL_OP_DECR;
        default:
            return D3D12_STENCIL_OP_KEEP;
        }
    }

    D3D12_COMPARISON_FUNC convertComparisonFunc(DepthStencilState::ComparisonFunc value)
    {
        switch (value)
        {
        case DepthStencilState::COMPARISON_NEVER:
            return D3D12_COMPARISON_FUNC_NEVER;
        case DepthStencilState::COMPARISON_LESS:
            return D3D12_COMPARISON_FUNC_LESS;
        case DepthStencilState::COMPARISON_EQUAL:
            return D3D12_COMPARISON_FUNC_EQUAL;
        case DepthStencilState::COMPARISON_LESS_EQUAL:
            return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case DepthStencilState::COMPARISON_GREATER:
            return D3D12_COMPARISON_FUNC_GREATER;
        case DepthStencilState::COMPARISON_NOT_EQUAL:
            return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case DepthStencilState::COMPARISON_GREATER_EQUAL:
            return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case DepthStencilState::COMPARISON_ALWAYS:
            return D3D12_COMPARISON_FUNC_ALWAYS;
        default:
            return D3D12_COMPARISON_FUNC_NEVER;
        }
    }


    void TranslateBlendState(const BlendState& inState, D3D12_BLEND_DESC& outState)
    {
        outState.AlphaToCoverageEnable = inState.alphaToCoverage;
        outState.IndependentBlendEnable = true;

        for (uint32_t i = 0; i < BlendState::MAX_MRT_BLEND_COUNT; i++)
        {
            outState.RenderTarget[i].BlendEnable = inState.blendEnable[i] ? TRUE : FALSE;
            outState.RenderTarget[i].SrcBlend = convertBlendValue(inState.srcBlend[i]);
            outState.RenderTarget[i].DestBlend = convertBlendValue(inState.destBlend[i]);
            outState.RenderTarget[i].BlendOp = convertBlendOp(inState.blendOp[i]);
            outState.RenderTarget[i].SrcBlendAlpha = convertBlendValue(inState.srcBlendAlpha[i]);
            outState.RenderTarget[i].DestBlendAlpha = convertBlendValue(inState.destBlendAlpha[i]);
            outState.RenderTarget[i].BlendOpAlpha = convertBlendOp(inState.blendOpAlpha[i]);
            outState.RenderTarget[i].RenderTargetWriteMask =
                (inState.colorWriteEnable[i] & BlendState::COLOR_MASK_RED ? D3D12_COLOR_WRITE_ENABLE_RED : 0) |
                (inState.colorWriteEnable[i] & BlendState::COLOR_MASK_GREEN ? D3D12_COLOR_WRITE_ENABLE_GREEN : 0) |
                (inState.colorWriteEnable[i] & BlendState::COLOR_MASK_BLUE ? D3D12_COLOR_WRITE_ENABLE_BLUE : 0) |
                (inState.colorWriteEnable[i] & BlendState::COLOR_MASK_ALPHA ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0);
        }
    }

    void TranslateDepthStencilState(const DepthStencilState& inState, D3D12_DEPTH_STENCIL_DESC& outState)
    {
        outState.DepthEnable = inState.depthEnable ? TRUE : FALSE;
        outState.DepthWriteMask = inState.depthWriteMask == DepthStencilState::DEPTH_WRITE_MASK_ALL ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        outState.DepthFunc = convertComparisonFunc(inState.depthFunc);
        outState.StencilEnable = inState.stencilEnable ? TRUE : FALSE;
        outState.StencilReadMask = (UINT8)inState.stencilReadMask;
        outState.StencilWriteMask = (UINT8)inState.stencilWriteMask;
        outState.FrontFace.StencilFailOp = convertStencilOp(inState.frontFace.stencilFailOp);
        outState.FrontFace.StencilDepthFailOp = convertStencilOp(inState.frontFace.stencilDepthFailOp);
        outState.FrontFace.StencilPassOp = convertStencilOp(inState.frontFace.stencilPassOp);
        outState.FrontFace.StencilFunc = convertComparisonFunc(inState.frontFace.stencilFunc);
        outState.BackFace.StencilFailOp = convertStencilOp(inState.backFace.stencilFailOp);
        outState.BackFace.StencilDepthFailOp = convertStencilOp(inState.backFace.stencilDepthFailOp);
        outState.BackFace.StencilPassOp = convertStencilOp(inState.backFace.stencilPassOp);
        outState.BackFace.StencilFunc = convertComparisonFunc(inState.backFace.stencilFunc);
    }

    void TranslateRasterizerState(const RasterState& inState, D3D12_RASTERIZER_DESC& outState)
    {
        switch (inState.fillMode)
        {
        case RasterState::FILL_SOLID:
            outState.FillMode = D3D12_FILL_MODE_SOLID;
            break;
        case RasterState::FILL_LINE:
            outState.FillMode = D3D12_FILL_MODE_WIREFRAME;
            break;
        default:
            assert(!"Unknown fillMode");
        }

        switch (inState.cullMode)
        {
        case RasterState::CULL_BACK:
            outState.CullMode = D3D12_CULL_MODE_BACK;
            break;
        case RasterState::CULL_FRONT:
            outState.CullMode = D3D12_CULL_MODE_FRONT;
            break;
        case RasterState::CULL_NONE:
            outState.CullMode = D3D12_CULL_MODE_NONE;
            break;
        default:
            assert(!"Unknown cullMode");
        }

        outState.FrontCounterClockwise = inState.frontCounterClockwise ? TRUE : FALSE;
        outState.DepthBias = inState.depthBias;
        outState.DepthBiasClamp = inState.depthBiasClamp;
        outState.SlopeScaledDepthBias = inState.slopeScaledDepthBias;
        outState.DepthClipEnable = inState.depthClipEnable ? TRUE : FALSE;
        outState.MultisampleEnable = inState.multisampleEnable ? TRUE : FALSE;
        outState.AntialiasedLineEnable = inState.antialiasedLineEnable ? TRUE : FALSE;
        outState.ConservativeRaster = inState.conservativeRasterEnable ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        outState.ForcedSampleCount = inState.forcedSampleCount;
    }

    RefCountPtr<ID3D12PipelineState> Device::createPipelineState(const GraphicsPipelineDesc & state, RootSignature* pRS, const FramebufferInfo& fbinfo)
    {
        if (state.renderState.singlePassStereo.enabled && !m_SinglePassStereoSupported)
        {
            CHECK_ERROR(0, "Single-pass stereo is not supported by this device");
            return nullptr;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = pRS->handle;

        Shader* shader;
        shader = static_cast<Shader*>(state.VS.Get());
        if (shader) desc.VS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = static_cast<Shader*>(state.HS.Get());
        if (shader) desc.HS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = static_cast<Shader*>(state.DS.Get());
        if (shader) desc.DS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = static_cast<Shader*>(state.GS.Get());
        if (shader) desc.GS = { &shader->bytecode[0], shader->bytecode.size() };

        shader = static_cast<Shader*>(state.PS.Get());
        if (shader) desc.PS = { &shader->bytecode[0], shader->bytecode.size() };


        TranslateBlendState(state.renderState.blendState, desc.BlendState);
        

        const DepthStencilState& depthState = state.renderState.depthStencilState;
        TranslateDepthStencilState(depthState, desc.DepthStencilState);

        if ((depthState.depthEnable || depthState.stencilEnable) && fbinfo.depthFormat == Format::UNKNOWN)
        {
            desc.DepthStencilState.DepthEnable = FALSE;
            desc.DepthStencilState.StencilEnable = FALSE;
            OutputDebugStringA("WARNING: depthEnable or stencilEnable is true, but no depth target is bound\n");
        }

        const RasterState& rasterState = state.renderState.rasterState;
        TranslateRasterizerState(rasterState, desc.RasterizerState);

        switch (state.primType)
        {
        case PrimitiveType::POINT_LIST:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            break;
		case PrimitiveType::LINE_LIST:
			desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
			break;
        case PrimitiveType::TRIANGLE_LIST:
        case PrimitiveType::TRIANGLE_STRIP:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            break;
        case PrimitiveType::PATCH_1_CONTROL_POINT:
        case PrimitiveType::PATCH_3_CONTROL_POINT:
        case PrimitiveType::PATCH_4_CONTROL_POINT:
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            break;
        }

        desc.DSVFormat = GetFormatMapping(fbinfo.depthFormat).rtvFormat;

        desc.SampleDesc.Count = fbinfo.sampleCount;
        desc.SampleDesc.Quality = fbinfo.sampleQuality;

        for (uint32_t i = 0; i < uint32_t(fbinfo.colorFormats.size()); i++)
        {
            desc.RTVFormats[i] = GetFormatMapping(fbinfo.colorFormats[i]).rtvFormat;
        }

        InputLayout* inputLayout = static_cast<InputLayout*>(state.inputLayout.Get());
        if (inputLayout && !inputLayout->inputElements.empty())
        {
            desc.InputLayout.NumElements = uint32_t(inputLayout->inputElements.size());
            desc.InputLayout.pInputElementDescs = &(inputLayout->inputElements[0]);
        }

        desc.NumRenderTargets = uint32_t(fbinfo.colorFormats.size());
        desc.SampleMask = ~0u;

        RefCountPtr<ID3D12PipelineState> pipelineState;

#if NVRHI_D3D12_WITH_NVAPI
        std::vector<const NVAPI_D3D12_PSO_EXTENSION_DESC*> extensions;

        shader = static_cast<Shader*>(state.VS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = static_cast<Shader*>(state.HS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = static_cast<Shader*>(state.DS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = static_cast<Shader*>(state.GS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());
        shader = static_cast<Shader*>(state.PS.Get()); if (shader) extensions.insert(extensions.end(), shader->extensions.begin(), shader->extensions.end());

        if (rasterState.programmableSamplePositionsEnable || rasterState.quadFillEnable)
        {
            NVAPI_D3D12_PSO_RASTERIZER_STATE_DESC rasterizerDesc = {};
            rasterizerDesc.baseVersion = NV_PSO_EXTENSION_DESC_VER;
            rasterizerDesc.psoExtension = NV_PSO_RASTER_EXTENSION;
            rasterizerDesc.version = NV_RASTERIZER_PSO_EXTENSION_DESC_VER;

            rasterizerDesc.ProgrammableSamplePositionsEnable = rasterState.programmableSamplePositionsEnable;
            rasterizerDesc.SampleCount = rasterState.forcedSampleCount;
            memcpy(rasterizerDesc.SamplePositionsX, rasterState.samplePositionsX, sizeof(rasterState.samplePositionsX));
            memcpy(rasterizerDesc.SamplePositionsY, rasterState.samplePositionsY, sizeof(rasterState.samplePositionsY));
            rasterizerDesc.QuadFillMode = rasterState.quadFillEnable ? NVAPI_QUAD_FILLMODE_BBOX : NVAPI_QUAD_FILLMODE_DISABLED;

            extensions.push_back(&rasterizerDesc);
        }

        if (extensions.size() > 0)
        {
            NvAPI_Status status = NvAPI_D3D12_CreateGraphicsPipelineState(m_pDevice, &desc, NvU32(extensions.size()), &extensions[0], &pipelineState);

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                SIGNAL_ERROR("Failed to create a graphics pipeline state object with NVAPI extensions");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        HRESULT hr;
        hr = m_pDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipelineState));

        if (FAILED(hr))
        {
            SIGNAL_ERROR("Failed to create a graphics pipeline state object");
            return nullptr;
        }

        return pipelineState;
    }

    RefCountPtr<ID3D12PipelineState> Device::createPipelineState(const ComputePipelineDesc & state, RootSignature* pRS)
    {
        RefCountPtr<ID3D12PipelineState> pipelineState;

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};

        desc.pRootSignature = pRS->handle;
        Shader* shader = static_cast<Shader*>(state.CS.Get());
        desc.CS = { &shader->bytecode[0], shader->bytecode.size() };

#if NVRHI_D3D12_WITH_NVAPI
        if (shader->extensions.size() > 0)
        {
            NvAPI_Status status = NvAPI_D3D12_CreateComputePipelineState(m_pDevice, &desc, 
                NvU32(shader->extensions.size()), const_cast<const NVAPI_D3D12_PSO_EXTENSION_DESC**>(shader->extensions.data()), &pipelineState);

            if (status != NVAPI_OK || pipelineState == nullptr)
            {
                SIGNAL_ERROR("Failed to create a compute pipeline state object with NVAPI extensions");
                return nullptr;
            }

            return pipelineState;
        }
#endif

        HRESULT hr;
        hr = m_pDevice->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipelineState));

        if (FAILED(hr))
        {
            SIGNAL_ERROR("Failed to create a compute pipeline state object");
            return nullptr;
        }

        return pipelineState;
    }

    void Device::createCBV(size_t descriptor, IBuffer* _cbuffer)
    {
        Buffer* cbuffer = static_cast<Buffer*>(_cbuffer);

        assert(cbuffer->desc.isConstantBuffer);

        D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
        desc.BufferLocation = cbuffer->resource->GetGPUVirtualAddress();
        desc.SizeInBytes = cbuffer->desc.byteSize;
        m_pDevice->CreateConstantBufferView(&desc, { descriptor });
    }

    void Device::createTextureSRV(size_t descriptor, ITexture* _texture, const Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        subresources = subresources.resolve(textureDesc, false);

        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc = {};

        viewDesc.Format = GetFormatMapping(format == Format::UNKNOWN ? textureDesc.format : format).srvFormat;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        uint32_t planeSlice = (viewDesc.Format == DXGI_FORMAT_X24_TYPELESS_G8_UINT) ? 1 : 0;

        switch (textureDesc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1D.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture1DArray.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2D.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2D.PlaneSlice = planeSlice;
            break;
        case TextureDimension::Texture2DArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture2DArray.MipLevels = subresources.numMipLevels;
            viewDesc.Texture2DArray.PlaneSlice = planeSlice;
            break;
        case TextureDimension::TextureCube:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            viewDesc.TextureCube.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCube.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            viewDesc.TextureCubeArray.First2DArrayFace = subresources.baseArraySlice;
            viewDesc.TextureCubeArray.NumCubes = subresources.numArraySlices / 6;
            viewDesc.TextureCubeArray.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.TextureCubeArray.MipLevels = subresources.numMipLevels;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MostDetailedMip = subresources.baseMipLevel;
            viewDesc.Texture3D.MipLevels = subresources.numMipLevels;
            break;
        default:
            return;
        }

        m_pDevice->CreateShaderResourceView(texture->resource, &viewDesc, { descriptor });
    }

    void Device::createTextureUAV(size_t descriptor, ITexture* _texture, Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        subresources = subresources.resolve(textureDesc, true);

        D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc = {};

        viewDesc.Format = GetFormatMapping(format == Format::UNKNOWN ? textureDesc.format : format).srvFormat;

        switch (textureDesc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = 0;
            viewDesc.Texture3D.WSize = textureDesc.depth;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;
        default:
            return;
        }

        m_pDevice->CreateUnorderedAccessView(texture->resource, nullptr, &viewDesc, { descriptor });
    }

    void Device::createBufferSRV(size_t descriptor, IBuffer* _buffer, Format format, BufferRange range)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};

        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (!buffer)
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc.Format = mapping.srvFormat;
            m_pDevice->CreateShaderResourceView(nullptr, &desc, { descriptor });
            return;
        }

        range = range.resolve(buffer->desc);

        if (buffer->desc.structStride != 0)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.FirstElement = range.byteOffset / buffer->desc.structStride;
            desc.Buffer.NumElements = range.byteSize / buffer->desc.structStride;
            desc.Buffer.StructureByteStride = buffer->desc.structStride;
        }
        else
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc.Format = mapping.srvFormat;
            uint32_t bytesPerElement = mapping.bitsPerPixel / 8;
            desc.Buffer.FirstElement = range.byteOffset / bytesPerElement;
            desc.Buffer.NumElements = range.byteSize / bytesPerElement;
        }

        m_pDevice->CreateShaderResourceView(buffer->resource, &desc, { descriptor });
    }

    void Device::createBufferUAV(size_t descriptor, IBuffer* _buffer, Format format, BufferRange range)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};

        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

        if (!buffer)
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc.Format = mapping.srvFormat;
            m_pDevice->CreateUnorderedAccessView(nullptr, nullptr, &desc, { descriptor });
            return;
        }

        range = range.resolve(buffer->desc);

        if (buffer->desc.structStride != 0)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.FirstElement = range.byteOffset / buffer->desc.structStride;
            desc.Buffer.NumElements = range.byteSize / buffer->desc.structStride;
            desc.Buffer.StructureByteStride = buffer->desc.structStride;
        }
        else
        {
            const FormatMapping& mapping = GetFormatMapping(format == Format::UNKNOWN ? Format::R32_UINT : format);

            desc.Format = mapping.srvFormat;
            uint32_t bytesPerElement = mapping.bitsPerPixel / 8;
            desc.Buffer.FirstElement = range.byteOffset / bytesPerElement;
            desc.Buffer.NumElements = range.byteSize / bytesPerElement;
        }

        m_pDevice->CreateUnorderedAccessView(buffer->resource, nullptr, &desc, { descriptor });
    }

    void Device::createSamplerView(size_t descriptor, ISampler* _sampler)
    {
        Sampler* sampler = static_cast<Sampler*>(_sampler);

        D3D12_SAMPLER_DESC desc;
        const SamplerDesc& samplerDesc = sampler->desc;

        UINT reductionType;
        switch (samplerDesc.reductionType)
        {
        case SamplerDesc::REDUCTION_COMPARISON:
            reductionType = D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
            break;
        case SamplerDesc::REDUCTION_MINIMUM:
            reductionType = D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
            break;
        case SamplerDesc::REDUCTION_MAXIMUM:
            reductionType = D3D12_FILTER_REDUCTION_TYPE_MAXIMUM;
            break;
        default:
            reductionType = D3D12_FILTER_REDUCTION_TYPE_STANDARD;
            break;
        }

        if (samplerDesc.anisotropy > 1.0f)
        {
            desc.Filter = D3D12_ENCODE_ANISOTROPIC_FILTER(reductionType);
        }
        else
        {
            desc.Filter = D3D12_ENCODE_BASIC_FILTER(
                samplerDesc.minFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                samplerDesc.magFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                samplerDesc.mipFilter ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
                reductionType);
        }

        D3D12_TEXTURE_ADDRESS_MODE* dest[] = { &desc.AddressU, &desc.AddressV, &desc.AddressW };
        for (int i = 0; i < 3; i++)
        {
            switch (samplerDesc.wrapMode[i])
            {
            case SamplerDesc::WRAP_MODE_BORDER:
                *dest[i] = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                break;
            case SamplerDesc::WRAP_MODE_CLAMP:
                *dest[i] = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                break;
            case SamplerDesc::WRAP_MODE_WRAP:
                *dest[i] = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                break;
            }
        }

        desc.MipLODBias = samplerDesc.mipBias;
        desc.MaxAnisotropy = std::max((UINT)samplerDesc.anisotropy, 1U);
        desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
        desc.BorderColor[0] = samplerDesc.borderColor.r;
        desc.BorderColor[1] = samplerDesc.borderColor.g;
        desc.BorderColor[2] = samplerDesc.borderColor.b;
        desc.BorderColor[3] = samplerDesc.borderColor.a;
        desc.MinLOD = 0;
        desc.MaxLOD = D3D12_FLOAT32_MAX;

        m_pDevice->CreateSampler(&desc, { descriptor });
    }

    void Device::createTextureRTV(size_t descriptor, ITexture* _texture, Format format, TextureSubresourceSet subresources)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        subresources = subresources.resolve(textureDesc, true);

        D3D12_RENDER_TARGET_VIEW_DESC viewDesc = {};

        viewDesc.Format = GetFormatMapping(format == Format::UNKNOWN ? textureDesc.format : format).rtvFormat;

        switch (textureDesc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        case TextureDimension::Texture3D:
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.FirstWSlice = subresources.baseArraySlice;
            viewDesc.Texture3D.WSize = subresources.numArraySlices;
            viewDesc.Texture3D.MipSlice = subresources.baseMipLevel;
            break;
        default:
            return;
        }

        m_pDevice->CreateRenderTargetView(texture->resource, &viewDesc, { descriptor });
    }

    void Device::createTextureDSV(size_t descriptor, ITexture* _texture, TextureSubresourceSet subresources, bool isReadOnly)
    {
        Texture* texture = static_cast<Texture*>(_texture);
        const TextureDesc& textureDesc = texture->desc;

        subresources = subresources.resolve(textureDesc, true);

        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};

        viewDesc.Format = GetFormatMapping(texture->desc.format).rtvFormat;

        if (isReadOnly)
        {
            viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            if (viewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || viewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                viewDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        }

        switch (textureDesc.dimension)
        {
        case TextureDimension::Texture1D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
            viewDesc.Texture1D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture1DArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
            viewDesc.Texture1DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture1DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture1DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2D:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.ArraySize = subresources.numArraySlices;
            viewDesc.Texture2DArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DArray.MipSlice = subresources.baseMipLevel;
            break;
        case TextureDimension::Texture2DMS:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            break;
        case TextureDimension::Texture2DMSArray:
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
            viewDesc.Texture2DMSArray.FirstArraySlice = subresources.baseArraySlice;
            viewDesc.Texture2DMSArray.ArraySize = subresources.numArraySlices;
            break;
        default:
            return;
        }

        m_pDevice->CreateDepthStencilView(texture->resource, &viewDesc, { descriptor });
    }

    void Device::releaseFramebufferViews(Framebuffer* framebuffer)
    {
        for (DescriptorIndex RTV : framebuffer->RTVs)
            m_dhRTV.ReleaseDescriptor(RTV);

        if (framebuffer->DSV != INVALID_DESCRIPTOR_INDEX)
            m_dhDSV.ReleaseDescriptor(framebuffer->DSV);
    }

    void Device::releaseTextureViews(ITexture* _texture)
    {
        Texture* texture = static_cast<Texture*>(_texture);

        for (auto pair : texture->renderTargetViews)
            m_dhRTV.ReleaseDescriptor(pair.second);

        for (auto pair : texture->depthStencilViews)
            m_dhDSV.ReleaseDescriptor(pair.second);

        for (auto index : texture->clearMipLevelUAVs)
            m_dhSRVetc.ReleaseDescriptor(index);

        for (auto pair : texture->customSRVs)
            m_dhSRVetc.ReleaseDescriptor(pair.second);

        for (auto pair : texture->customUAVs)
            m_dhSRVetc.ReleaseDescriptor(pair.second);
    }

    void Device::releaseBufferViews(IBuffer* _buffer)
    {
        Buffer* buffer = static_cast<Buffer*>(_buffer);

        if (buffer->clearUAV != INVALID_DESCRIPTOR_INDEX)
        {
            m_dhSRVetc.ReleaseDescriptor(buffer->clearUAV);
            buffer->clearUAV = INVALID_DESCRIPTOR_INDEX;
        }
    }

    void Device::releaseBindingSetViews(BindingSet* bindingSet)
    {
        for (int stage = 0; stage < ARRAYSIZE(bindingSet->descriptorTablesSRVetc); stage++)
        {
            const std::shared_ptr<StageBindingLayout>& stageLayout = bindingSet->layout->stages[stage];

            if (stageLayout)
            {
                DescriptorIndex tableStartSRV = bindingSet->descriptorTablesSRVetc[stage];
                int tableSizeSRV = stageLayout->descriptorTableSizeSRVetc;

                if(tableSizeSRV > 0)
                    m_dhSRVetc.ReleaseDescriptors(tableStartSRV, tableSizeSRV);

                DescriptorIndex tableStartSamplers = bindingSet->descriptorTablesSamplers[stage];
                int tableSizeSamplers = stageLayout->descriptorTableSizeSamplers;

                if (tableSizeSRV > 0)
                    m_dhSamplers.ReleaseDescriptors(tableStartSamplers, tableSizeSamplers);
            }

        }
    }

    void Device::removeRootSignatureFromCache(RootSignature* rootsig)
    {
        if (rootsig == nullptr) return;
        m_rootsigCache[rootsig->hash] = nullptr;
    }

    D3D12_RESOURCE_DESC Device::createTextureResourceDesc(const TextureDesc& d)
    {
        const auto& formatMapping = GetFormatMapping(d.format);

        D3D12_RESOURCE_DESC desc = {};
        desc.Width = d.width;
        desc.Height = d.height;
        desc.MipLevels = UINT16(d.mipLevels);
        desc.Format = d.isTypeless ? formatMapping.resourceFormat : formatMapping.rtvFormat;
        desc.SampleDesc.Count = d.sampleCount;
        desc.SampleDesc.Quality = d.sampleQuality;

        switch (d.dimension)
        {
        case TextureDimension::Texture1D:
        case TextureDimension::Texture1DArray:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
            desc.DepthOrArraySize = UINT16(d.arraySize);
            break;
        case TextureDimension::Texture2D:
        case TextureDimension::Texture2DArray:
        case TextureDimension::TextureCube:
        case TextureDimension::TextureCubeArray:
        case TextureDimension::Texture2DMS:
        case TextureDimension::Texture2DMSArray:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.DepthOrArraySize = UINT16(d.arraySize);
            break;
        case TextureDimension::Texture3D:
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            desc.DepthOrArraySize = UINT16(d.depth);
            break;
        default:
            break;
        }

        if (d.isRenderTarget)
        {
            if (formatMapping.isDepthStencil)
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            else
                desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }

        if (d.isUAV)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        return desc;
    }

    uint8_t Device::GetFormatPlaneCount(DXGI_FORMAT format)
    {
        uint8_t& planeCount = m_DxgiFormatPlaneCounts[format];
        if (planeCount == 0)
        {
            D3D12_FEATURE_DATA_FORMAT_INFO formatInfo = { format };
            if (FAILED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &formatInfo, sizeof(formatInfo))))
            {
                // Format not supported - store a special value in the cache to avoid querying later
                planeCount = 255;
            }
            else
            {
                // Format supported - store the plane count in the cache
                planeCount = formatInfo.PlaneCount;
            }
        }

        if (planeCount == 255)
            return 0;
         
        return planeCount;
    }

    TextureHandle Device::createTexture(const TextureDesc & d)
    {
        Texture* texture = new Texture(this);
        texture->desc = d;
        
        D3D12_RESOURCE_DESC desc = createTextureResourceDesc(d);
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const auto& formatMapping = GetFormatMapping(d.format);
        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = formatMapping.rtvFormat;
        if (formatMapping.isDepthStencil)
        {
            clearValue.DepthStencil.Depth = d.clearValue.r;
            clearValue.DepthStencil.Stencil = UINT8(d.clearValue.g);
        }
        else
        {
            clearValue.Color[0] = d.clearValue.r;
            clearValue.Color[1] = d.clearValue.g;
            clearValue.Color[2] = d.clearValue.b;
            clearValue.Color[3] = d.clearValue.a;
        }

        HRESULT hr = m_pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            TranslateResourceStates(d.initialState),
            d.useClearValue ? &clearValue : nullptr,
            IID_PPV_ARGS(&texture->resource));

        CHECK_ERROR(SUCCEEDED(hr), "Failed to create a texture");

        if (FAILED(hr))
        {
            delete texture;
            return nullptr;
        }

        postCreateTextureObject(texture, desc);

        return TextureHandle::Create(texture);
    }


    TextureHandle Device::createHandleForNativeTexture(ObjectType objectType, Object _texture, const TextureDesc& desc)
    {
        if (_texture.pointer == nullptr)
            return nullptr;

        if (objectType != ObjectTypes::D3D12_Resource)
            return nullptr;

        ID3D12Resource* pResource = static_cast<ID3D12Resource*>(_texture.pointer);

        Texture* texture = new Texture(this);
        texture->resource = pResource;
        texture->desc = desc;

        D3D12_RESOURCE_DESC resourceDesc = pResource->GetDesc();

        postCreateTextureObject(texture, resourceDesc);

        return TextureHandle::Create(texture);
    }

    void Device::postCreateTextureObject(Texture* texture, const D3D12_RESOURCE_DESC& desc)
    {
        if (texture->desc.debugName)
        {
            std::string name(texture->desc.debugName);
            std::wstring wname(name.begin(), name.end());
            texture->resource->SetName(wname.c_str());
        }

        if (texture->desc.isUAV)
        {
            texture->clearMipLevelUAVs.resize(texture->desc.mipLevels);
            for (uint32_t mipLevel = 0; mipLevel < texture->desc.mipLevels; mipLevel++)
            {
                DescriptorIndex di = m_dhSRVetc.AllocateDescriptor();
                TextureSubresourceSet subresources(mipLevel, 1, 0, TextureSubresourceSet::AllArraySlices);
                createTextureUAV(m_dhSRVetc.GetCpuHandle(di).ptr, texture, Format::UNKNOWN, subresources);
                m_dhSRVetc.CopyToShaderVisibleHeap(di);
                texture->clearMipLevelUAVs[mipLevel] = di;
            }
        }

        texture->planeCount = GetFormatPlaneCount(desc.Format);
    }

    StagingTextureHandle Device::createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess)
    {
        assert(cpuAccess != CpuAccessMode::None);

        StagingTexture *ret = new StagingTexture(this);
        ret->desc = d;
        ret->resourceDesc = createTextureResourceDesc(d);
        ret->computeSubresourceOffsets(m_pDevice);

        BufferDesc bufferDesc;
        bufferDesc.byteSize = uint32_t(ret->getSizeInBytes(m_pDevice));
        bufferDesc.structStride = 0;
        bufferDesc.debugName = d.debugName;
        bufferDesc.cpuAccess = cpuAccess;

        BufferHandle buffer = createBuffer(bufferDesc);
        ret->buffer = static_cast<Buffer*>(buffer.Get());
        if (!ret->buffer)
        {
            delete ret;
            return nullptr;
        }

        ret->cpuAccess = cpuAccess;
        return StagingTextureHandle::Create(ret);
    }

    void *Device::mapStagingTexture(IStagingTexture* _tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch)
    {
        StagingTexture* tex = static_cast<StagingTexture*>(_tex);

        assert(slice.x == 0);
        assert(slice.y == 0);
        assert(cpuAccess != CpuAccessMode::None);
        assert(tex->mappedRegion.size == 0);
        assert(tex->mappedAccess == CpuAccessMode::None);

        auto resolvedSlice = slice.resolve(tex->desc);
        auto region = tex->getSliceRegion(m_pDevice, resolvedSlice);

        if (tex->lastUseFence)
        {
            WaitForFence(tex->lastUseFence, tex->lastUseFenceValue, m_fenceEvent);
            tex->lastUseFence = nullptr;
        }

        D3D12_RANGE range;

        if (cpuAccess == CpuAccessMode::Read)
        {
            range = { SIZE_T(region.offset), region.offset + region.size };
        } else {
            range = { 0, 0 };
        }

        uint8_t *ret;
        CHECK_ERROR(SUCCEEDED(tex->buffer->resource->Map(0, &range, (void **)&ret)), "mapBuffer failed");

        tex->mappedRegion = region;
        tex->mappedAccess = cpuAccess;

        *outRowPitch = region.footprint.Footprint.RowPitch;
        return ret + tex->mappedRegion.offset;
    }

    void Device::unmapStagingTexture(IStagingTexture* _tex)
    {
        StagingTexture* tex = static_cast<StagingTexture*>(_tex);

        assert(tex->mappedRegion.size != 0);
        assert(tex->mappedAccess != CpuAccessMode::None);

        D3D12_RANGE range;

        if (tex->mappedAccess == CpuAccessMode::Write)
        {
            range = { SIZE_T(tex->mappedRegion.offset), tex->mappedRegion.offset + tex->mappedRegion.size };
        } else {
            range = { 0, 0 };
        }

        tex->buffer->resource->Unmap(0, &range);

        tex->mappedRegion.size = 0;
        tex->mappedAccess = CpuAccessMode::None;
    }

    BufferHandle Device::createBuffer(const BufferDesc & d)
    {
        Buffer* buffer = new Buffer(this);
        buffer->desc = d;
        buffer->parent = this;

        if (buffer->desc.isConstantBuffer)
        {
            buffer->desc.byteSize += 256 - (buffer->desc.byteSize % 256);
        }

        if (d.isVolatile)
        {
            CHECK_ERROR(!d.canHaveUAVs, "Volatile buffers can't have UAVs");
            // CHECK_ERROR(d.cpuAccess == CpuAccess::None, 
            //    "Volatile buffers cannot have any CpuAccess, they're only writable through writeBuffer");

            // Do not create any resources for volatile buffers. Done.
            return BufferHandle::Create(buffer);
        }

        D3D12_RESOURCE_DESC desc = {};
        desc.Width = buffer->desc.byteSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (buffer->desc.canHaveUAVs)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12_HEAP_PROPERTIES heapProps = {};
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

        switch(buffer->desc.cpuAccess)
        {
            case CpuAccessMode::None:
                heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
                initialState = TranslateResourceStates(d.initialState);
                break;

            case CpuAccessMode::Read:
                heapProps.Type = D3D12_HEAP_TYPE_READBACK;
                initialState = D3D12_RESOURCE_STATE_COPY_DEST;
                break;

            case CpuAccessMode::Write:
                heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
                initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
                break;
        }

        HRESULT hr = m_pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&buffer->resource));

        CHECK_ERROR(SUCCEEDED(hr), "Failed to create a buffer");

        if (FAILED(hr))
        {
            delete buffer;
            return nullptr;
        }

        postCreateBufferObject(buffer);

        return BufferHandle::Create(buffer);
    }

    void Device::postCreateBufferObject(Buffer* buffer)
    {
        buffer->gpuVA = buffer->resource->GetGPUVirtualAddress();

        if (buffer->desc.debugName)
        {
            std::string name(buffer->desc.debugName);
            std::wstring wname(name.begin(), name.end());
            buffer->resource->SetName(wname.c_str());
        }

        if (buffer->desc.canHaveUAVs)
        {
            buffer->clearUAV = m_dhSRVetc.AllocateDescriptor();

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Format = DXGI_FORMAT_R32_UINT;
            uavDesc.Buffer.NumElements = buffer->desc.byteSize / sizeof(uint32_t);

            m_pDevice->CreateUnorderedAccessView(buffer->resource, nullptr, &uavDesc, m_dhSRVetc.GetCpuHandle(buffer->clearUAV));
            m_dhSRVetc.CopyToShaderVisibleHeap(buffer->clearUAV);
        }
    }

    void *Device::mapBuffer(IBuffer* _b, CpuAccessMode flags)
    {
        Buffer* b = static_cast<Buffer*>(_b);

        if (b->lastUseFence)
        {
            WaitForFence(b->lastUseFence, b->lastUseFenceValue, m_fenceEvent);
            b->lastUseFence = nullptr;
        }

        D3D12_RANGE range;

        if (flags == CpuAccessMode::Read)
        {
            range = { 0, b->desc.byteSize };
        } else {
            range = { 0, 0 };
        }

        void *ret;
        CHECK_ERROR(SUCCEEDED(b->resource->Map(0, &range, &ret)), "mapBuffer failed");
        return ret;
    }

    void Device::unmapBuffer(IBuffer* _b)
    {
        Buffer* b = static_cast<Buffer*>(_b);

        b->resource->Unmap(0, nullptr);
    }

    nvrhi::BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc)
    {
        if (_buffer.pointer == nullptr)
            return nullptr;

        if (objectType != ObjectTypes::D3D12_Resource)
            return nullptr;

        ID3D12Resource* pResource = static_cast<ID3D12Resource*>(_buffer.pointer);

        Buffer* buffer = new Buffer(this);
        buffer->resource = pResource;
        buffer->desc = desc;

        D3D12_RESOURCE_DESC resourceDesc = pResource->GetDesc();

        postCreateBufferObject(buffer);

        return BufferHandle::Create(buffer);
    }

    ShaderHandle Device::createShader(const ShaderDesc & d, const void * binary, const size_t binarySize)
    {
        if (binarySize == 0)
            return nullptr;

        Shader* shader = new Shader();
        shader->bytecode.resize(binarySize);
        shader->desc = d;
        memcpy(&shader->bytecode[0], binary, binarySize);

#if NVRHI_D3D12_WITH_NVAPI
        // Save the custom semantics structure because it may be on the stack or otherwise dynamic.
        // Note that this has to be a deep copy; currently NV_CUSTOM_SEMANTIC has no pointers, but that might change.
        if (d.numCustomSemantics && d.pCustomSemantics)
        {
            shader->customSemantics.resize(d.numCustomSemantics);
            memcpy(&shader->customSemantics[0], d.pCustomSemantics, sizeof(NV_CUSTOM_SEMANTIC) * d.numCustomSemantics);
        }

        // Save the coordinate swizzling patterns for the same reason
        if (d.pCoordinateSwizzling)
        {
            const uint32_t numSwizzles = 16;
            shader->coordinateSwizzling.resize(numSwizzles);
            memcpy(&shader->coordinateSwizzling[0], d.pCoordinateSwizzling, sizeof(uint32_t) * numSwizzles);
        }

        if (d.hlslExtensionsUAV >= 0)
        {
            NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC* pExtn = new NVAPI_D3D12_PSO_SET_SHADER_EXTENSION_SLOT_DESC();
            memset(pExtn, 0, sizeof(*pExtn));
            pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
            pExtn->psoExtension = NV_PSO_SET_SHADER_EXTNENSION_SLOT_AND_SPACE;
            pExtn->version = NV_SET_SHADER_EXTENSION_SLOT_DESC_VER;

            pExtn->uavSlot = d.hlslExtensionsUAV;
            pExtn->registerSpace = 0;

            shader->extensions.push_back(pExtn);
        }

        switch (d.shaderType)
        {
        case ShaderType::SHADER_VERTEX:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_VERTEX_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_VERTEX_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_VERTEX_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::SHADER_HULL:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_HULL_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_HULL_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_HULL_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::SHADER_DOMAIN:
            if (d.numCustomSemantics)
            {
                NVAPI_D3D12_PSO_DOMAIN_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_DOMAIN_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_VERTEX_SHADER_EXTENSION;
                pExtn->version = NV_DOMAIN_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = &shader->customSemantics[0];
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;

                shader->extensions.push_back(pExtn);
            }
            break;
        case ShaderType::SHADER_GEOMETRY:
            if ((d.fastGSFlags & FastGeometryShaderFlags::COMPATIBILITY_MODE) && (d.fastGSFlags & FastGeometryShaderFlags::FORCE_FAST_GS))
            {
                CHECK_ERROR(d.numCustomSemantics == 0, "Compatibility mode FastGS does not support custom semantics");

                NVAPI_D3D12_PSO_CREATE_FASTGS_EXPLICIT_DESC* pExtn = new NVAPI_D3D12_PSO_CREATE_FASTGS_EXPLICIT_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_EXPLICIT_FASTGS_EXTENSION;
                pExtn->version = NV_FASTGS_EXPLICIT_PSO_EXTENSION_VER;

                if (d.pCoordinateSwizzling)
                    pExtn->pCoordinateSwizzling = &shader->coordinateSwizzling[0];

                pExtn->flags = 0;

                if (d.fastGSFlags & FastGeometryShaderFlags::USE_VIEWPORT_MASK)
                    pExtn->flags |= NV_FASTGS_USE_VIEWPORT_MASK;

                if (d.fastGSFlags & FastGeometryShaderFlags::OFFSET_RT_INDEX_BY_VP_INDEX)
                    pExtn->flags |= NV_FASTGS_OFFSET_RT_INDEX_BY_VP_INDEX;

                if (d.fastGSFlags & FastGeometryShaderFlags::STRICT_API_ORDER)
                    pExtn->flags |= NV_FASTGS_STRICT_API_ORDER;

                shader->extensions.push_back(pExtn);
            }
            else if ((d.fastGSFlags & FastGeometryShaderFlags::FORCE_FAST_GS) || d.numCustomSemantics || d.pCoordinateSwizzling)
            {
                NVAPI_D3D12_PSO_GEOMETRY_SHADER_DESC* pExtn = new NVAPI_D3D12_PSO_GEOMETRY_SHADER_DESC();
                memset(pExtn, 0, sizeof(*pExtn));
                pExtn->baseVersion = NV_PSO_EXTENSION_DESC_VER;
                pExtn->psoExtension = NV_PSO_GEOMETRY_SHADER_EXTENSION;
                pExtn->version = NV_GEOMETRY_SHADER_PSO_EXTENSION_DESC_VER;

                pExtn->NumCustomSemantics = d.numCustomSemantics;
                pExtn->pCustomSemantics = d.numCustomSemantics ? &shader->customSemantics[0] : nullptr;
                pExtn->UseCoordinateSwizzle = d.pCoordinateSwizzling != nullptr;
                pExtn->pCoordinateSwizzling = d.pCoordinateSwizzling != nullptr ? &shader->coordinateSwizzling[0] : nullptr;
                pExtn->ForceFastGS = (d.fastGSFlags & FastGeometryShaderFlags::FORCE_FAST_GS) != 0;
                pExtn->UseViewportMask = (d.fastGSFlags & FastGeometryShaderFlags::USE_VIEWPORT_MASK) != 0;
                pExtn->OffsetRtIndexByVpIndex = (d.fastGSFlags & FastGeometryShaderFlags::OFFSET_RT_INDEX_BY_VP_INDEX) != 0;
                pExtn->DontUseViewportOrder = (d.fastGSFlags & FastGeometryShaderFlags::STRICT_API_ORDER) != 0;
                pExtn->UseSpecificShaderExt = d.useSpecificShaderExt;
                pExtn->UseAttributeSkipMask = false;

                shader->extensions.push_back(pExtn);
            }
            break;
        }
#else
        if (d.numCustomSemantics || d.pCoordinateSwizzling || d.fastGSFlags || d.hlslExtensionsUAV >= 0)
        {
            delete shader;

            // NVAPI is unavailable
            return nullptr;
        }
#endif

#if ENABLE_D3D_REFLECTION
        RefCountPtr<ID3D11ShaderReflection> pReflector;
        HRESULT hr = D3DReflect(binary, binarySize, IID_PPV_ARGS(&pReflector));
        
        if (SUCCEEDED(hr) && pReflector)
        {
            D3D11_SHADER_DESC desc;
            pReflector->GetDesc(&desc);

            for (uint32_t i = 0; i < desc.BoundResources; i++)
            {
                D3D11_SHADER_INPUT_BIND_DESC bindingDesc;
                hr = pReflector->GetResourceBindingDesc(i, &bindingDesc);
                if (SUCCEEDED(hr))
                {
                    switch (bindingDesc.Type)
                    {
                    case D3D_SIT_CBUFFER:
                        shader->slotsCB.set(bindingDesc.BindPoint);
                        break;

                    case D3D_SIT_TBUFFER:
                    case D3D_SIT_TEXTURE:
                    case D3D_SIT_STRUCTURED:
                    case D3D_SIT_BYTEADDRESS:
                        shader->slotsSRV.set(bindingDesc.BindPoint);
                        break;

                    case D3D_SIT_SAMPLER:
                        shader->slotsSampler.set(bindingDesc.BindPoint);
                        break;

                    case D3D_SIT_UAV_RWTYPED:
                    case D3D_SIT_UAV_RWSTRUCTURED:
                    case D3D_SIT_UAV_RWBYTEADDRESS:
                    case D3D_SIT_UAV_APPEND_STRUCTURED:
                    case D3D_SIT_UAV_CONSUME_STRUCTURED:
                    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                        shader->slotsUAV.set(bindingDesc.BindPoint);
                        break;
                    }
                }
            }
        }
#endif

        return ShaderHandle::Create(shader);
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

    nvrhi::ShaderLibraryHandle Device::createShaderLibrary(const void* binary, const size_t binarySize)
    {
        ShaderLibrary* shaderLibrary = new ShaderLibrary();

        shaderLibrary->bytecode.resize(binarySize);
        memcpy(&shaderLibrary->bytecode[0], binary, binarySize);

        return ShaderLibraryHandle::Create(shaderLibrary);
    }

    nvrhi::ShaderLibraryHandle Device::createShaderLibraryPermutation(const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound /*= true*/)
    {
        const void* binary = nullptr;
        size_t binarySize = 0;

        if (FindPermutationInBlob(blob, blobSize, constants, numConstants, &binary, &binarySize))
        {
            return createShaderLibrary(binary, binarySize);
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

    SamplerHandle Device::createSampler(const SamplerDesc & d)
    {
        Sampler* sampler = new Sampler(this);
        sampler->desc = d;
        sampler->parent = this;
        return SamplerHandle::Create(sampler);
    }
    
    InputLayoutHandle Device::createInputLayout(const VertexAttributeDesc * d, uint32_t attributeCount, IShader* vertexShader)
    {
        // The shader is not needed here, there are no separate IL objects in DX12
        (void)vertexShader;

        InputLayout* layout = new InputLayout(this);
        layout->attributes.resize(attributeCount);

        for (uint32_t index = 0; index < attributeCount; index++)
        {
            VertexAttributeDesc& attr = layout->attributes[index];

            // Copy the description to get a stable name pointer in desc
            attr = d[index];

            assert(attr.arraySize > 0);

            const FormatMapping& formatMapping = GetFormatMapping(attr.format);

            for (uint32_t semanticIndex = 0; semanticIndex < attr.arraySize; semanticIndex++)
            {
                D3D12_INPUT_ELEMENT_DESC desc;

                desc.SemanticName = attr.name;
                desc.AlignedByteOffset = attr.offset + semanticIndex * (formatMapping.bitsPerPixel / 8);
                desc.Format = formatMapping.srvFormat;
                desc.InputSlot = attr.bufferIndex;
                desc.SemanticIndex = semanticIndex;

                if (attr.isInstanced)
                {
                    desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                    desc.InstanceDataStepRate = 1;
                }
                else
                {
                    desc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                    desc.InstanceDataStepRate = 0;
                }

                layout->inputElements.push_back(desc);
            }

            if (layout->elementStrides.find(attr.bufferIndex) == layout->elementStrides.end())
            {
                layout->elementStrides[attr.bufferIndex] = attr.elementStride;
            } else {
                assert(layout->elementStrides[attr.bufferIndex] == attr.elementStride);
            }
        }

        return InputLayoutHandle::Create(layout);
    }
    
    EventQueryHandle Device::createEventQuery(void)
    {
        EventQuery *ret = new EventQuery(this);
        return EventQueryHandle::Create(ret);
    }

    void Device::setEventQuery(IEventQuery* _query)
    {
        EventQuery* query = static_cast<EventQuery*>(_query);

        query->started = true;

        if (m_CommandListsInFlight.empty())
        {
            query->resolved = true;
            return;
        }

        std::shared_ptr<CommandListInstance> lastCommandList = m_CommandListsInFlight.back();
        query->fence = lastCommandList->fence;
        query->fenceCounter = lastCommandList->instanceID;
        query->resolved = false;
    }

    bool Device::pollEventQuery(IEventQuery* _query)
    {
        EventQuery* query = static_cast<EventQuery*>(_query);

        if (!query->started)
            return false;

        if (query->resolved)
            return true;

        CHECK_ERROR(query->fence, "An unresolved event query must have a fence");
        
        if (query->fence->GetCompletedValue() >= query->fenceCounter)
        {
            query->resolved = true;
            query->fence = nullptr;
        }

        return query->resolved;
    }

    void Device::waitEventQuery(IEventQuery* _query)
    {
        EventQuery* query = static_cast<EventQuery*>(_query);

        if (!query->started || query->resolved)
            return;

        CHECK_ERROR(query->fence, "An unresolved event query must have a fence");

        WaitForFence(query->fence, query->fenceCounter, m_fenceEvent);
    }

    void Device::resetEventQuery(IEventQuery* _query)
    {
        EventQuery* query = static_cast<EventQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->fence = nullptr;
    }

    TimerQueryHandle Device::createTimerQuery(void)
    {
        uint32_t querySlotBegin = allocateTimerQuerySlot();
        uint32_t querySlotEnd = allocateTimerQuerySlot();

        if (querySlotBegin == uint32_t(-1) || querySlotEnd == uint32_t(-1))
        {
            return nullptr;
        }

        TimerQuery* query = new TimerQuery(this);
        query->beginQueryIndex = querySlotBegin;
        query->endQueryIndex = querySlotEnd;
        query->resolved = false;
        query->time = 0.f;

        return TimerQueryHandle::Create(query);
    }

    bool Device::pollTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = static_cast<TimerQuery*>(_query);

        if (!query->started)
            return false;

        if (!query->fence)
            return true;

        if (query->fence->GetCompletedValue() >= query->fenceCounter)
        {
            query->fence = nullptr;
            return true;
        }

        return false;
    }

    float Device::getTimerQueryTime(ITimerQuery* _query)
    {
        TimerQuery* query = static_cast<TimerQuery*>(_query);

        if (!query->resolved)
        {
            if (query->fence)
            {
                WaitForFence(query->fence, query->fenceCounter, m_fenceEvent);
                query->fence = nullptr;
            }

            uint64_t frequency;
            m_pCommandQueue->GetTimestampFrequency(&frequency);

            D3D12_RANGE bufferReadRange = { query->beginQueryIndex * sizeof(uint64_t), (query->beginQueryIndex + 2) * sizeof(uint64_t) };
            volatile uint64_t *data;
            CHECK_ERROR(SUCCEEDED(m_timerQueryResolveBuffer->resource->Map(0, &bufferReadRange, (void**)&data)), "getTimerQueryTime: Map() failed");

            query->resolved = true;
            query->time = float(double(data[query->endQueryIndex] - data[query->beginQueryIndex]) / double(frequency));

            m_timerQueryResolveBuffer->resource->Unmap(0, nullptr);
        }

        return query->time;
    }

    void Device::resetTimerQuery(ITimerQuery* _query)
    {
        TimerQuery* query = static_cast<TimerQuery*>(_query);

        query->started = false;
        query->resolved = false;
        query->time = 0.f;
        query->fence = nullptr;
    }


    GraphicsAPI Device::getGraphicsAPI()
    {
        return GraphicsAPI::D3D12;
    }


    Object Device::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Device:
            return Object(m_pDevice);
        case ObjectTypes::D3D12_CommandQueue:
            return Object(m_pCommandQueue);
        case ObjectTypes::Nvrhi_D3D12_Device:
            return Object(this);
        default:
            return nullptr;
        }
    }

    FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc)
    {
        Framebuffer *fb = new Framebuffer(this);
        fb->desc = desc;
        fb->framebufferInfo = FramebufferInfo(desc);

        if (desc.colorAttachments.size())
        {
            Texture* texture = static_cast<Texture*>(desc.colorAttachments[0].texture);
            fb->rtWidth = texture->desc.width;
            fb->rtHeight = texture->desc.height;
        } else if (desc.depthAttachment.valid())
        {
            Texture* texture = static_cast<Texture*>(desc.depthAttachment.texture);
            fb->rtWidth = texture->desc.width;
            fb->rtHeight = texture->desc.height;
        }

        for (size_t rt = 0; rt < desc.colorAttachments.size(); rt++)
        {
            auto& attachment = desc.colorAttachments[rt];

            Texture* texture = static_cast<Texture*>(attachment.texture);
            assert(texture->desc.width == fb->rtWidth);
            assert(texture->desc.height == fb->rtHeight);

            DescriptorIndex index = m_dhRTV.AllocateDescriptor();

            createTextureRTV(m_dhRTV.GetCpuHandle(index).ptr, texture, attachment.format, attachment.subresources);

            fb->RTVs.push_back(index);
            fb->textures.push_back(texture);
        }

        if (desc.depthAttachment.valid())
        {
            Texture* texture = static_cast<Texture*>(desc.depthAttachment.texture);
            assert(texture->desc.width == fb->rtWidth);
            assert(texture->desc.height == fb->rtHeight);

            DescriptorIndex index = m_dhDSV.AllocateDescriptor();

            createTextureDSV(m_dhDSV.GetCpuHandle(index).ptr, texture, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly);

            fb->DSV = index;
            fb->textures.push_back(texture);
        }

        return FramebufferHandle::Create(fb);
    }

    DX12_ViewportState convertViewportState(const GraphicsPipeline *pso, const ViewportState& vpState)
    {
        DX12_ViewportState ret;

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
            if (pso->desc.renderState.rasterState.scissorEnable)
            {
                ret.scissorRects[rt].left = (LONG)vpState.scissorRects[rt].minX;
                ret.scissorRects[rt].top = (LONG)vpState.scissorRects[rt].minY;
                ret.scissorRects[rt].right = (LONG)vpState.scissorRects[rt].maxX;
                ret.scissorRects[rt].bottom = (LONG)vpState.scissorRects[rt].maxY;
            }
            else
            {
                ret.scissorRects[rt].left = (LONG)vpState.viewports[rt].minX;
                ret.scissorRects[rt].top = (LONG)vpState.viewports[rt].minY;
                ret.scissorRects[rt].right = (LONG)vpState.viewports[rt].maxX;
                ret.scissorRects[rt].bottom = (LONG)vpState.viewports[rt].maxY;

                if (pso->framebufferInfo.width > 0)
                {
                    ret.scissorRects[rt].left = std::max(ret.scissorRects[rt].left, LONG(0));
                    ret.scissorRects[rt].top = std::max(ret.scissorRects[rt].top, LONG(0));
                    ret.scissorRects[rt].right = std::min(ret.scissorRects[rt].right, LONG(pso->framebufferInfo.width));
                    ret.scissorRects[rt].bottom = std::min(ret.scissorRects[rt].bottom, LONG(pso->framebufferInfo.height));
                }
            }
        }

        return ret;
    }
    
    GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb)
    {
        RefCountPtr<RootSignature> pRS = getRootSignature(desc.bindingLayouts, desc.inputLayout != nullptr);

        RefCountPtr<ID3D12PipelineState> pPSO = createPipelineState(desc, pRS, fb->GetFramebufferInfo());

        return createHandleForNativeGraphicsPipeline(pRS, pPSO, desc, fb->GetFramebufferInfo());
    }

    bool IsBlendFactorRequired(BlendState::BlendValue value)
    {
        return value == BlendState::BLEND_BLEND_FACTOR || value == BlendState::BLEND_INV_BLEND_FACTOR;
    }

    nvrhi::GraphicsPipelineHandle Device::createHandleForNativeGraphicsPipeline(IRootSignature* rootSignature, ID3D12PipelineState* pipelineState, const GraphicsPipelineDesc& desc, const FramebufferInfo& framebufferInfo)
    {
        if (rootSignature == nullptr)
            return nullptr;

        if (pipelineState == nullptr)
            return nullptr;

        GraphicsPipeline *pso = new GraphicsPipeline(this);
        pso->desc = desc;
        pso->framebufferInfo = framebufferInfo;
        pso->rootSignature = static_cast<RootSignature*>(rootSignature);
        pso->pipelineState = pipelineState;

        pso->viewportState = convertViewportState(pso, desc.renderState.viewportState);

        for (size_t index = 0; index < pso->framebufferInfo.colorFormats.size(); index++)
        {
            if (IsBlendFactorRequired(desc.renderState.blendState.srcBlend[index]) ||
                IsBlendFactorRequired(desc.renderState.blendState.destBlend[index]) ||
                IsBlendFactorRequired(desc.renderState.blendState.srcBlendAlpha[index]) ||
                IsBlendFactorRequired(desc.renderState.blendState.destBlendAlpha[index]))
            {
                pso->requiresBlendFactors = true;
            }
        }

        return GraphicsPipelineHandle::Create(pso);
    }

    void Device::createResourceBindingsForStage(BindingSet* bindingSet, ShaderType::Enum stage, const StageBindingLayout* layout, const StageBindingSetDesc& bindings)
    {
        if (layout == nullptr && bindings.size() != 0)
            SIGNAL_ERROR("Attempted binding to empty layout");

        if (layout != nullptr && bindings.size() == 0)
            SIGNAL_ERROR("No bindings for an existing layout");

        if (layout == nullptr || bindings.size() == 0)
            return;

        // TODO: verify that the entire layout is covered by the binding set

        // Process the volatile constant buffers: they occupy one root parameter each
        for (const std::pair<RootParameterIndex, D3D12_ROOT_DESCRIPTOR1>& parameter : layout->rootParametersVolatileCB)
        {
            IBuffer* foundBuffer = nullptr;

            RootParameterIndex rootParameterIndex = parameter.first;
            const D3D12_ROOT_DESCRIPTOR1& rootDescriptor = parameter.second;

            for (const auto& binding : bindings)
            {
                if (binding.type == ResourceType::VolatileConstantBuffer && binding.slot == rootDescriptor.ShaderRegister)
                {
                    Buffer* buffer = static_cast<Buffer*>(binding.resourceHandle);
                    bindingSet->resources.push_back(buffer);

                    // It's legal (although wasteful) to bind non-volatile CBs to root parameters. Not the other way around.
                    if (!buffer->desc.isVolatile)
                    {
                        if (buffer->IsPermanent())
                        {
                            D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                            CHECK_ERROR((buffer->permanentState & requiredBits) == requiredBits, "Permanent buffer has incorrect state");
                        }
                        else
                        {
                            bindingSet->barrierSetup.push_back([this, buffer](CommandList* commandList, IBuffer* indirectParamsBuffer, bool& indirectParamsTransitioned) 
                            {
                                D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                                if (indirectParamsBuffer == buffer)
                                {
                                    state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                                    indirectParamsTransitioned = true;
                                }

                                commandList->requireBufferState(buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                            });
                        }
                    }
                    
                    foundBuffer = buffer;
                    break;
                }
            }

            // Add an entry to the binding set's array, whether we found the buffer in the binding set or not.
            // Even if not found, the command list still has to bind something to the root parameter.
            bindingSet->rootParametersVolatileCB[stage].push_back(std::make_pair(rootParameterIndex, foundBuffer));
        }

        if (layout->descriptorTableSizeSamplers > 0)
        {
            DescriptorIndex descriptorTableBaseIndex = m_dhSamplers.AllocateDescriptors(layout->descriptorTableSizeSamplers);
            bindingSet->descriptorTablesSamplers[stage] = descriptorTableBaseIndex;
            bindingSet->rootParameterIndicesSamplers[stage] = layout->rootParameterSamplers;
            bindingSet->descriptorTablesValidSamplers[stage] = true;

            for (const auto& range : layout->descriptorRangesSamplers)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; itemInRange++)
                {
                    uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_dhSamplers.GetCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    for (const auto& binding : bindings)
                    {
                        if (binding.type == ResourceType::Sampler && binding.slot == slot)
                        {
                            Sampler* sampler = static_cast<Sampler*>(binding.resourceHandle);
                            bindingSet->resources.push_back(sampler);

                            createSamplerView(descriptorHandle.ptr, static_cast<ISampler*>(binding.resourceHandle));
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        // Create a default sampler
                        D3D12_SAMPLER_DESC samplerDesc = {};
                        m_pDevice->CreateSampler(&samplerDesc, descriptorHandle);
                    }
                }
            }

            m_dhSamplers.CopyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSamplers);
        }

        if (layout->descriptorTableSizeSRVetc > 0)
        {
            DescriptorIndex descriptorTableBaseIndex = m_dhSRVetc.AllocateDescriptors(layout->descriptorTableSizeSRVetc);
            bindingSet->descriptorTablesSRVetc[stage] = descriptorTableBaseIndex;
            bindingSet->rootParameterIndicesSRVetc[stage] = layout->rootParameterSRVetc;
            bindingSet->descriptorTablesValidSRVetc[stage] = true;

            for (const auto& range : layout->descriptorRangesSRVetc)
            {
                for (uint32_t itemInRange = 0; itemInRange < range.NumDescriptors; itemInRange++)
                {
                    uint32_t slot = range.BaseShaderRegister + itemInRange;
                    bool found = false;
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptorHandle = m_dhSRVetc.GetCpuHandle(
                        descriptorTableBaseIndex + range.OffsetInDescriptorsFromTableStart + itemInRange);

                    IResource* pResource = nullptr;

                    for (const auto& binding : bindings)
                    {
                        if (binding.slot != slot)
                            continue;

                        const auto bindingType = GetNormalizedResourceType(binding.type);

                        if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::Buffer_SRV)
                        {
                            Buffer* buffer = static_cast<Buffer*>(binding.resourceHandle);

                            createBufferSRV(descriptorHandle.ptr, buffer, binding.format, binding.range);
                            pResource = buffer;

                            if (buffer)
                            {
                                if (buffer->IsPermanent())
                                {
                                    D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                                    CHECK_ERROR((buffer->permanentState & requiredBits) == requiredBits, "Permanent buffer has incorrect state");
                                }
                                else
                                {
                                    bindingSet->barrierSetup.push_back([this, buffer](CommandList* commandList, IBuffer* indirectParamsBuffer, bool& indirectParamsTransitioned)
                                    {
                                        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                                        if (indirectParamsBuffer == buffer)
                                        {
                                            state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                                            indirectParamsTransitioned = true;
                                        }

                                        commandList->requireBufferState(buffer, state);
                                    });
                                }
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == ResourceType::Buffer_UAV)
                        {
                            Buffer* buffer = static_cast<Buffer*>(binding.resourceHandle);

                            createBufferUAV(descriptorHandle.ptr, buffer, binding.format, binding.range);
                            pResource = buffer;

                            if (buffer)
                            {
                                if (buffer->IsPermanent())
                                {
                                    D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                                    CHECK_ERROR((buffer->permanentState & requiredBits) == requiredBits, "Permanent buffer has incorrect state");
                                }
                                else
                                {
                                    bindingSet->barrierSetup.push_back([this, buffer](CommandList* commandList, IBuffer* indirectParamsBuffer, bool& indirectParamsTransitioned)
                                    {
                                        if (indirectParamsBuffer == buffer)
                                        {
                                            message(MessageSeverity::Error, "Same buffer bound as a UAV and as drawIndirect/dispatchIndirect arguments, which is invalid");
                                            indirectParamsTransitioned = true;
                                        }

                                        commandList->requireBufferState(buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                    });
                                }
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::Texture_SRV)
                        {
                            Texture* texture = static_cast<Texture*>(binding.resourceHandle);

                            TextureSubresourceSet subresources = binding.subresources;

                            createTextureSRV(descriptorHandle.ptr, texture, binding.format, subresources);
                            pResource = texture;

                            if (texture->IsPermanent())
                            {
                                D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                                CHECK_ERROR((texture->permanentState & requiredBits) == requiredBits, "Permanent texture has incorrect state");
                            }
                            else
                            {
                                bindingSet->barrierSetup.push_back([this, texture, subresources](CommandList* commandList, IBuffer*, bool&) {
                                    commandList->requireTextureState(texture, subresources, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                                });
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV && bindingType == ResourceType::Texture_UAV)
                        {
                            Texture* texture = static_cast<Texture*>(binding.resourceHandle);

                            TextureSubresourceSet subresources = binding.subresources;

                            createTextureUAV(descriptorHandle.ptr, texture, binding.format, subresources);
                            pResource = texture;

                            if (texture->IsPermanent())
                            {
                                D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                                CHECK_ERROR((texture->permanentState & requiredBits) == requiredBits, "Permanent texture has incorrect state");
                            }
                            else
                            {
                                bindingSet->barrierSetup.push_back([this, texture, subresources](CommandList* commandList, IBuffer*, bool&) {
                                    commandList->requireTextureState(texture, subresources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                                });
                            }

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SRV && bindingType == ResourceType::RayTracingAccelStruct)
                        {
#if NVRHI_WITH_DXR
                            dxr::AccelStruct* as = static_cast<dxr::AccelStruct*>(binding.resourceHandle);

                            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
                            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            srvDesc.RaytracingAccelerationStructure.Location = as->dataBuffer->gpuVA;

                            m_pDevice->CreateShaderResourceView(nullptr, &srvDesc, descriptorHandle);

                            pResource = as;
#else
                            assert(!"DXR is not supported in this build");
#endif

                            found = true;
                            break;
                        }
                        else if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_CBV && bindingType == ResourceType::ConstantBuffer)
                        {
                            Buffer* buffer = static_cast<Buffer*>(binding.resourceHandle);

                            createCBV(descriptorHandle.ptr, buffer);
                            pResource = buffer;

                            if(buffer->desc.isVolatile)
                            { 
                                CHECK_ERROR(false, "Attempted to bind a volatile constant buffer to a non-volatile CB layout.");
                                found = false;
                                break;
                            }

                            if (buffer->IsPermanent())
                            {
                                D3D12_RESOURCE_STATES requiredBits = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                                CHECK_ERROR((buffer->permanentState & requiredBits) == requiredBits, "Permanent buffer has incorrect state");
                            }
                            else
                            {
                                bindingSet->barrierSetup.push_back([this, buffer](CommandList* commandList, IBuffer* indirectParamsBuffer, bool& indirectParamsTransitioned) 
                                {

                                    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
                                    if (indirectParamsBuffer == buffer)
                                    {
                                        state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
                                        indirectParamsTransitioned = true;
                                    }

                                    commandList->requireBufferState(buffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                                });
                            }

                            found = true;
                            break;
                        }
                    }

                    if (pResource)
                    {
                        bindingSet->resources.push_back(pResource);
                    }

                    if (!found)
                    {
                        // Create a null SRV, UAV, or CBV

                        switch (range.RangeType)
                        {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: {
                            D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
                            descSRV.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                            descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            descSRV.Format = DXGI_FORMAT_R32_UINT;

                            m_pDevice->CreateShaderResourceView(nullptr, &descSRV, descriptorHandle);

                            break;
                        }
                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: {
                            D3D12_UNORDERED_ACCESS_VIEW_DESC descUAV = {};
                            descUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                            descUAV.Format = DXGI_FORMAT_R32_UINT;

                            m_pDevice->CreateUnorderedAccessView(nullptr, nullptr, &descUAV, descriptorHandle);

                            break;
                        }

                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: {
                            m_pDevice->CreateConstantBufferView(nullptr, descriptorHandle);

                            break;
                        }
                        }
                    }
                }
            }

            m_dhSRVetc.CopyToShaderVisibleHeap(descriptorTableBaseIndex, layout->descriptorTableSizeSRVetc);
        }
    }

    BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc)
    {
        BindingLayout* ret = new BindingLayout(desc);
        return BindingLayoutHandle::Create(ret);
    }

    BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout)
    {
        BindingSet *ret = new BindingSet(this);
        ret->desc = desc;

        BindingLayout* pipelineLayout = static_cast<BindingLayout*>(_layout);
        ret->layout = pipelineLayout;

        createResourceBindingsForStage(ret, ShaderType::SHADER_VERTEX,      pipelineLayout->stages[ShaderType::SHADER_VERTEX].get(),    desc.VS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_HULL,        pipelineLayout->stages[ShaderType::SHADER_HULL].get(),      desc.HS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_DOMAIN,      pipelineLayout->stages[ShaderType::SHADER_DOMAIN].get(),    desc.DS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_GEOMETRY,    pipelineLayout->stages[ShaderType::SHADER_GEOMETRY].get(),  desc.GS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_PIXEL,       pipelineLayout->stages[ShaderType::SHADER_PIXEL].get(),     desc.PS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_COMPUTE,     pipelineLayout->stages[ShaderType::SHADER_COMPUTE].get(),   desc.CS);
        createResourceBindingsForStage(ret, ShaderType::SHADER_ALL_GRAPHICS,         pipelineLayout->stages[ShaderType::SHADER_ALL_GRAPHICS].get(),       desc.ALL);

        return BindingSetHandle::Create(ret);
    }

    
    ComputePipelineHandle Device::createComputePipeline(const ComputePipelineDesc& desc)
    {
        RefCountPtr<RootSignature> pRS = getRootSignature(desc.bindingLayouts, false);
        RefCountPtr<ID3D12PipelineState> pPSO = createPipelineState(desc, pRS);

        if (pPSO == nullptr)
            return nullptr;

        ComputePipeline *pso = new ComputePipeline(this);

        pso->desc = desc;

        pso->rootSignature = pRS;
        pso->pipelineState = pPSO;

        return ComputePipelineHandle::Create(pso);
    }

    uint32_t Device::getNumberOfAFRGroups()
    {
        return 1;
    }

    uint32_t Device::getAFRGroupOfCurrentFrame(uint32_t numAFRGroups)
    {
        (void)numAFRGroups;
        return 0;
    }

    nvrhi::CommandListHandle Device::createCommandList(const CommandListParameters& params)
    {
        return CommandListHandle::Create(new CommandList(this, params));
    }

    void Device::executeCommandList(ICommandList* _commandList)
    {
        CommandList* commandList = static_cast<CommandList*>(_commandList);

        std::shared_ptr<CommandListInstance> instance = commandList->execute(m_pCommandQueue);
        m_CommandListsInFlight.push(instance);

        for (auto it : instance->referencedStagingTextures)
        {
            it->lastUseFence = instance->fence;
            it->lastUseFenceValue = instance->instanceID;
        }

        for (auto it : instance->referencedStagingBuffers)
        {
            it->lastUseFence = instance->fence;
            it->lastUseFenceValue = instance->instanceID;
        }

        for (auto it : instance->referencedTimerQueries)
        {
            it->started = true;
            it->resolved = false;
            it->fence = instance->fence;
            it->fenceCounter = instance->instanceID;
        }

        for (auto it : commandList->m_permanentTextureStates)
        {
            Texture* texture = static_cast<Texture*>(it.first.Get());
            CHECK_ERROR(!texture->IsPermanent() || texture->permanentState == it.second, "Attempted to switch texture's permanent state");
            texture->permanentState = it.second;
        }
        commandList->m_permanentTextureStates.clear();

        for (auto it : commandList->m_permanentBufferStates)
        {
            Buffer* buffer = static_cast<Buffer*>(it.first.Get());
            CHECK_ERROR(!buffer->IsPermanent() || buffer->permanentState == it.second, "Attempted to switch buffer's permanent state");
            buffer->permanentState = it.second;
        }
        commandList->m_permanentBufferStates.clear();

        HRESULT hr = m_pDevice->GetDeviceRemovedReason();
        if (FAILED(hr))
        {
            OutputDebugStringA("FATAL ERROR: Device Removed!\n");
            DebugBreak();
        }
    }

    void Device::runGarbageCollection()
    {
        while (!m_CommandListsInFlight.empty())
        {
            std::shared_ptr<CommandListInstance> instance = m_CommandListsInFlight.front();

            if (instance->fence->GetCompletedValue() >= instance->instanceID)
            {
                m_CommandListsInFlight.pop();
            }
            else
            {
                break;
            }
        }
    }

    bool Device::queryFeatureSupport(Feature feature)
    {
        switch (feature)
        {
        case Feature::DeferredCommandLists:
            return true;
        case Feature::SinglePassStereo:
            return m_SinglePassStereoSupported;
        case Feature::RayTracing:
            return m_RayTracingSupported;
        default:
            return false;
        }
    }

    Object Texture::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Resource:
            return Object(resource);
        case ObjectTypes::Nvrhi_D3D12_Device:
            return Object(this);
        default:
            return nullptr;
        }
    }

    Object Texture::getNativeView(ObjectType objectType, Format format, TextureSubresourceSet subresources, bool isReadOnlyDSV)
    {
        static_assert(sizeof(void*) == sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "Cannot typecast a descriptor to void*");
        
        switch (objectType)
        {
        case nvrhi::ObjectTypes::D3D12_ShaderResourceViewGpuDescripror: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex = INVALID_DESCRIPTOR_INDEX;
            auto found = customSRVs.find(key);
            if (found == customSRVs.end())
            {
                descriptorIndex = parent->m_dhSRVetc.AllocateDescriptor();
                customSRVs[key] = descriptorIndex;

                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = parent->m_dhSRVetc.GetCpuHandle(descriptorIndex);
                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleShaderVisible = parent->m_dhSRVetc.GetCpuHandleShaderVisible(descriptorIndex);
                parent->createTextureSRV(cpuHandle.ptr, this, format, subresources);
                parent->m_pDevice->CopyDescriptorsSimple(1, cpuHandleShaderVisible, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(parent->m_dhSRVetc.GetGpuHandle(descriptorIndex).ptr);
        }

        case nvrhi::ObjectTypes::D3D12_UnorderedAccessViewGpuDescripror: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex = INVALID_DESCRIPTOR_INDEX;
            auto found = customUAVs.find(key);
            if (found == customUAVs.end())
            {
                descriptorIndex = parent->m_dhSRVetc.AllocateDescriptor();
                customUAVs[key] = descriptorIndex;

                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = parent->m_dhSRVetc.GetCpuHandle(descriptorIndex);
                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleShaderVisible = parent->m_dhSRVetc.GetCpuHandleShaderVisible(descriptorIndex);
                parent->createTextureUAV(cpuHandle.ptr, this, format, subresources);
                parent->m_pDevice->CopyDescriptorsSimple(1, cpuHandleShaderVisible, cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(parent->m_dhSRVetc.GetGpuHandle(descriptorIndex).ptr);
        }
        case nvrhi::ObjectTypes::D3D12_RenderTargetViewDescriptor: {
            TextureBindingKey key = TextureBindingKey(subresources, format);
            DescriptorIndex descriptorIndex = INVALID_DESCRIPTOR_INDEX;

            auto found = renderTargetViews.find(key);
            if (found == renderTargetViews.end())
            {
                descriptorIndex = parent->m_dhRTV.AllocateDescriptor();
                renderTargetViews[key] = descriptorIndex;

                parent->createTextureRTV(parent->m_dhRTV.GetCpuHandle(descriptorIndex).ptr, this, format, subresources);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(parent->m_dhRTV.GetCpuHandle(descriptorIndex).ptr);
        }

        case nvrhi::ObjectTypes::D3D12_DepthStencilViewDescriptor: {
            TextureBindingKey key = TextureBindingKey(subresources, format, isReadOnlyDSV);
            DescriptorIndex descriptorIndex = INVALID_DESCRIPTOR_INDEX;

            auto found = depthStencilViews.find(key);
            if (found == depthStencilViews.end())
            {
                descriptorIndex = parent->m_dhDSV.AllocateDescriptor();
                depthStencilViews[key] = descriptorIndex;

                parent->createTextureDSV(parent->m_dhDSV.GetCpuHandle(descriptorIndex).ptr, this, subresources, isReadOnlyDSV);
            }
            else
            {
                descriptorIndex = found->second;
            }

            return Object(parent->m_dhDSV.GetCpuHandle(descriptorIndex).ptr);
        }

        default:
            return nullptr;
        }
    }

    Framebuffer::~Framebuffer()
    {
        parent->releaseFramebufferViews(this);
    }

    Texture::~Texture()
    {
        parent->releaseTextureViews(this);
    }

    Object Buffer::getNativeObject(ObjectType objectType)
    {
        switch (objectType)
        {
        case ObjectTypes::D3D12_Resource:
            return Object(resource);
        default:
            return nullptr;
        }
    }

    Buffer::~Buffer()
    {
        parent->releaseBufferViews(this);
    }

    TimerQuery::~TimerQuery()
    {
        parent->releaseTimerQuerySlot(beginQueryIndex);
        parent->releaseTimerQuerySlot(endQueryIndex);
    }

    BindingSet::~BindingSet()
    {
        parent->releaseBindingSetViews(this);
    }

} // namespace d3d12
} // namespace nvrhi
