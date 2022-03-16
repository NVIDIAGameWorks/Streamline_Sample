#pragma once

#include <donut/engine/View.h>
#include <nvrhi/nvrhi.h>

namespace donut::engine
{
    struct MeshInfo;
    struct MeshInstance;
    struct Material;
    struct BufferGroup;
    class FramebufferFactory;
}

namespace donut::render
{
    class BasicDrawStrategy;

    class IGeometryPass
    {
    public:
        virtual engine::ViewType::Enum GetSupportedViewTypes() const = 0;
        virtual void PrepareForView(nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) = 0;
        virtual bool SetupMaterial(const engine::Material* material, nvrhi::GraphicsState& state) = 0;
        virtual void SetupInputBuffers(const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) = 0;
    };

    void RenderView(
        nvrhi::ICommandList* commandList, 
        const engine::IView* view, 
        const engine::IView* viewPrev, 
        nvrhi::IFramebuffer* framebuffer,
        const engine::MeshInstance* const* pMeshInstances,
        size_t numMeshInstances,
        IGeometryPass& pass,
        bool materialEvents = false);

    void RenderCompositeView(
        nvrhi::ICommandList* commandList,
        const engine::ICompositeView* compositeView,
        const engine::ICompositeView* compositeViewPrev,
        engine::FramebufferFactory& framebufferFactory,
        BasicDrawStrategy& drawStrategy,
        IGeometryPass& pass,
        const char* passEvent = nullptr,
        bool materialEvents = false);
}