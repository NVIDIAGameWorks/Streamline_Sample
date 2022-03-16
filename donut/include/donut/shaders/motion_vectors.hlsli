
#include "view_cb.h"

float2 GetMotionVector(float2 svPosition, float3 prevWorldPos, PlanarViewConstants view, PlanarViewConstants viewPrev)
{
    float4 clipPos = mul(float4(prevWorldPos, 1), viewPrev.matWorldToClip);
    if (clipPos.w <= 0)
        return 0;

    clipPos.xyz /= clipPos.w;
    float2 prevWindowPos = clipPos.xy * viewPrev.clipToWindowScale + viewPrev.clipToWindowBias;

    return prevWindowPos - svPosition + (view.pixelOffset - viewPrev.pixelOffset);
}
