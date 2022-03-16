#pragma once

#include <nvrhi/common/containers.h>
#include <nvrhi/nvrhi.h>

namespace nvrhi {

// a binding key is a combination of a register slot and a resource type
struct ResourceBindingKey
{
    uint32_t slot;
    ResourceType type;

    ResourceBindingKey() = default;
    ResourceBindingKey(uint32_t slot, ResourceType type)
        : slot(slot)
        , type(type)
    { }

    bool operator== (const ResourceBindingKey& other) const
    {
        return slot == other.slot && type == other.type;
    }
};

template <typename T>
using ResourceBindingKey_HashMap = nvrhi::unordered_map<ResourceBindingKey, T>;

// describes a texture binding --- used to manage SRV / VkImageView per texture
struct TextureBindingKey : public TextureSubresourceSet
{
    Format format;
    bool isReadOnlyDSV;

    TextureBindingKey()
    {
    }

    TextureBindingKey(const TextureSubresourceSet& b, Format _format, bool _isReadOnlyDSV = false)
        : TextureSubresourceSet(b)
        , format(_format)
        , isReadOnlyDSV(_isReadOnlyDSV)
    {
    }

    bool operator== (const TextureBindingKey& other) const
    {
        return format == other.format &&
            static_cast<const TextureSubresourceSet&>(*this) == static_cast<const TextureSubresourceSet&>(other) &&
            isReadOnlyDSV == other.isReadOnlyDSV;
    }
};

template <typename T>
using TextureBindingKey_HashMap = nvrhi::unordered_map<TextureBindingKey, T>;

struct BufferBindingKey : public BufferRange
{
    Format format;

    BufferBindingKey()
    { }

    BufferBindingKey(const BufferRange& range, Format _format)
        : BufferRange(range)
        , format(_format)
    { }

    bool operator== (const BufferBindingKey& other) const
    {
        return format == other.format &&
            static_cast<const BufferRange&>(*this) == static_cast<const BufferRange&>(other);
    }
};

} // namespace nvrhi

namespace std
{
    template<> struct hash<nvrhi::ResourceBindingKey>
    {
        std::size_t operator()(nvrhi::ResourceBindingKey const& s) const noexcept
        {
            return std::hash<uint32_t>()(s.slot)
                ^ std::hash<nvrhi::ResourceType>()(s.type);
        }
    };

    template<> struct hash<nvrhi::TextureBindingKey>
    {
        std::size_t operator()(nvrhi::TextureBindingKey const& s) const noexcept
        {
            return std::hash<nvrhi::Format>()(s.format)
                ^ std::hash<nvrhi::TextureSubresourceSet>()(s)
                ^ std::hash<bool>()(s.isReadOnlyDSV);
        }
    };

    template<> struct hash<nvrhi::BufferBindingKey>
    {
        std::size_t operator()(nvrhi::BufferBindingKey const& s) const noexcept
        {
            return std::hash<nvrhi::Format>()(s.format)
                ^ std::hash<nvrhi::BufferRange>()(s);
        }
    };
}