
#ifndef BLOOM_CB_H
#define BLOOM_CB_H

struct BloomConstants
{
    float2 pixstep;
    float argumentScale;
    float normalizationScale;

    float3 padding;
    float numSamples;
};

#endif // BLOOM_CB_H