#pragma pack_matrix(row_major)

#include <donut/shaders/tonemapping_cb.h>

Buffer<uint> t_Histogram : register(t0);
RWTexture2D<float> u_Exposure : register(u0);

cbuffer c_ToneMapping : register(b0)
{
    ToneMappingConstants g_ToneMapping;
};

#define FIXED_POINT_FRAC_BITS 6
#define FIXED_POINT_FRAC_MULTIPLIER (1 << FIXED_POINT_FRAC_BITS)

// Most of the code below has been shamelessly stolen from UE4 eye adaptation shaders, i.e. PostProcessEyeAdaptation.usf

float GetHistogramBucket(uint BucketIndex)
{
    return float(t_Histogram[BucketIndex]) / FIXED_POINT_FRAC_MULTIPLIER;
}

float ComputeHistogramSum()
{
    float Sum = 0;

    [loop]
    for (uint i = 0; i < HISTOGRAM_BINS; ++i)
    {
        Sum += GetHistogramBucket(i);
    }

    return Sum;
}

float ComputeLuminanceFromHistogramPosition(float HistogramPosition)
{
    return exp2(HistogramPosition * g_ToneMapping.logLuminanceScale + g_ToneMapping.logLuminanceBias);
}

float ComputeAverageLuminaneWithoutOutlier(float MinFractionSum, float MaxFractionSum)
{
    float2 SumWithoutOutliers = 0;

    [loop]
    for (uint i = 0; i < HISTOGRAM_BINS; ++i)
    {
        float LocalValue = GetHistogramBucket(i);

        // remove outlier at lower end
        float Sub = min(LocalValue, MinFractionSum);
        LocalValue = LocalValue - Sub;
        MinFractionSum -= Sub;
        MaxFractionSum -= Sub;

        // remove outlier at upper end
        LocalValue = min(LocalValue, MaxFractionSum);
        MaxFractionSum -= LocalValue;

        float LuminanceAtBucket = ComputeLuminanceFromHistogramPosition(i / (float)HISTOGRAM_BINS);

        SumWithoutOutliers += float2(LuminanceAtBucket, 1) * LocalValue;
    }

    return SumWithoutOutliers.x / max(0.0001f, SumWithoutOutliers.y);
}

float ComputeEyeAdaptationExposure()
{
    float HistogramSum = ComputeHistogramSum();

    float UnclampedAdaptedLuminance = ComputeAverageLuminaneWithoutOutlier(
        HistogramSum * g_ToneMapping.histogramLowPercentile, 
        HistogramSum * g_ToneMapping.histogramHighPercentile);

    float ClampedAdaptedLuminance = clamp(
        UnclampedAdaptedLuminance, 
        g_ToneMapping.minAdaptedLuminance, 
        g_ToneMapping.maxAdaptedLuminance);
    
    return ClampedAdaptedLuminance;
}

float ComputeEyeAdaptation(float OldExposure, float TargetExposure, float FrameTime)
{
    float Diff = OldExposure - TargetExposure;

    float AdaptationSpeed = (Diff < 0) ? g_ToneMapping.eyeAdaptationSpeedUp : g_ToneMapping.eyeAdaptationSpeedDown;

    if (AdaptationSpeed <= 0)
        return TargetExposure;

    float Factor = exp2(-FrameTime * AdaptationSpeed);

    return TargetExposure + Diff * Factor;
}

[numthreads(1, 1, 1)]
void main()
{
    float TargetExposure = ComputeEyeAdaptationExposure();
    float OldExposure = u_Exposure[uint2(0, 0)];

    float SmoothedExposure = ComputeEyeAdaptation(OldExposure, TargetExposure, g_ToneMapping.frameTime);

    u_Exposure[uint2(0, 0)] = SmoothedExposure;
}
