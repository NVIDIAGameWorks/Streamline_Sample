#include <donut/core/math/math.h>

namespace donut::math
{

    float3 sphericalToCartesian(radians_f azimuth, radians_f elevation, float distance)
    {
        // azimuth=0, elevation=0 points to (1, 0, 0)
        // positive elevation goes to positive Y
        // positive azimuth goes to positive Z

        float y = sinf(elevation);
        float x = cosf(elevation) * cosf(azimuth);
        float z = cosf(elevation) * sinf(azimuth);

        return float3(x, y, z) * distance;
    }

    float3 sphericalDegreesToCartesian(degrees_f azimuth, degrees_f elevation, float distance)
    {
        return sphericalToCartesian(radians(azimuth), radians(elevation), distance);
    }

    void cartesianToSpherical(const float3& v, radians_f& azimuth, radians_f& elevation, float& distance)
    {
        distance = length(v);

        if (distance == 0)
        {
            azimuth = 0.f;
            elevation = 0.f;
            return;
        }

        float3 vn = v / distance;

        elevation = asinf(vn.y);

        if (vn.z == 0.f && vn.x == 0.f)
            azimuth = 0.f;
        else
            azimuth = atan2f(vn.z, vn.x);
    }

    void cartesianToSphericalDegrees(const float3& v, degrees_f& azimuth, degrees_f& elevation, float& distance)
    {
        radians_f r_azimuth, r_elevation;
        cartesianToSpherical(v, r_azimuth, r_elevation, distance);
        azimuth = degrees(r_azimuth);
        elevation = degrees(r_elevation);
    }

    template<>
    uint vectorToSnorm8(const float2& v)
    {
        float scale = 127.0f / sqrtf(v.x * v.x + v.y * v.y);
        uint32_t x = uint32_t(v.x * scale);
        uint32_t y = uint32_t(v.y * scale);
        return (x & 0xff) | ((y & 0xff) << 8);
    }

    template<>
    uint vectorToSnorm8(const float3& v)
    {
        float scale = 127.0f / sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        uint32_t x = uint32_t(v.x * scale);
        uint32_t y = uint32_t(v.y * scale);
        uint32_t z = uint32_t(v.z * scale);
        return (x & 0xff) | ((y & 0xff) << 8) | ((z & 0xff) << 16);
    }

    template<>
    uint vectorToSnorm8(const float4& v)
    {
        float scale = 127.0f / sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
        uint32_t x = uint32_t(v.x * scale);
        uint32_t y = uint32_t(v.y * scale);
        uint32_t z = uint32_t(v.z * scale);
        uint32_t w = uint32_t(v.w * scale);
        return (x & 0xff) | ((y & 0xff) << 8) | ((z & 0xff) << 16) | ((w & 0xff) << 24);
    }

    template<>
    float2 snorm8ToVector(uint v)
    {
        
        float x = static_cast<signed char>(v & 0xff);
        float y = static_cast<signed char>((v >> 8) & 0xff);
        return max(float2(x, y) / 127.0f, float2(-1.f));
    }

    template<>
    float3 snorm8ToVector(uint v)
    {
        float x = static_cast<signed char>(v & 0xff);
        float y = static_cast<signed char>((v >> 8) & 0xff);
        float z = static_cast<signed char>((v >> 16) & 0xff);
        return max(float3(x, y, z) / 127.0f, float3(-1.f));
    }

    template<>
    float4 snorm8ToVector(uint v)
    {
        float x = static_cast<signed char>(v & 0xff);
        float y = static_cast<signed char>((v >> 8) & 0xff);
        float z = static_cast<signed char>((v >> 16) & 0xff);
        float w = static_cast<signed char>((v >> 24) & 0xff);
        return max(float4(x, y, z, w) / 127.0f, float4(-1.f));
    }
}
