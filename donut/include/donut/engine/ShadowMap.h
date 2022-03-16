#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>

struct ShadowConstants;

namespace donut::engine
{
    class IShadowMap
    {
    public:
        virtual dm::float4x4 GetWorldToUvzwMatrix() const = 0;
        virtual const class ICompositeView& GetView() const = 0;
        virtual nvrhi::ITexture* GetTexture() const = 0;
        virtual uint32_t GetNumberOfCascades() const = 0;
        virtual const IShadowMap* GetCascade(uint32_t index) const = 0;
        virtual uint32_t GetNumberOfPerObjectShadows() const = 0;
        virtual const IShadowMap* GetPerObjectShadow(uint32_t index) const = 0;
        virtual dm::int2 GetTextureSize() const = 0;
        virtual dm::box2 GetUVRange() const = 0;
        virtual dm::float2 GetFadeRangeInTexels() const = 0;
        virtual bool IsLitOutOfBounds() const = 0;
        virtual void FillShadowConstants(ShadowConstants& constants) const = 0;
    };
}