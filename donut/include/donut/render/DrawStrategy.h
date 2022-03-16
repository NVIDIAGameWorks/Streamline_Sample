#pragma once

#include <vector>

namespace donut::engine
{
    struct MeshInstance;
    class IMeshSet;
    class IView;
}

namespace donut::render
{
    class BasicDrawStrategy
    {
    protected:
        std::vector<const engine::MeshInstance*> m_MeshInstances;

    public:
        virtual void PrepareForView(const engine::IView* view, std::vector<const engine::MeshInstance*>& instancesToDraw) = 0;
    };

    class InstancedOpaqueDrawStrategy : public BasicDrawStrategy
    {
    public:
        InstancedOpaqueDrawStrategy(engine::IMeshSet& meshSet);

        virtual void PrepareForView(const engine::IView* view, std::vector<const engine::MeshInstance*>& instancesToDraw) override;
    };

    class TransparentDrawStrategy : public BasicDrawStrategy
    {
    public:
        TransparentDrawStrategy(engine::IMeshSet& meshSet);

        virtual void PrepareForView(const engine::IView* view, std::vector<const engine::MeshInstance*>& instancesToDraw) override;
    };
}