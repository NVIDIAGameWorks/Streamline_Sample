#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <nvrhi/vulkan/vulkan_ext.h>

#include <nvrhi/nvrhi.h>

#include <nvrhi/common/alloc.h>
#include <nvrhi/common/containers.h>
#include <nvrhi/common/objectpool.h>

namespace nvrhi 
{
namespace vulkan 
{

    class Texture;
    class StagingTexture;
    class InputLayout;
    class Buffer;
    class Shader;
    class Sampler;
    class Framebuffer;
    class GraphicsPipeline;
    class ComputePipeline;
    class ResourceBindingSet;
    class EvenetQuery;
    class TimerQuery;
    class Marker;

}
}

#include <nvrhi/vulkan/constants.h>
#include <nvrhi/vulkan/context.h>
#include <nvrhi/vulkan/sync.h>
#include <nvrhi/vulkan/queue.h>
#include <nvrhi/vulkan/resources.h>
#include <nvrhi/vulkan/allocator.h>

#include <nvrhi/vulkan/renderer.h>
