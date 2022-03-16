
#include <donut/render/GeometryPasses.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/DrawStrategy.h>
#include <assert.h>

using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

void donut::render::RenderView(
    nvrhi::ICommandList* commandList, 
    const IView* view, 
    const IView* viewPrev,
    nvrhi::IFramebuffer* framebuffer,
    const MeshInstance* const* pMeshInstances,
    size_t numMeshInstances,
    IGeometryPass& pass, 
    bool materialEvents)
{
    pass.PrepareForView(commandList, view, viewPrev);

    Material* lastMaterial = nullptr;
    BufferGroup* lastBuffers = nullptr;

    bool drawMaterial = true;
    bool stateValid = false;

    const Material* eventMaterial = nullptr;

    nvrhi::GraphicsState graphicsState;
    graphicsState.framebuffer = framebuffer;
    graphicsState.viewport = view->GetViewportState();

    nvrhi::DrawArguments currentDraw;
    currentDraw.instanceCount = 0;

    auto flushDraw = [commandList, materialEvents, &currentDraw, &eventMaterial](const Material* material)
    {
        if (currentDraw.instanceCount == 0)
            return;

        if (materialEvents && material != eventMaterial)
        {
            if (eventMaterial)
                commandList->endMarker();

            if (material->name.empty())
            {
                eventMaterial = nullptr;
            }
            else
            {
                commandList->beginMarker(material->name.c_str());
                eventMaterial = material;
            }
        }

        commandList->drawIndexed(currentDraw);
        currentDraw.instanceCount = 0;
    };

    for(size_t index = 0; index < numMeshInstances; index++)
    {
        const MeshInstance* instance = pMeshInstances[index];
        const MeshInfo* mesh = instance->mesh;

        if (mesh->material == nullptr)
            continue;


        bool newBuffers = mesh->buffers != lastBuffers;
        bool newMaterial = mesh->material != lastMaterial;

        if (newBuffers || newMaterial)
        {
            flushDraw(lastMaterial);
        }

        if (newBuffers)
        {
            pass.SetupInputBuffers(mesh->buffers, graphicsState);

            lastBuffers = mesh->buffers;
            stateValid = false;
        }

        if (newMaterial)
        {
            drawMaterial = pass.SetupMaterial(mesh->material, graphicsState);

            lastMaterial = mesh->material;
            stateValid = false;
        }

        if (drawMaterial)
        {
            if (!stateValid)
            {
                commandList->setGraphicsState(graphicsState);
                stateValid = true;
            }

            nvrhi::DrawArguments args;
            args.vertexCount = mesh->numIndices;
            args.instanceCount = 1;
            args.startVertexLocation = mesh->vertexOffset;
            args.startIndexLocation = mesh->indexOffset;
            args.startInstanceLocation = instance->instanceOffset;

            if (currentDraw.instanceCount > 0 && currentDraw.startIndexLocation == args.startIndexLocation && currentDraw.startInstanceLocation + currentDraw.instanceCount == args.startInstanceLocation)
            {
                currentDraw.instanceCount += 1;
            }
            else
            {
                flushDraw(mesh->material);

                currentDraw = args;
            }
        }
    }

    flushDraw(lastMaterial);

    if (materialEvents && eventMaterial)
        commandList->endMarker();
}

void donut::render::RenderCompositeView(
    nvrhi::ICommandList* commandList, 
    const ICompositeView* compositeView, 
    const ICompositeView* compositeViewPrev, 
    FramebufferFactory& framebufferFactory,
    BasicDrawStrategy& drawStrategy,
    IGeometryPass& pass, 
    const char* passEvent, 
    bool materialEvents)
{
    if (passEvent)
        commandList->beginMarker(passEvent);

    ViewType::Enum supportedViewTypes = pass.GetSupportedViewTypes();

    if (compositeViewPrev)
    {
        // the views must have the same topology
        assert(compositeView->GetNumChildViews(supportedViewTypes) == compositeViewPrev->GetNumChildViews(supportedViewTypes));
    }

    std::vector<const MeshInstance*> instancesToDraw;

    for (uint viewIndex = 0; viewIndex < compositeView->GetNumChildViews(supportedViewTypes); viewIndex++)
    {
        const IView* view = compositeView->GetChildView(supportedViewTypes, viewIndex);
        const IView* viewPrev = compositeViewPrev ? compositeViewPrev->GetChildView(supportedViewTypes, viewIndex) : nullptr;

        assert(view != nullptr);

        drawStrategy.PrepareForView(view, instancesToDraw);

        nvrhi::IFramebuffer* framebuffer = framebufferFactory.GetFramebuffer(*view);

        RenderView(commandList, view, viewPrev, framebuffer, instancesToDraw.data(), instancesToDraw.size(), pass, materialEvents);
    }

    if (passEvent)
        commandList->endMarker();
}
