#pragma once

#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>
#include <nvrhi/nvrhi.h>
#include <memory>

namespace donut::render
{
    class PlanarShadowMap : public engine::IShadowMap
    {
    private:
        nvrhi::TextureHandle m_ShadowMapTexture;
        std::shared_ptr<engine::PlanarView> m_View;
        bool m_IsLitOutOfBounds = false;
        dm::float2 m_FadeRangeTexels = 1.f;
        dm::float2 m_ShadowMapSize;
        dm::float2 m_TextureSize;
        float m_FalloffDistance = 1.f;

    public:
        PlanarShadowMap(
            nvrhi::IDevice* device,
            int resolution,
            nvrhi::Format format);

        PlanarShadowMap(
            nvrhi::IDevice* device,
            nvrhi::ITexture* texture,
            uint32_t arraySlice,
            const nvrhi::Viewport& viewport);

        bool SetupWholeSceneDirectionalLightView(
            const engine::DirectionalLight& light, 
            dm::box3_arg sceneBounds, 
            float fadeRangeWorld = 0.f);

        bool SetupDynamicDirectionalLightView(
            const engine::DirectionalLight& light, 
            dm::float3 anchor, 
            dm::float3 halfShadowBoxSize, 
            dm::float3 preViewTranslation = 0.f,
            float fadeRangeWorld = 0.f);

        void Clear(nvrhi::ICommandList* commandList);

        void SetLitOutOfBounds(bool litOutOfBounds);
        void SetFalloffDistance(float distance);

        std::shared_ptr<engine::PlanarView> GetPlanarView();

        virtual dm::float4x4 GetWorldToUvzwMatrix() const override;
        virtual const engine::ICompositeView& GetView() const override;
        virtual nvrhi::ITexture* GetTexture() const override;
        virtual uint32_t GetNumberOfCascades() const override;
        virtual const IShadowMap* GetCascade(uint32_t index) const override;
        virtual uint32_t GetNumberOfPerObjectShadows() const override;
        virtual const IShadowMap* GetPerObjectShadow(uint32_t index) const override;
        virtual dm::int2 GetTextureSize() const override;
        virtual dm::box2 GetUVRange() const override;
        virtual dm::float2 GetFadeRangeInTexels() const override;
        virtual bool IsLitOutOfBounds() const override;
        virtual void FillShadowConstants(ShadowConstants& constants) const override;
    };
}