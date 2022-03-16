#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>

#include <nvrhi/nvrhi.h>

namespace nvrhi {
namespace vulkan {

vk::Format convertFormat(nvrhi::Format format);
vk::SamplerAddressMode convertSamplerAddressMode(nvrhi::SamplerDesc::WrapMode mode);
uint32_t formatElementSizeBits(nvrhi::Format format);
uint32_t formatBlockSize(nvrhi::Format format);

} // namespace vk
} // namespace nvrhi
