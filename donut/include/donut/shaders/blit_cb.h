
#ifndef BLIT_CB_H
#define BLIT_CB_H

struct BlitConstants
{
    float2  sourceOrigin;
    float2  sourceSize;

    float2  targetOrigin;
    float2  targetSize;

    float2  padding;
    uint    sourceSlice;
    float 	sharpenFactor;
};

#endif // BLIT_CB_H