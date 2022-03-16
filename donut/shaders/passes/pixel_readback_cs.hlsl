#include <donut/shaders/pixel_readback_cb.h>

cbuffer c_PixelReadback : register(b0)
{
    PixelReadbackConstants g_PixelReadback;
};

Texture2D<TYPE> t_Source : register(t0);
RWBuffer<TYPE> u_Dest : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    u_Dest[0] = t_Source[g_PixelReadback.pixelPosition.xy];
}
