/*
* Copyright (c) 2012-2016, NVIDIA CORPORATION. All rights reserved.
*
* NVIDIA CORPORATION and its licensors retain all intellectual property
* and proprietary rights in and to this software, related documentation
* and any modifications thereto. Any use, reproduction, disclosure or
* distribution of this software and related documentation without an express
* license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#ifndef GFSDK_NVRHI_H_
#define GFSDK_NVRHI_H_

#include <stdint.h>
#include <memory.h>
#include <atomic>
#include <cmath>
#include <type_traits>

typedef struct _NV_CUSTOM_SEMANTIC NV_CUSTOM_SEMANTIC;

#include <nvrhi/common/containers.h>

#if __linux__ // nerf'd SAL annotations
#define _Inout_
#endif // __linux__

namespace nvrhi
{
    //////////////////////////////////////////////////////////////////////////
    // Basic Types
    //////////////////////////////////////////////////////////////////////////

    struct Color
    {
        float r, g, b, a;
        Color() : r(0.f), g(0.f), b(0.f), a(0.f) { }
        Color(float c) : r(c), g(c), b(c), a(c) { }
        Color(float _r, float _g, float _b, float _a) : r(_r), g(_g), b(_b), a(_a) { }

        bool operator ==(const Color& _b) const { return r == _b.r && g == _b.g && b == _b.b && a == _b.a; }
        bool operator !=(const Color& _b) const { return r != _b.r || g != _b.g || b != _b.b || a != _b.a; }
    };

    struct Viewport
    {
        float minX, maxX;
        float minY, maxY;
        float minZ, maxZ;

        Viewport() : minX(0.f), maxX(0.f), minY(0.f), maxY(0.f), minZ(0.f), maxZ(1.f) { }
        Viewport(float width, float height) : minX(0.f), maxX(width), minY(0.f), maxY(height), minZ(0.f), maxZ(1.f) { }
        Viewport(float _minX, float _maxX, float _minY, float _maxY, float _minZ, float _maxZ) : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY), minZ(_minZ), maxZ(_maxZ) { }

        bool operator ==(const Viewport& b) const { return minX == b.minX && minY == b.minY && minZ == b.minZ && maxX == b.maxX && maxY == b.maxY && maxZ == b.maxZ; }
        bool operator !=(const Viewport& b) const { return !(*this == b); }

        inline float width() const { return maxX - minX; }
        inline float height() const { return maxY - minY; }
    };

    struct Rect
    {
        int minX, maxX;
        int minY, maxY;

        Rect() : minX(0), maxX(0), minY(0), maxY(0) { }
        Rect(int width, int height) : minX(0), maxX(width), minY(0), maxY(height) { }
        Rect(int _minX, int _maxX, int _minY, int _maxY) : minX(_minX), maxX(_maxX), minY(_minY), maxY(_maxY) { }
        Rect(const Viewport& viewport) : minX(int(floor(viewport.minX))), maxX(int(std::ceil(viewport.maxX))), minY(int(std::floor(viewport.minY))), maxY(int(std::ceil(viewport.maxY))) { }

        bool operator ==(const Rect& b) const { return minX == b.minX && minY == b.minY && maxX == b.maxX && maxY == b.maxY; }
        bool operator !=(const Rect& b) const { return !(*this == b); }

        inline int width() const { return maxX - minX; }
        inline int height() const { return maxY - minY; }
    };

    enum struct GraphicsAPI
    {
        D3D11,
        D3D12,
        VULKAN
    };

    typedef uint32_t ObjectType;

    // ObjectTypes namespace contains identifiers for various object types. 
    // All constants have to be distinct. Implementations of NVRHI may extend the list.
    //
    // The encoding is chosen to minimize potential conflicts between implementations.
    // 0x00aabbcc, where:
    //   aa is GAPI, 1 for D3D11, 2 for D3D12, 3 for VK
    //   bb is layer, 0 for native GAPI objects, 1 for reference NVRHI backend, 2 for user-defined backends
    //   cc is a sequential number

    namespace ObjectTypes
    {
        constexpr ObjectType D3D11_Device                           = 0x00010001;
        constexpr ObjectType D3D11_DeviceContext                    = 0x00010002;
        constexpr ObjectType D3D11_Resource                         = 0x00010003;
        constexpr ObjectType D3D11_Buffer                           = 0x00010004;
        constexpr ObjectType D3D11_RenderTargetView                 = 0x00010005;
        constexpr ObjectType D3D11_DepthStencilView                 = 0x00010006;
        constexpr ObjectType D3D11_ShaderResourceView               = 0x00010007;
        constexpr ObjectType D3D11_UnorderedAccessView              = 0x00010008;

        constexpr ObjectType D3D12_Device                           = 0x00020001;
        constexpr ObjectType D3D12_CommandQueue                     = 0x00020002;
        constexpr ObjectType D3D12_GraphicsCommandList              = 0x00020003;
        constexpr ObjectType D3D12_Resource                         = 0x00020004;
        constexpr ObjectType D3D12_RenderTargetViewDescriptor       = 0x00020005;
        constexpr ObjectType D3D12_DepthStencilViewDescriptor       = 0x00020006;
        constexpr ObjectType D3D12_ShaderResourceViewGpuDescripror  = 0x00020007;
        constexpr ObjectType D3D12_UnorderedAccessViewGpuDescripror = 0x00020008;
        constexpr ObjectType D3D12_RootSignature                    = 0x00020009;
        constexpr ObjectType D3D12_PipelineState                    = 0x0002000a;

        constexpr ObjectType VK_Image                               = 0x00030001;
        constexpr ObjectType VK_Device                              = 0x00030002;
        constexpr ObjectType VK_PhysicalDevice                      = 0x00030003;
        constexpr ObjectType VK_Instance                            = 0x00030004;
        constexpr ObjectType VK_CommandBuffer                       = 0x00030005;
        constexpr ObjectType VK_Buffer                              = 0x00030006;
        constexpr ObjectType VK_ImageView                           = 0x00030007;
    };

    struct Object
    {
        union {
            uint64_t integer;
            void* pointer;
        };

        Object(uint64_t i) : integer(i) { }
        Object(void* p) : pointer(p) { }

        template<typename T> operator T*() const { return static_cast<T*>(pointer); }
    };

    class IResource
    {
    public:
        virtual unsigned long AddRef() = 0;
        virtual unsigned long Release() = 0;

        // Returns a native object or interface, for example ID3D11Device*, or nullptr if the requested interface is unavailable.
        // Does *not* AddRef the returned interface.
        virtual Object getNativeObject(ObjectType objectType) { (void)objectType; return nullptr; }

        virtual ~IResource() { }
    };


    //////////////////////////////////////////////////////////////////////////
    // RefCountPtr
    // Mostly a copy of Microsoft::WRL::ComPtr<T>
    //////////////////////////////////////////////////////////////////////////

    template <typename T>
    class RefCountPtr
    {
    public:
        typedef T InterfaceType;

        template <bool b, typename U = void>
        struct EnableIf
        {
        };

        template <typename U>
        struct EnableIf<true, U>
        {
            typedef U type;
        };

    protected:
        InterfaceType *ptr_;
        template<class U> friend class RefCountPtr;

        void InternalAddRef() const throw()
        {
            if (ptr_ != nullptr)
            {
                ptr_->AddRef();
            }
        }

        unsigned long InternalRelease() throw()
        {
            unsigned long ref = 0;
            T* temp = ptr_;

            if (temp != nullptr)
            {
                ptr_ = nullptr;
                ref = temp->Release();
            }

            return ref;
        }

    public:

        RefCountPtr() throw() : ptr_(nullptr)
        {
        }

        RefCountPtr(std::nullptr_t) throw() : ptr_(nullptr)
        {
        }

        template<class U>
        RefCountPtr(U *other) throw() : ptr_(other)
        {
            InternalAddRef();
        }

        RefCountPtr(const RefCountPtr& other) throw() : ptr_(other.ptr_)
        {
            InternalAddRef();
        }

        // copy ctor that allows to instanatiate class when U* is convertible to T*
        template<class U>
        RefCountPtr(const RefCountPtr<U> &other, typename std::enable_if<std::is_convertible<U*, T*>::value, void *>::type * = 0) throw() :
            ptr_(other.ptr_)
        
        {
            InternalAddRef();
        }

        RefCountPtr(_Inout_ RefCountPtr &&other) throw() : ptr_(nullptr)
        {
            if (this != reinterpret_cast<RefCountPtr*>(&reinterpret_cast<unsigned char&>(other)))
            {
                Swap(other);
            }
        }

        // Move ctor that allows instantiation of a class when U* is convertible to T*
        template<class U>
        RefCountPtr(_Inout_ RefCountPtr<U>&& other, typename std::enable_if<std::is_convertible<U*, T*>::value, void *>::type * = 0) throw() :
            ptr_(other.ptr_)
        {
            other.ptr_ = nullptr;
        }

        ~RefCountPtr() throw()
        {
            InternalRelease();
        }

        RefCountPtr& operator=(std::nullptr_t) throw()
        {
            InternalRelease();
            return *this;
        }

        RefCountPtr& operator=(T *other) throw()
        {
            if (ptr_ != other)
            {
                RefCountPtr(other).Swap(*this);
            }
            return *this;
        }

        template <typename U>
        RefCountPtr& operator=(U *other) throw()
        {
            RefCountPtr(other).Swap(*this);
            return *this;
        }

        RefCountPtr& operator=(const RefCountPtr &other) throw()
        {
            if (ptr_ != other.ptr_)
            {
                RefCountPtr(other).Swap(*this);
            }
            return *this;
        }

        template<class U>
        RefCountPtr& operator=(const RefCountPtr<U>& other) throw()
        {
            RefCountPtr(other).Swap(*this);
            return *this;
        }

        RefCountPtr& operator=(_Inout_ RefCountPtr &&other) throw()
        {
            RefCountPtr(static_cast<RefCountPtr&&>(other)).Swap(*this);
            return *this;
        }

        template<class U>
        RefCountPtr& operator=(_Inout_ RefCountPtr<U>&& other) throw()
        {
            RefCountPtr(static_cast<RefCountPtr<U>&&>(other)).Swap(*this);
            return *this;
        }

        void Swap(_Inout_ RefCountPtr&& r) throw()
        {
            T* tmp = ptr_;
            ptr_ = r.ptr_;
            r.ptr_ = tmp;
        }

        void Swap(_Inout_ RefCountPtr& r) throw()
        {
            T* tmp = ptr_;
            ptr_ = r.ptr_;
            r.ptr_ = tmp;
        }

        T* Get() const throw()
        {
            return ptr_;
        }
        
        operator T*() const
        {
            return ptr_;
        }

        InterfaceType* operator->() const throw()
        {
            return ptr_;
        }

        T** operator&() 
        {
            return &ptr_;
        }

        T* const* GetAddressOf() const throw()
        {
            return &ptr_;
        }

        T** GetAddressOf() throw()
        {
            return &ptr_;
        }

        T** ReleaseAndGetAddressOf() throw()
        {
            InternalRelease();
            return &ptr_;
        }

        T* Detach() throw()
        {
            T* ptr = ptr_;
            ptr_ = nullptr;
            return ptr;
        }

        // Set the pointer while keeping the object's reference count unchanged
        void Attach(InterfaceType* other) throw()
        {
            if (ptr_ != nullptr)
            {
                auto ref = ptr_->Release();
                (void)ref;

                // Attaching to the same object only works if duplicate references are being coalesced. Otherwise
                // re-attaching will cause the pointer to be released and may cause a crash on a subsequent dereference.
                assert(ref != 0 || ptr_ != other);
            }

            ptr_ = other;
        }

        // Create a wrapper around a raw object while keeping the object's reference count unchanged
        static RefCountPtr<T> Create(T* other)
        {
            RefCountPtr<T> Ptr;
            Ptr.Attach(other);
            return Ptr;
        }

        unsigned long Reset()
        {
            return InternalRelease();
        }
    };    // RefCountPtr

    typedef RefCountPtr<IResource> ResourceHandle;

    //////////////////////////////////////////////////////////////////////////
    // RefCounter<T>
    // A class that implements reference counting in a way compatible with RefCountPtr.
    // Intended usage is to use it as a base class for interface implementations, like so:
    // class Texture : public RefCounter<ITexture> { ... }
    //////////////////////////////////////////////////////////////////////////

    template<class T>
    class RefCounter : public T
    {
    private:
        std::atomic<unsigned long> m_refCount = 1;
    public:
        virtual unsigned long AddRef() override 
        {
            return ++m_refCount;
        }

        virtual unsigned long Release() override
        {
            unsigned long result = --m_refCount;
            if (result == 0) delete this;
            return result;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Texture
    //////////////////////////////////////////////////////////////////////////

    enum class Format
    {
        UNKNOWN,

        R8_UINT,
        R8_SINT,
        R8_UNORM,
        R8_SNORM,
        RG8_UINT,
        RG8_SINT,
        RG8_UNORM,
        RG8_SNORM,
        R16_UINT,
        R16_SINT,
        R16_UNORM,
        R16_SNORM,
        R16_FLOAT,
        BGRA4_UNORM,
        B5G6R5_UNORM,
        B5G5R5A1_UNORM,
        RGBA8_UINT,
        RGBA8_SINT,
        RGBA8_UNORM,
        RGBA8_SNORM,
        BGRA8_UNORM,
        SRGBA8_UNORM,
        SBGRA8_UNORM,
        R10G10B10A2_UNORM,
        R11G11B10_FLOAT,
        RG16_UINT,
        RG16_SINT,
        RG16_UNORM,
        RG16_SNORM,
        RG16_FLOAT,
        R32_UINT,
        R32_SINT,
        R32_FLOAT,
        RGBA16_UINT,
        RGBA16_SINT,
        RGBA16_FLOAT,
        RGBA16_UNORM,
        RGBA16_SNORM,
        RG32_UINT,
        RG32_SINT,
        RG32_FLOAT,
        RGB32_UINT,
        RGB32_SINT,
        RGB32_FLOAT,
        RGBA32_UINT,
        RGBA32_SINT,
        RGBA32_FLOAT,

        // be sure to adjust utility functions below if adding or rearranging depthstencil formats
        D16,
        D24S8,
        X24G8_UINT,
        D32,
        D32S8,
        X32G8_UINT,

        BC1_UNORM,
        BC1_UNORM_SRGB,
        BC2_UNORM,
        BC2_UNORM_SRGB,
        BC3_UNORM,
        BC3_UNORM_SRGB,
        BC4_UNORM,
        BC4_SNORM,
        BC5_UNORM,
        BC5_SNORM,
        BC6H_UFLOAT,
        BC6H_SFLOAT,
        BC7_UNORM,
        BC7_UNORM_SRGB,

        COUNT,
    };

    static inline bool FormatIsDepthStencil(Format fmt)
    {
        return fmt >= Format::D16 && fmt <= Format::X32G8_UINT;
    }

    static inline bool FormatIsStencil(Format fmt)
    {
        return fmt == Format::X32G8_UINT || fmt == Format::X24G8_UINT;
    }

    enum class TextureDimension
    {
        Unknown,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        TextureCube,
        TextureCubeArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D
    };

    enum class CpuAccessMode
    {
        None,
        Read,
        Write
    };
    
    struct ResourceStates
    {
        enum Enum
        {
            COMMON = 0,
            CONSTANT_BUFFER = 0x1,
            VERTEX_BUFFER = 0x2,
            INDEX_BUFFER = 0x4,
            INDIRECT_ARGUMENT = 0x8,
            SHADER_RESOURCE = 0x10,
            UNORDERED_ACCESS = 0x20,
            RENDER_TARGET = 0x40,
            DEPTH_WRITE = 0x80,
            DEPTH_READ = 0x100,
            STREAM_OUT = 0x200,
            COPY_DEST = 0x400,
            COPY_SOURCE = 0x800,
            RESOLVE_DEST = 0x1000,
            RESOLVE_SOURCE = 0x2000,
            PRESENT = 0x8000,
            RAY_TRACING_AS = 0x10000
        };
    };

    typedef uint32_t MipLevel;
    typedef uint32_t ArraySlice;

    struct TextureDesc
    {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t arraySize = 1;
        uint32_t mipLevels = 1;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;
        Format format = Format::UNKNOWN;
        TextureDimension dimension = TextureDimension::Texture2D;
        const char* debugName = nullptr;

        bool isRenderTarget = false;
        bool isUAV = false;
        bool isTypeless = false;
        bool disableGPUsSync = false;

        Color clearValue;
        bool useClearValue = false;

        ResourceStates::Enum initialState = ResourceStates::COMMON;

        // If keepInitialState is true, command lists that use the texture will automatically
        // begin tracking the texture from the initial state and transition it to the initial state 
        // on command list close.
        bool keepInitialState = false;
    };

    // describes a 2D section of a single mip level + single slice of a texture
    struct TextureSlice
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t z = 0;
        // -1 means the entire dimension is part of the region
        // resolve() below will translate these values into actual dimensions
        uint32_t width = uint32_t(-1);
        uint32_t height = uint32_t(-1);
        uint32_t depth = uint32_t(-1);

        ArraySlice arraySlice = 0;
        MipLevel mipLevel = 0;

        TextureSlice resolve(const TextureDesc& desc) const
        {
            TextureSlice ret(*this);

            assert(mipLevel < desc.mipLevels);

            if (width == uint32_t(-1))
                ret.width = (desc.width >> mipLevel);

            if (height == uint32_t(-1))
                ret.height = (desc.height >> mipLevel);

            if (depth == uint32_t(-1))
            {
                if (desc.dimension == TextureDimension::Texture3D)
                    ret.depth = (desc.depth >> mipLevel);
                else
                    ret.depth = 1;
            }

            return ret;
        }

        // helper functions for common patterns
        static TextureSlice setMip(MipLevel level)
        {
            TextureSlice ret;
            ret.mipLevel = level;

            return ret;
        }

        static TextureSlice setArraySlice(ArraySlice slice)
        {
            TextureSlice ret;
            ret.arraySlice = slice;

            return ret;
        }
    };

    struct TextureSubresourceSet
    {
        static constexpr MipLevel AllMipLevels = MipLevel(-1);
        static constexpr ArraySlice AllArraySlices = ArraySlice(-1);
        
        MipLevel baseMipLevel;
        MipLevel numMipLevels;
        ArraySlice baseArraySlice;
        ArraySlice numArraySlices;

        TextureSubresourceSet() 
        { }

        TextureSubresourceSet(MipLevel _baseMipLevel, MipLevel _numMipLevels, ArraySlice _baseArraySlice, ArraySlice _numArraySlices)
            : baseMipLevel(_baseMipLevel)
            , numMipLevels(_numMipLevels)
            , baseArraySlice(_baseArraySlice)
            , numArraySlices(_numArraySlices)
        {
        }

        TextureSubresourceSet resolve(const TextureDesc& desc, bool singleMipLevel) const
        {
            TextureSubresourceSet ret;
            ret.baseMipLevel = baseMipLevel;

            if (singleMipLevel)
            {
                ret.numMipLevels = 1;
            }
            else
            {
                int lastMipLevelPlusOne = std::min(baseMipLevel + numMipLevels, desc.mipLevels);
                ret.numMipLevels = MipLevel(std::max(0u, lastMipLevelPlusOne - baseMipLevel));
            }

            switch (desc.dimension)
            {
            case TextureDimension::Texture1DArray:
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
            case TextureDimension::Texture2DMSArray: {
                ret.baseArraySlice = baseArraySlice;
                int lastArraySlicePlusOne = std::min(baseArraySlice + numArraySlices, desc.arraySize);
                ret.numArraySlices = ArraySlice(std::max(0u, lastArraySlicePlusOne - baseArraySlice));
                break;
            }
            default: 
                ret.baseArraySlice = 0;
                ret.numArraySlices = 1;
                break;
            }

            return ret;
        }

        bool isEntireTexture(const TextureDesc& desc) const
        {
            if (baseMipLevel > 0u || baseMipLevel + numMipLevels < int(desc.mipLevels))
                return false;

            switch (desc.dimension)
            {
            case TextureDimension::Texture1DArray:
            case TextureDimension::Texture2DArray:
            case TextureDimension::TextureCube:
            case TextureDimension::TextureCubeArray:
            case TextureDimension::Texture2DMSArray: 
                if (baseArraySlice > 0u || baseArraySlice + numArraySlices < int(desc.arraySize))
                    return false;
            default:;
            }

            return true;
        }

        bool operator== (const TextureSubresourceSet& other) const
        {
            return baseMipLevel == other.baseMipLevel &&
                numMipLevels == other.numMipLevels &&
                baseArraySlice == other.baseArraySlice &&
                numArraySlices == other.numArraySlices;
        }

        // see the bottom of this file for a specialization of std::hash<TextureSubresourceSet>
    };

    static const TextureSubresourceSet AllSubresources = TextureSubresourceSet(0, TextureSubresourceSet::AllMipLevels, 0, TextureSubresourceSet::AllArraySlices);

    class ITexture : public IResource
    {
    public:
        virtual const TextureDesc& GetDesc() const = 0;

        // Similar to getNativeObject, returns a native view for a specified set of subresources. Returns nullptr if unavailable.
        // TODO: on D3D12, the views might become invalid later if the view heap is grown/reallocated, we should do something about that.
        virtual Object getNativeView(ObjectType objectType, Format format = Format::UNKNOWN, TextureSubresourceSet subresources = AllSubresources, bool isReadOnlyDSV = false) { (void)objectType; (void)format; (void)subresources; (void)isReadOnlyDSV; return nullptr; }
    };
    typedef RefCountPtr<ITexture> TextureHandle;

    class IStagingTexture : public IResource
    {
    public:
        virtual const TextureDesc& GetDesc() const = 0;
    };
    typedef RefCountPtr<IStagingTexture> StagingTextureHandle;

    //////////////////////////////////////////////////////////////////////////
    // Input Layout
    //////////////////////////////////////////////////////////////////////////

    struct VertexAttributeDesc
    {
        enum { MAX_NAME_LENGTH = 256 };

        char name[MAX_NAME_LENGTH];
        Format format;
        uint32_t arraySize;
        uint32_t bufferIndex;
        uint32_t offset;
        // note: for most APIs, all strides for a given bufferIndex must be identical
        uint32_t elementStride;
        bool isInstanced;
    };

    class IInputLayout : public IResource
    {
    public:
        virtual uint32_t GetNumAttributes() const = 0;
        virtual const VertexAttributeDesc* GetAttributeDesc(uint32_t index) const = 0;
    };

    typedef RefCountPtr<IInputLayout> InputLayoutHandle;

    //////////////////////////////////////////////////////////////////////////
    // Buffer
    //////////////////////////////////////////////////////////////////////////

    struct BufferDesc
    {
        uint32_t byteSize = 0;
        uint32_t structStride = 0; //if non-zero it's structured
        const char* debugName = nullptr;
        bool canHaveUAVs = false;
        bool isVertexBuffer = false;
        bool isIndexBuffer = false;
        bool isConstantBuffer = false;
        bool isDrawIndirectArgs = false;
        bool isVolatile = false; // a dynamic/upload buffer whose contents only live in the current command list
        bool disableGPUsSync = false;

        ResourceStates::Enum initialState = ResourceStates::COMMON;

        // see TextureDesc::keepInitialState
        bool keepInitialState = false;

        CpuAccessMode cpuAccess = CpuAccessMode::None;
    };

    struct BufferRange
    {
        uint32_t byteOffset;
        uint32_t byteSize;
        
        BufferRange() 
        { }

        BufferRange(uint32_t _byteOffset, uint32_t _byteSize)
            : byteOffset(_byteOffset)
            , byteSize(_byteSize)
        { }

        BufferRange resolve(const BufferDesc& desc) const
        {
            BufferRange result;
            result.byteOffset = std::min(byteOffset, desc.byteSize);
            if (byteSize == 0)
                result.byteSize = desc.byteSize - result.byteOffset;
            else
                result.byteSize = std::min(byteSize, desc.byteSize - result.byteOffset);
            return result;
        }

        bool isEntireBuffer(const BufferDesc& desc) const
        {
            return (byteOffset == 0) && (byteSize == ~0u || byteSize == desc.byteSize);
        }

        bool operator== (const BufferRange& other) const
        {
            return byteOffset == other.byteOffset &&
                byteSize == other.byteSize;
        }
    };

    static const BufferRange EntireBuffer = BufferRange(0, ~0u);

    class IBuffer : public IResource
    {
    public:
        virtual const BufferDesc& GetDesc() const = 0;
    };

    typedef RefCountPtr<IBuffer> BufferHandle;

    //////////////////////////////////////////////////////////////////////////
    // Shader
    //////////////////////////////////////////////////////////////////////////

    struct ShaderType
    {
        enum Enum
        {
            SHADER_VERTEX,
            SHADER_HULL,
            SHADER_DOMAIN,
            SHADER_GEOMETRY,
            SHADER_PIXEL,
            SHADER_ALL_GRAPHICS, // Special value for processing bindings

            SHADER_COMPUTE,

            SHADER_RAY_GENERATION,
            SHADER_MISS,
            SHADER_CLOSEST_HIT,
            SHADER_ANY_HIT,
            SHADER_INTERSECTION
        };
    };

    struct FastGeometryShaderFlags
    {
        enum Enum
        {
            FORCE_FAST_GS = 0x01,
            COMPATIBILITY_MODE = 0x02,
            USE_VIEWPORT_MASK = 0x04,
            OFFSET_RT_INDEX_BY_VP_INDEX = 0x08,
            STRICT_API_ORDER = 0x10
        };
    };

    struct ShaderDesc
    {
        ShaderType::Enum shaderType;
        std::string debugName;
        std::string entryName;

        int hlslExtensionsUAV;

        bool useSpecificShaderExt;
        uint32_t numCustomSemantics;
        NV_CUSTOM_SEMANTIC* pCustomSemantics;

        FastGeometryShaderFlags::Enum fastGSFlags;
        uint32_t* pCoordinateSwizzling;

        ShaderDesc(ShaderType::Enum type)
            : shaderType(type)
            , debugName("")
            , entryName("main")
            , hlslExtensionsUAV(-1)
            , useSpecificShaderExt(false)
            , numCustomSemantics(0)
            , pCustomSemantics(nullptr)
            , fastGSFlags(FastGeometryShaderFlags::Enum(0))
            , pCoordinateSwizzling(nullptr)
        { }
    };

    struct ShaderConstant
    {
        const char* name;
        const char* value;

        // TODO: add info about Vulkan shader specializations
    };

    class IShader : public IResource
    {
    public:
        virtual const ShaderDesc& GetDesc() const = 0;
        virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const = 0;
    };

    typedef RefCountPtr<IShader> ShaderHandle;

    //////////////////////////////////////////////////////////////////////////
    // Shader Library
    //////////////////////////////////////////////////////////////////////////

    class IShaderLibrary : public IResource
    {
    public:
        virtual void GetBytecode(const void** ppBytecode, size_t* pSize) const = 0;
        virtual ShaderHandle GetShader(const char* entryName, ShaderType::Enum shaderType) = 0;
    };

    typedef RefCountPtr<IShaderLibrary> ShaderLibraryHandle;

    //////////////////////////////////////////////////////////////////////////
    // Blend State
    //////////////////////////////////////////////////////////////////////////

    struct BlendState
    {
        enum { MAX_MRT_BLEND_COUNT = 8 };

        enum BlendValue : unsigned char
        {
            BLEND_ZERO = 1,
            BLEND_ONE = 2,
            BLEND_SRC_COLOR = 3,
            BLEND_INV_SRC_COLOR = 4,
            BLEND_SRC_ALPHA = 5,
            BLEND_INV_SRC_ALPHA = 6,
            BLEND_DEST_ALPHA = 7,
            BLEND_INV_DEST_ALPHA = 8,
            BLEND_DEST_COLOR = 9,
            BLEND_INV_DEST_COLOR = 10,
            BLEND_SRC_ALPHA_SAT = 11,
            BLEND_BLEND_FACTOR = 14,
            BLEND_INV_BLEND_FACTOR = 15,
            BLEND_SRC1_COLOR = 16,
            BLEND_INV_SRC1_COLOR = 17,
            BLEND_SRC1_ALPHA = 18,
            BLEND_INV_SRC1_ALPHA = 19
        };

        enum BlendOp : unsigned char
        {
            BLEND_OP_ADD = 1,
            BLEND_OP_SUBTRACT = 2,
            BLEND_OP_REV_SUBTRACT = 3,
            BLEND_OP_MIN = 4,
            BLEND_OP_MAX = 5
        };

        enum ColorMask : unsigned char
        {
            COLOR_MASK_RED = 1,
            COLOR_MASK_GREEN = 2,
            COLOR_MASK_BLUE = 4,
            COLOR_MASK_ALPHA = 8,
            COLOR_MASK_ALL = 0xF
        };

        bool        blendEnable[MAX_MRT_BLEND_COUNT];
        BlendValue  srcBlend[MAX_MRT_BLEND_COUNT];
        BlendValue  destBlend[MAX_MRT_BLEND_COUNT];
        BlendOp     blendOp[MAX_MRT_BLEND_COUNT];
        BlendValue  srcBlendAlpha[MAX_MRT_BLEND_COUNT];
        BlendValue  destBlendAlpha[MAX_MRT_BLEND_COUNT];
        BlendOp     blendOpAlpha[MAX_MRT_BLEND_COUNT];
        ColorMask   colorWriteEnable[MAX_MRT_BLEND_COUNT];
        Color    blendFactor;
        bool        alphaToCoverage;
        uint8_t     padding[7];

        BlendState() :
            blendFactor(0, 0, 0, 0),
            alphaToCoverage(false)
        {
            for (uint32_t i = 0; i < MAX_MRT_BLEND_COUNT; i++)
            {
                blendEnable[i] = false;
                colorWriteEnable[i] = COLOR_MASK_ALL;
                srcBlend[i] = BLEND_ONE;
                destBlend[i] = BLEND_ZERO;
                blendOp[i] = BLEND_OP_ADD;
                srcBlendAlpha[i] = BLEND_ONE;
                destBlendAlpha[i] = BLEND_ZERO;
                blendOpAlpha[i] = BLEND_OP_ADD;
            }

            memset(padding, 0, sizeof(padding));
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Raster State
    //////////////////////////////////////////////////////////////////////////

    struct RasterState
    {
        enum FillMode : unsigned char
        {
            FILL_SOLID,
            FILL_LINE
        };

        enum CullMode : unsigned char
        {
            CULL_BACK,
            CULL_FRONT,
            CULL_NONE
        };

        FillMode    fillMode;
        CullMode    cullMode;
        bool frontCounterClockwise;
        bool depthClipEnable;
        bool scissorEnable;
        bool multisampleEnable;
        bool antialiasedLineEnable;
        int depthBias;
        float depthBiasClamp;
        float slopeScaledDepthBias;

        // Extended rasterizer state supported by Maxwell
        // In D3D11, use NvAPI_D3D11_CreateRasterizerState to create such rasterizer state.
        char forcedSampleCount;
        bool programmableSamplePositionsEnable;
        bool conservativeRasterEnable;
        bool quadFillEnable;
        char samplePositionsX[16];
        char samplePositionsY[16];

        RasterState() {
            memset(this, 0, sizeof(RasterState));
            depthClipEnable = true;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Depth Stencil State
    //////////////////////////////////////////////////////////////////////////

    struct DepthStencilState
    {
        enum DepthWriteMask : unsigned char
        {
            DEPTH_WRITE_MASK_ZERO = 0,
            DEPTH_WRITE_MASK_ALL = 1
        };

        enum StencilOp : unsigned char
        {
            STENCIL_OP_KEEP = 1,
            STENCIL_OP_ZERO = 2,
            STENCIL_OP_REPLACE = 3,
            STENCIL_OP_INCR_SAT = 4,
            STENCIL_OP_DECR_SAT = 5,
            STENCIL_OP_INVERT = 6,
            STENCIL_OP_INCR = 7,
            STENCIL_OP_DECR = 8
        };

        enum ComparisonFunc : unsigned char
        {
            COMPARISON_NEVER = 1,
            COMPARISON_LESS = 2,
            COMPARISON_EQUAL = 3,
            COMPARISON_LESS_EQUAL = 4,
            COMPARISON_GREATER = 5,
            COMPARISON_NOT_EQUAL = 6,
            COMPARISON_GREATER_EQUAL = 7,
            COMPARISON_ALWAYS = 8
        };

        struct StencilOpDesc
        {
            StencilOp stencilFailOp;
            StencilOp stencilDepthFailOp;
            StencilOp stencilPassOp;
            ComparisonFunc stencilFunc;
        };

        bool            depthEnable;
        DepthWriteMask  depthWriteMask;
        ComparisonFunc  depthFunc;
        bool            stencilEnable;
        uint8_t         stencilReadMask;
        uint8_t         stencilWriteMask;
        uint8_t         stencilRefValue;
        uint8_t         padding;
        StencilOpDesc   frontFace;
        StencilOpDesc   backFace;

        DepthStencilState() :
            stencilRefValue(0),
            depthEnable(true),
            depthWriteMask(DEPTH_WRITE_MASK_ALL),
            depthFunc(COMPARISON_LESS),
            stencilEnable(false),
            stencilReadMask(0xff),
            stencilWriteMask(0xff),
            padding(0)
        {
            StencilOpDesc stencilOpDesc;
            stencilOpDesc.stencilFailOp = STENCIL_OP_KEEP;
            stencilOpDesc.stencilDepthFailOp = STENCIL_OP_KEEP;
            stencilOpDesc.stencilPassOp = STENCIL_OP_KEEP;
            stencilOpDesc.stencilFunc = COMPARISON_ALWAYS;
            frontFace = stencilOpDesc;
            backFace = stencilOpDesc;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Viewport State
    //////////////////////////////////////////////////////////////////////////

    struct ViewportState
    {
        enum { MAX_VIEWPORTS = 16 };

        //These are in pixels
        // note: you can only set each of these either in the PSO or per draw call in DrawArguments
        // it is not legal to have the same state set in both the PSO and DrawArguments
        // leaving these vectors empty means no state is set
        static_vector<Viewport, MAX_VIEWPORTS> viewports;
        static_vector<Rect, MAX_VIEWPORTS> scissorRects;

        inline ViewportState& addViewport(const Viewport& v) { viewports.push_back(v); return *this; }
        inline ViewportState& addScissorRect(const Rect& r) { scissorRects.push_back(r); return *this; }
        inline ViewportState& addViewportAndScissorRect(const Viewport& v) { return addViewport(v).addScissorRect(Rect(v)); }
    };

    //////////////////////////////////////////////////////////////////////////
    // Sampler
    //////////////////////////////////////////////////////////////////////////

    struct SamplerDesc
    {
        enum WrapMode : unsigned char
        {
            WRAP_MODE_CLAMP,
            WRAP_MODE_WRAP,
            WRAP_MODE_BORDER
        };

        enum ReductionType : unsigned char
        {
            REDUCTION_STANDARD,
            REDUCTION_COMPARISON,
            REDUCTION_MINIMUM,
            REDUCTION_MAXIMUM
        };

        WrapMode wrapMode[3];
        float mipBias, anisotropy;
        bool minFilter, magFilter, mipFilter;
        ReductionType reductionType;
        Color borderColor;

        SamplerDesc() :
            minFilter(true),
            magFilter(true),
            mipFilter(true),
            mipBias(0),
            anisotropy(1),
            reductionType(REDUCTION_STANDARD),
            borderColor(1, 1, 1, 1)
        {
            wrapMode[0] = wrapMode[1] = wrapMode[2] = WRAP_MODE_CLAMP;
        }
    };

    class ISampler : public IResource 
    { 
    public:
        virtual const SamplerDesc& GetDesc() const = 0;
    };

    typedef RefCountPtr<ISampler> SamplerHandle;

    namespace rt
    {
        //////////////////////////////////////////////////////////////////////////
        // rt::AccelStruct
        //////////////////////////////////////////////////////////////////////////

        class IAccelStruct : public IResource
        {
        };

        typedef RefCountPtr<IAccelStruct> AccelStructHandle;

    }

    //////////////////////////////////////////////////////////////////////////
    // Framebuffer
    //////////////////////////////////////////////////////////////////////////

    struct FramebufferAttachment
    {
        ITexture* texture;
        TextureSubresourceSet subresources;
        Format format;
        bool isReadOnly;

        FramebufferAttachment(ITexture* texture = nullptr,
            ArraySlice targetIndex = 0,
            MipLevel targetMipSlice = 0,
            Format format = Format::UNKNOWN,
            bool isReadOnly = false)
            : texture(texture)
            , subresources(targetMipSlice, 1, targetIndex, 1)
            , format(format)
            , isReadOnly(isReadOnly)
        { }

        FramebufferAttachment(ITexture* texture, const TextureSubresourceSet& subresources, Format format = Format::UNKNOWN, bool isReadOnly = false)
            : texture(texture)
            , subresources(subresources)
            , format(format)
            , isReadOnly(isReadOnly)
        { }

        FramebufferAttachment& setTexture(nvrhi::ITexture* t) { texture = t; return *this; }
        FramebufferAttachment& setTargetIndex(ArraySlice index) { subresources.baseArraySlice = index; subresources.numArraySlices = 1; return *this; }
        FramebufferAttachment& setTargetIndexRange(ArraySlice index, ArraySlice count) { subresources.baseArraySlice = index; subresources.numArraySlices = count; return *this; }
        FramebufferAttachment& setTargetMipSlice(MipLevel level) { subresources.baseMipLevel = level; subresources.numMipLevels = 1; return *this; }
        FramebufferAttachment& setFormat(Format f) { format = f; return *this; }
        FramebufferAttachment& setReadOnly(bool ro) { isReadOnly = ro; return *this; }

        bool valid() const { return texture != nullptr; }

        Object getNativeView(ObjectType objectType) const { if (texture) return texture->getNativeView(objectType, format, subresources); else return nullptr; }
    };

    struct FramebufferDesc
    {
        enum { MAX_RENDER_TARGETS = 8 };

        static_vector<FramebufferAttachment, MAX_RENDER_TARGETS> colorAttachments;
        FramebufferAttachment depthAttachment;

        FramebufferDesc& addColorAttachment(FramebufferAttachment a)
        {
            colorAttachments.push_back(a);
            return *this;
        }

        FramebufferDesc& addColorAttachment(ITexture* texture, uint32_t targetIndex = 0, uint32_t targetMipSlice = 0, Format format = Format::UNKNOWN, bool isReadOnly = false)
        {
            colorAttachments.push_back(FramebufferAttachment(texture, targetIndex, targetMipSlice, format, isReadOnly));
            return *this;
        }

        FramebufferDesc& addColorAttachment(ITexture* texture, const TextureSubresourceSet& subresources, Format format = Format::UNKNOWN, bool isReadOnly = false)
        {
            colorAttachments.push_back(FramebufferAttachment(texture, subresources, format, isReadOnly));
            return *this;
        }

        FramebufferDesc& setDepthAttachment(FramebufferAttachment d)
        {
            depthAttachment = d;
            return *this;
        }

        FramebufferDesc& setDepthAttachment(ITexture* texture, uint32_t targetIndex = 0, uint32_t targetMipSlice = 0, bool isReadOnly = false)
        {
            depthAttachment = FramebufferAttachment(texture, targetIndex, targetMipSlice, Format::UNKNOWN, isReadOnly);
            return *this;
        }

        FramebufferDesc& setDepthAttachment(ITexture* texture, const TextureSubresourceSet& subresources, bool isReadOnly = false)
        {
            depthAttachment = FramebufferAttachment(texture, subresources, Format::UNKNOWN, isReadOnly);
            return *this;
        }
    };

    struct FramebufferInfo
    {
        static_vector<Format, FramebufferDesc::MAX_RENDER_TARGETS> colorFormats;
        Format depthFormat = Format::UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t sampleCount = 1;
        uint32_t sampleQuality = 0;

        FramebufferInfo()
        { }

        FramebufferInfo(const FramebufferDesc& desc)
        {
            for (size_t i = 0; i < desc.colorAttachments.size(); i++)
            {
                const FramebufferAttachment& attachment = desc.colorAttachments[i];
                colorFormats.push_back(attachment.format == Format::UNKNOWN && attachment.texture ? attachment.texture->GetDesc().format : attachment.format);
            }

            if (desc.depthAttachment.valid())
            {
                const TextureDesc& textureDesc = desc.depthAttachment.texture->GetDesc();
                depthFormat = textureDesc.format;
                width = textureDesc.width;
                height = textureDesc.height;
                sampleCount = textureDesc.sampleCount;
                sampleQuality = textureDesc.sampleQuality;
            }
            else if(desc.colorAttachments.size() > 0 && desc.colorAttachments[0].valid())
            {
                const TextureDesc& textureDesc = desc.colorAttachments[0].texture->GetDesc();
                width = textureDesc.width;
                height = textureDesc.height;
                sampleCount = textureDesc.sampleCount;
                sampleQuality = textureDesc.sampleQuality;
            }
        }

        bool operator==(const FramebufferInfo& other) const { return formatsEqual(colorFormats, other.colorFormats) && depthFormat == other.depthFormat && width == other.width && height == other.height && sampleCount == other.sampleCount && sampleQuality == other.sampleQuality; }
        bool operator!=(const FramebufferInfo& other) const { return !(*this == other); }

        inline Viewport GetViewport(float minZ = 0.f, float maxZ = 1.f) const { return Viewport(0.f, float(width), 0.f, float(height), minZ, maxZ); }

    private:
        static bool formatsEqual(const static_vector<Format, FramebufferDesc::MAX_RENDER_TARGETS>& a, const static_vector<Format, FramebufferDesc::MAX_RENDER_TARGETS>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return false;
            return true;
        }
    };

    class IFramebuffer : public IResource 
    {
    public:
        virtual const FramebufferDesc& GetDesc() const = 0;
        virtual const FramebufferInfo& GetFramebufferInfo() const = 0;
    };

    typedef RefCountPtr<IFramebuffer> FramebufferHandle;

    //////////////////////////////////////////////////////////////////////////
    // Binding Layouts
    //////////////////////////////////////////////////////////////////////////

    // identifies the underlying resource type in a binding
    enum class ResourceType : uint8_t
    {
        Texture_SRV,
        Texture_UAV,
        Buffer_SRV,
        Buffer_UAV,
        ConstantBuffer,
        VolatileConstantBuffer,
        Sampler,
        RayTracingAccelStruct,
        StructuredBuffer_SRV,
        StructuredBuffer_UAV,
    };

    static constexpr uint32_t MaxBindingLayouts = 5;
    static constexpr uint32_t MaxBindingsPerStage = 128;
    static constexpr uint32_t MaxVolatileConstantBuffersPerLayout = 6;

    struct BindingLayoutItem
    {
        uint32_t slot : 16;
        ResourceType type : 8;
        uint32_t registerSpace : 8;

        bool operator ==(const BindingLayoutItem& b) const { return slot == b.slot && type == b.type; }
        bool operator !=(const BindingLayoutItem& b) const { return !(*this == b); }
    };

    typedef static_vector<BindingLayoutItem, MaxBindingsPerStage> StageBindingLayoutDesc;

    struct BindingLayoutDesc
    {
        StageBindingLayoutDesc VS;
        StageBindingLayoutDesc HS;
        StageBindingLayoutDesc DS;
        StageBindingLayoutDesc GS;
        StageBindingLayoutDesc PS;

        StageBindingLayoutDesc CS;

        StageBindingLayoutDesc ALL;
    };

    class IBindingLayout : public IResource
    {
    public:
        virtual const BindingLayoutDesc& GetDesc() const = 0;
    };

    typedef RefCountPtr<IBindingLayout> BindingLayoutHandle;

    //////////////////////////////////////////////////////////////////////////
    // Binding Sets
    //////////////////////////////////////////////////////////////////////////

    struct BindingSetItem
    {
        uint32_t slot : 16;
        ResourceType type : 8;
        uint32_t registerSpace : 8;

        void *resourceHandle;   // ITexture*, IBuffer* or ISampler*

        Format format; // Texture_SRV, Texture_UAV, Buffer_SRV, Buffer_UAV

        union 
        {
            TextureSubresourceSet subresources; // Texture_SRV, Texture_UAV
            BufferRange range; // Buffer_SRV, Buffer_UAV
        };

        BindingSetItem()
        { }

        // Helper functions for strongly typed initialization

        static inline BindingSetItem Texture_SRV(uint32_t slot, ITexture* texture, Format format = Format::UNKNOWN, TextureSubresourceSet subresources = AllSubresources, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Texture_SRV;
            result.registerSpace = registerSpace;
            result.resourceHandle = texture;
            result.format = format;
            result.subresources = subresources;
            return result;
        }

        static inline BindingSetItem Texture_UAV(uint32_t slot, ITexture* texture, Format format = Format::UNKNOWN, TextureSubresourceSet subresources = TextureSubresourceSet(0, 1, 0, TextureSubresourceSet::AllArraySlices), uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Texture_UAV;
            result.registerSpace = registerSpace;
            result.resourceHandle = texture;
            result.format = format;
            result.subresources = subresources;
            return result;
        }

        static inline BindingSetItem Buffer_SRV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Buffer_SRV;
            result.registerSpace = registerSpace;
            result.resourceHandle = buffer;
            result.format = format;
            result.range = range;
            return result;
        }

        static inline BindingSetItem Buffer_UAV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Buffer_UAV;
            result.registerSpace = registerSpace;
            result.resourceHandle = buffer;
            result.format = format;
            result.range = range;
            return result;
        }

        static inline BindingSetItem ConstantBuffer(uint32_t slot, IBuffer* buffer, uint32_t registerSpace = 0)
        {
            bool isVolatile = buffer && buffer->GetDesc().isVolatile;

            BindingSetItem result;
            result.slot = slot;
            result.type = isVolatile ? ResourceType::VolatileConstantBuffer : ResourceType::ConstantBuffer;
            result.registerSpace = registerSpace;
            result.resourceHandle = buffer;
            result.format = Format::UNKNOWN;
            result.range = EntireBuffer;
            return result;
        }

        static inline BindingSetItem Sampler(uint32_t slot, ISampler* sampler, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::Sampler;
            result.registerSpace = registerSpace;
            result.resourceHandle = sampler;
            result.format = Format::UNKNOWN;
            return result;
        }

        static inline BindingSetItem RayTracingAccelStruct(uint32_t slot, rt::IAccelStruct* as, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::RayTracingAccelStruct;
            result.registerSpace = registerSpace;
            result.resourceHandle = as;
            result.format = Format::UNKNOWN;
            return result;
        }

        static inline BindingSetItem StructuredBuffer_SRV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::StructuredBuffer_SRV;
            result.registerSpace = registerSpace;
            result.resourceHandle = buffer;
            result.format = format;
            result.range = range;
            return result;
        }

        static inline BindingSetItem StructuredBuffer_UAV(uint32_t slot, IBuffer* buffer, Format format = Format::UNKNOWN, BufferRange range = EntireBuffer, uint32_t registerSpace = 0)
        {
            BindingSetItem result;
            result.slot = slot;
            result.type = ResourceType::StructuredBuffer_UAV;
            result.registerSpace = registerSpace;
            result.resourceHandle = buffer;
            result.format = format;
            result.range = range;
            return result;
        }
    };

    // describes the resource bindings for a single pipeline stage
    typedef static_vector<BindingSetItem, MaxBindingsPerStage> StageBindingSetDesc;

    // describes a set of bindings across all stages of the pipeline
    // (not all bindings need to be present in the set, but the set must be defined by a single BindingSetItem object)
    struct BindingSetDesc
    {
        StageBindingSetDesc VS;
        StageBindingSetDesc HS;
        StageBindingSetDesc DS;
        StageBindingSetDesc GS;
        StageBindingSetDesc PS;

        StageBindingSetDesc CS;

        StageBindingSetDesc ALL;

        // Enables automatic liveness tracking of this binding set by nvrhi command lists.
        // By setting trackLiveness to false, you take the responsibility of not releasing it 
        // until all rendering commands using the binding set are finished.
        bool trackLiveness = true;
    };

    class IBindingSet : public IResource
    {
    public:
        virtual const BindingSetDesc& GetDesc() const = 0;
        virtual IBindingLayout* GetLayout() const = 0;
    };

    typedef RefCountPtr<IBindingSet> BindingSetHandle;

    //////////////////////////////////////////////////////////////////////////
    // Draw State
    //////////////////////////////////////////////////////////////////////////

    struct PrimitiveType
    {
        enum Enum
        {
            POINT_LIST,
            LINE_LIST,
            TRIANGLE_LIST,
            TRIANGLE_STRIP,
            PATCH_1_CONTROL_POINT,
            PATCH_3_CONTROL_POINT,
            PATCH_4_CONTROL_POINT
        };
    };

    struct SinglePassStereoState
    {
        bool enabled = false;
        bool independentViewportMask = false;
        uint16_t renderTargetIndexOffset = 0;

        bool operator ==(const SinglePassStereoState& b) const
        {
            return enabled == b.enabled && independentViewportMask == b.independentViewportMask && renderTargetIndexOffset == b.renderTargetIndexOffset;
        }

        bool operator !=(const SinglePassStereoState& b) const
        {
            return !(*this == b);
        }
    };

    struct RenderState
    {
        ViewportState viewportState;
        BlendState blendState;
        DepthStencilState depthStencilState;
        RasterState rasterState;
        SinglePassStereoState singlePassStereo;
    };

    typedef static_vector<BindingLayoutHandle, MaxBindingLayouts> BindingLayoutVector;

    struct GraphicsPipelineDesc
    {
        enum { MAX_VERTEX_ATTRIBUTE_COUNT = 16 };

        PrimitiveType::Enum primType;
        InputLayoutHandle inputLayout;

        ShaderHandle VS;
        ShaderHandle HS;
        ShaderHandle DS;
        ShaderHandle GS;
        ShaderHandle PS;

        RenderState renderState;

        BindingLayoutVector bindingLayouts;

        GraphicsPipelineDesc()
            : primType(PrimitiveType::TRIANGLE_LIST)
            , inputLayout(nullptr)
        { }
    };

    class IGraphicsPipeline : public IResource 
    {
    public:
        virtual const GraphicsPipelineDesc& GetDesc() const = 0;
        virtual const FramebufferInfo& GetFramebufferInfo() const = 0;
    };

    typedef RefCountPtr<IGraphicsPipeline> GraphicsPipelineHandle;

    struct ComputePipelineDesc
    {
        ShaderHandle CS;

        BindingLayoutVector bindingLayouts;
    };

    class IComputePipeline : public IResource 
    {
    public:
        virtual const ComputePipelineDesc& GetDesc() const = 0;
    };

    typedef RefCountPtr<IComputePipeline> ComputePipelineHandle;

    //////////////////////////////////////////////////////////////////////////
    // Draw and Dispatch
    //////////////////////////////////////////////////////////////////////////

    class IEventQuery : public IResource { };
    typedef RefCountPtr<IEventQuery> EventQueryHandle;

    class ITimerQuery : public IResource { };
    typedef RefCountPtr<ITimerQuery> TimerQueryHandle;

    struct VertexBufferBinding
    {
        IBuffer* buffer = nullptr;
        uint32_t slot;
        uint32_t offset;

        bool operator ==(const VertexBufferBinding& b) const { return buffer == b.buffer && slot == b.slot && offset == b.offset; }
        bool operator !=(const VertexBufferBinding& b) const { return !(*this == b); }
    };

    struct IndexBufferBinding
    {
        IBuffer* handle = nullptr;
        Format format;
        uint32_t offset;

        bool operator ==(const IndexBufferBinding& b) const { return handle == b.handle && format == b.format && offset == b.offset; }
        bool operator !=(const IndexBufferBinding& b) const { return !(*this == b); }
    };

    typedef static_vector<IBindingSet*, MaxBindingLayouts> BindingSetVector;

    struct GraphicsState
    {
        IGraphicsPipeline* pipeline = nullptr;
        IFramebuffer* framebuffer = nullptr;
        ViewportState viewport;

        BindingSetVector bindings;

        static_vector<VertexBufferBinding, GraphicsPipelineDesc::MAX_VERTEX_ATTRIBUTE_COUNT> vertexBuffers;
        IndexBufferBinding indexBuffer;

        IBuffer* indirectParams = nullptr;
    };

    struct DrawArguments
    {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t startIndexLocation;
        uint32_t startVertexLocation;
        uint32_t startInstanceLocation;

        DrawArguments()
            : vertexCount(0)
            , instanceCount(1)
            , startIndexLocation(0)
            , startVertexLocation(0)
            , startInstanceLocation(0)
        { }
    };

    struct ComputeState
    {
        IComputePipeline* pipeline = nullptr;

        BindingSetVector bindings;

        IBuffer* indirectParams = nullptr;
    };

    //////////////////////////////////////////////////////////////////////////
    // Ray Tracing
    //////////////////////////////////////////////////////////////////////////

    namespace rt
    {
        struct PipelineShaderDesc
        {
            std::string exportName;
            ShaderHandle shader;
            BindingLayoutHandle bindingLayout;
        };

        struct PipelineHitGroupDesc
        {
            std::string exportName;
            ShaderHandle closestHitShader;
            ShaderHandle anyHitShader;
            ShaderHandle intersectionShader;
            BindingLayoutHandle bindingLayout;
            bool isProceduralPrimitive = false;
        };

        struct PipelineDesc
        {
            std::vector<PipelineShaderDesc> shaders;
            std::vector<PipelineHitGroupDesc> hitGroups;
            BindingLayoutVector globalBindingLayouts;
            uint32_t maxPayloadSize = 0;
            uint32_t maxAttributeSize = 32; // D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES
            uint32_t maxRecursionDepth = 1;
        };

        class IPipeline;

        class IShaderTable : public IResource
        {
        public:
            virtual void SetRayGenerationShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddMissShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddHitGroup(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual int AddCallableShader(const char* exportName, IBindingSet* bindings = nullptr) = 0;
            virtual void ClearMissShaders() = 0;
            virtual void ClearHitShaders() = 0;
            virtual void ClearCallableShaders() = 0;
            virtual IPipeline* GetPipeline() = 0;
        };

        typedef RefCountPtr<IShaderTable> ShaderTableHandle;

        class IPipeline : public IResource
        {
        public:
            virtual const rt::PipelineDesc& GetDesc() const = 0;
            virtual ShaderTableHandle CreateShaderTable() = 0;
        };

        typedef RefCountPtr<IPipeline> PipelineHandle;

        typedef float AffineTransform[3][4];

        struct GeometryFlags
        {
            enum Enum
            {
                NONE = 0,
                OPAQUE_ = 1,
                NO_DUPLICATE_ANYHIT_INVOCATION = 2
            };
        };

        struct GeometryTrianglesDesc
        {
            Format indexFormat = Format::UNKNOWN;
            Format vertexFormat = Format::UNKNOWN;
            IBuffer* indexBuffer = nullptr;
            IBuffer* vertexBuffer = nullptr;
            uint32_t indexOffset = 0;
            uint32_t vertexOffset = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t vertexStride = 0;
            bool useTransform = false;
            AffineTransform transform;
            GeometryFlags::Enum flags = GeometryFlags::NONE;
        };

        struct InstanceFlags
        {
            enum Enum
            {
                NONE = 0,
                TRIANGLE_CULL_DISABLE = 1,
                TRIANGLE_FRONT_COUNTERCLOCKWISE = 2,
                FORCE_OPAQUE = 4,
                FORCE_NON_OPAQUE = 8
            };
        };

        struct InstanceDesc
        {
            uint32_t instanceID = 0;
            uint32_t instanceContributionToHitGroupIndex = 0;
            uint32_t instanceMask = 0;
            AffineTransform transform = {};
            InstanceFlags::Enum flags = InstanceFlags::NONE;
            IAccelStruct* bottomLevelAS = nullptr;
        };

        struct AccelStructBuildFlags
        {
            enum Enum
            {
                NONE = 0,
                ALLOW_UPDATE = 1,
                ALLOW_COMPACTION = 2,
                PERFER_FAST_TRACE = 4,
                PERFER_FAST_BUILD = 8,
                MINIMIZE_MEMORY = 0x10,
                PERFORM_UPDATE = 0x20
            };
        };

        struct BottomLevelAccelStructDesc
        {
            std::vector<GeometryTrianglesDesc> triangles;
            AccelStructBuildFlags::Enum buildFlags = AccelStructBuildFlags::NONE;
            bool trackLiveness = true;
        };

        struct TopLevelAccelStructDesc
        {
            std::vector<InstanceDesc> instances;
            AccelStructBuildFlags::Enum buildFlags = AccelStructBuildFlags::NONE;
        };

        struct State
        {
            IShaderTable* shaderTable = nullptr;

            BindingSetVector bindings;
        };

        struct DispatchRaysArguments
        {
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t depth = 1;
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Misc
    //////////////////////////////////////////////////////////////////////////

    enum class Feature
    {
        DeferredCommandLists,
        SinglePassStereo,
        RayTracing
    };

    enum class MessageSeverity
    {
        Info,
        Warning,
        Error,
        Fatal
    };

    // IMessageCallback should be implemented by the application.
    class IMessageCallback
    {
        IMessageCallback& operator=(const IMessageCallback& other) = delete;
    protected:
        virtual ~IMessageCallback() {}
    public:
        // NVRHI will call message(...) whenever it needs to signal something.
        // The application is free to ignore the messages, show message boxes, or terminate.
        // 'file' and 'line' parameters can be null.
        virtual void message(MessageSeverity severity, const char* messageText, const char* file = nullptr, int line = 0) = 0;
    };
    
    class IDevice;

    struct CommandListParameters
    {
        // A command list with enableImmediateExecution = true maps to the immediate command list on DX11.
        // Two immediate command lists cannot be open at the same time, which is checked by the validation layer.
        bool enableImmediateExecution = true;

        // Minimum size of memory chunks created to upload data to the device on DX12.
        size_t uploadChunkSize = 64 * 1024;

        // Minimum size of memory chunks created for DXR scratch buffers.
        size_t scratchChunkSize = 64 * 1024;

        // Maximum total memory size used for all DXR scratch buffers owned by this command list.
        size_t scratchMaxMemory = 1024 * 1024 * 1024;
    };
    
    //////////////////////////////////////////////////////////////////////////
    // ICommandList
    //////////////////////////////////////////////////////////////////////////

    class ICommandList : public IResource
    {
        ICommandList& operator=(const ICommandList& other); // undefined
    public:
        virtual void open() = 0;
        virtual void close() = 0;

        // Clears the graphics state of the underlying command list object and resets the state cache.
        virtual void clearState() = 0;

        virtual void clearTextureFloat(ITexture* t, TextureSubresourceSet subresources, const Color& clearColor) = 0;
        virtual void clearDepthStencilTexture(ITexture* t, TextureSubresourceSet subresources, bool clearDepth, float depth, bool clearStencil, uint8_t stencil) = 0;
        virtual void clearTextureUInt(ITexture* t, TextureSubresourceSet subresources, uint32_t clearColor) = 0;

        virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;
        virtual void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) = 0;
        virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice) = 0;
        virtual void writeTexture(ITexture* dest, uint32_t arraySlice, uint32_t mipLevel, const void* data, size_t rowPitch, size_t depthPitch = 0) = 0;
        virtual void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) = 0;

        virtual void writeBuffer(IBuffer* b, const void* data, size_t dataSize, size_t destOffsetBytes = 0) = 0;
        virtual void clearBufferUInt(IBuffer* b, uint32_t clearValue) = 0;
        virtual void copyBuffer(IBuffer* dest, uint32_t destOffsetBytes, IBuffer* src, uint32_t srcOffsetBytes, size_t dataSizeBytes) = 0;

        virtual void setGraphicsState(const GraphicsState& state) = 0;
        virtual void draw(const DrawArguments& args) = 0;
        virtual void drawIndexed(const DrawArguments& args) = 0;
        virtual void drawIndirect(uint32_t offsetBytes) = 0;
        
        virtual void setComputeState(const ComputeState& state) = 0;
        virtual void dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) = 0;
        virtual void dispatchIndirect(uint32_t offsetBytes) = 0;

        virtual void setRayTracingState(const rt::State& state) { (void)state; }
        virtual void dispatchRays(const rt::DispatchRaysArguments& args) { (void)args; }
        
        virtual void buildBottomLevelAccelStruct(rt::IAccelStruct* as, const rt::BottomLevelAccelStructDesc& desc) { (void)as; (void)desc; }
        virtual void buildTopLevelAccelStruct(rt::IAccelStruct* as, const rt::TopLevelAccelStructDesc& desc) { (void)as; (void)desc; }

        virtual void beginTimerQuery(ITimerQuery* query) = 0;
        virtual void endTimerQuery(ITimerQuery* query) = 0;

        // perf markers
        virtual void beginMarker(const char *name) = 0;
        virtual void endMarker() = 0;

        // Tells the D3D12 backend whether UAV barriers should be used for the given texture or buffer between draw calls.
        // A barrier should still be placed before the first draw call in the group and after the last one.
        virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers) = 0;
        virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers) = 0;

        // Informs the command list of the state of a texture subresource or buffer prior to command list execution
        virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits) = 0;
        virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits) = 0;

        // Resource state transitions, produce barriers on DX12
        virtual void endTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Enum stateBits, bool permanent = false) = 0;
        virtual void endTrackingBufferState(IBuffer* buffer, ResourceStates::Enum stateBits, bool permanent = false) = 0;

        // Returns the current tracked state of a texture subresource or a buffer.
        // If state is unknown, returns COMMON.
        virtual ResourceStates::Enum getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel) = 0;
        virtual ResourceStates::Enum getBufferState(IBuffer* buffer) = 0;

        // returns the owning device, does NOT call AddRef on it
        virtual IDevice* getDevice() = 0;
    };

    typedef RefCountPtr<ICommandList> CommandListHandle;

    //////////////////////////////////////////////////////////////////////////
    // IDevice
    //////////////////////////////////////////////////////////////////////////

    class IDevice : public IResource
    {
        IDevice& operator=(const IDevice& other); //undefined
    protected:
        virtual ~IDevice() {};
    public:
        virtual TextureHandle createTexture(const TextureDesc& d) = 0;

        virtual TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) = 0;

        virtual StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode cpuAccess) = 0;
        virtual void *mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode cpuAccess, size_t *outRowPitch) = 0;
        virtual void unmapStagingTexture(IStagingTexture* tex) = 0;

        virtual BufferHandle createBuffer(const BufferDesc& d) = 0;
        // this will wait on any fences required for access to the buffer
        // buffer must have been created with CPU access
        virtual void *mapBuffer(IBuffer* buffer, CpuAccessMode cpuAccess) = 0;
        virtual void unmapBuffer(IBuffer* buffer) = 0;

        virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) = 0;

        virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, const size_t binarySize) = 0;
        virtual ShaderHandle createShaderPermutation(const ShaderDesc& d, const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true) = 0;
        virtual ShaderLibraryHandle createShaderLibrary(const void* binary, const size_t binarySize) = 0;
        virtual ShaderLibraryHandle createShaderLibraryPermutation(const void* blob, const size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true) = 0;
        
        virtual SamplerHandle createSampler(const SamplerDesc& d) = 0;
        
        virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, uint32_t attributeCount, IShader* vertexShader) = 0;
        
        // event queries
        virtual EventQueryHandle createEventQuery(void) = 0;
        virtual void setEventQuery(IEventQuery* query) = 0;
        // note: poll never flushes the command stream
        // calling this in a loop without explicit flushes can cause a deadlock
        virtual bool pollEventQuery(IEventQuery* query) = 0;
        virtual void waitEventQuery(IEventQuery* query) = 0;
        virtual void resetEventQuery(IEventQuery* query) = 0;

        // timer queries
        virtual TimerQueryHandle createTimerQuery(void) = 0;
        // note: poll never flushes the command stream
        // calling this in a loop without explicit flushes can cause a deadlock
        virtual bool pollTimerQuery(ITimerQuery* query) = 0;
        // returns time in seconds
        virtual float getTimerQueryTime(ITimerQuery* query) = 0;
        virtual void resetTimerQuery(ITimerQuery* query) = 0;

        // Returns the API kind that the RHI backend is running on top of.
        virtual GraphicsAPI getGraphicsAPI() = 0;
        
        virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) = 0;
        
        virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* fb) = 0;
        
        virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;

        virtual rt::PipelineHandle createRayTracingPipeline(const rt::PipelineDesc& desc) { (void)desc; return nullptr; }
        
        virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) = 0;

        virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) = 0;

        virtual rt::AccelStructHandle createBottomLevelAccelStruct(const rt::BottomLevelAccelStructDesc& desc) { (void)desc; return nullptr; }
        virtual rt::AccelStructHandle createTopLevelAccelStruct(uint32_t numInstances, rt::AccelStructBuildFlags::Enum buildFlags = rt::AccelStructBuildFlags::NONE) { (void)numInstances; (void)buildFlags; return nullptr; }
        
        // For SLI configurations, call NvAPI_D3D_GetCurrentSLIState and return the value of numAFRGroups;
        // For non-SLI or APIs where SLI is not supported, just return "1".
        virtual uint32_t getNumberOfAFRGroups() = 0;

        // Normally you should just be able to return 0, for non-SLI or call NvAPI_D3D_GetCurrentSLIState.
        // However, if the application is buffering frames this may require some more complex logic to implement.
        // The application is passed the number of AFR groups for validation.
        virtual uint32_t getAFRGroupOfCurrentFrame(uint32_t numAFRGroups) = 0;

        virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) = 0;
        virtual void executeCommandList(ICommandList* commandList) = 0;
        virtual void waitForIdle() = 0;

        // Releases the resources that were referenced in the command lists that have finished executing.
        // IMPORTANT: Call this method at least once per frame.
        virtual void runGarbageCollection() = 0;

        virtual bool queryFeatureSupport(Feature feature) = 0;

        virtual IMessageCallback* getMessageCallback() = 0;
    };

    typedef RefCountPtr<IDevice> DeviceHandle;
}

namespace std
{
    template<> struct hash<nvrhi::TextureSubresourceSet>
    {
        std::size_t operator()(nvrhi::TextureSubresourceSet const& s) const noexcept
        {
            return (std::hash<nvrhi::MipLevel>()(s.baseMipLevel) << 3)
                ^ (std::hash<nvrhi::MipLevel>()(s.numMipLevels) << 2)
                ^ (std::hash<nvrhi::ArraySlice>()(s.baseArraySlice) << 1)
                ^ std::hash<nvrhi::ArraySlice>()(s.numArraySlices);
        }
    };

    template<> struct hash<nvrhi::BufferRange>
    {
        std::size_t operator()(nvrhi::BufferRange const& s) const noexcept
        {
            return (std::hash<uint32_t>()(s.byteOffset) << 1)
                ^ std::hash<uint32_t>()(s.byteSize);
        }
    };

    template<> struct hash<nvrhi::FramebufferInfo>
    {
        std::size_t operator()(nvrhi::FramebufferInfo const& s) const noexcept
        {
            size_t value = 0;
            for (auto format : s.colorFormats) 
                value = (value << 1) ^ std::hash<nvrhi::Format>()(format);
            return (value << 4)
                ^ (std::hash<nvrhi::Format>()(s.depthFormat) << 4)
                ^ (std::hash<uint32_t>()(s.width) << 3)
                ^ (std::hash<uint32_t>()(s.height) << 2)
                ^ (std::hash<uint32_t>()(s.sampleCount) << 1)
                ^ std::hash<uint32_t>()(s.sampleQuality);
        }
    };
}

#if __linux__ // nerf'd SAL annotations
#undef _Inout_
#endif // __linux__

#endif // GFSDK_NVRHI_H_
