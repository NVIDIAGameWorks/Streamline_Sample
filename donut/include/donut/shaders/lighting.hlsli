
#include "light_cb.h"

static const float PI = 3.1415926535;

float square(float x) { return x*x; }
float4 square(float4 x) { return x*x; }

float Lambert(SurfaceParams surface, float3 lightIncident)
{
    return max(0, -dot(surface.normal, lightIncident));
}

float3 slerp(float3 a, float3 b, float angle, float t)
{
    t = saturate(t);
    float sin1 = sin(angle * t);
    float sin2 = sin(angle * (1 - t));
    float ta = sin1 / (sin1 + sin2);
    float3 result = lerp(a, b, ta);
    return normalize(result);
}

float GGX(SurfaceParams surface, float3 lightIncident, float3 viewIncident, float halfAngularSize)
{
    float3 N = surface.normal;
    float3 V = -viewIncident;
    float3 L = -lightIncident;
    float3 R = reflect(viewIncident, N);

    // Correction of light vector L for spherical / directional area lights.
    // Inspired by "Real Shading in Unreal Engine 4" by B. Karis, 
    // re-formulated to work in spherical coordinates instead of world space.
    float AngleLR = acos(clamp(dot(R, L), -1, 1));

    float3 CorrectedL = (AngleLR > 0) ? slerp(L, R, AngleLR, saturate(halfAngularSize / AngleLR)) : L;
    float3 H = normalize(CorrectedL + V);

    float NdotH = max(0, dot(N, H));
    float NdotL = max(0, dot(N, CorrectedL));
    float NdotV = max(0, dot(N, V));

    float Alpha = max(0.01, square(surface.roughness));

    // Normalization for the widening of D, see the paper referenced above.
    float CorrectedAlpha = saturate(Alpha + 0.5 * tan(halfAngularSize));
    float SphereNormalization = square(Alpha / CorrectedAlpha);

    // GGX / Trowbridge-Reitz NDF with normalization for sphere lights
    float D = square(Alpha) / (PI * square(square(NdotH) * (square(Alpha) - 1) + 1)) * SphereNormalization;
    
    // Schlick model for geometric attenuation
    // The (NdotL * NdotV) term in the numerator is cancelled out by the same term in the denominator of the final result.
    float k = square(surface.roughness + 1) / 8.0;
    float G = 1 / ((NdotL * (1 - k) + k) * (NdotV * (1 - k) + k));

    return D * G / 4;
}

float GetClippedDiskArea(float3 incidentVector, float3 geometryNormal, float angularSize)
{
    float NdotL = -dot(incidentVector, geometryNormal);
    float angleAboveHorizon = PI * 0.5 - acos(NdotL);
    float fractionAboveHorizon = angleAboveHorizon / angularSize;

    // an approximation
    return smoothstep(-0.5, 0.5, fractionAboveHorizon);
}

void ShadeSurface(LightConstants light, SurfaceParams surface, float3 viewIncident, out float o_diffuseRadiance, out float o_specularRadiance)
{
    o_diffuseRadiance = 0;
    o_specularRadiance = 0;

    float3 incidentVector = 0;
    float halfAngularSize = 0;
    float attenuation = 1;
    float spotlight = 1;

    if (light.lightType == LightType_Directional)
    {
        incidentVector = light.direction;

        halfAngularSize = light.angularSizeOrInvRange * 0.5;
    }
    else if (light.lightType == LightType_Spot || light.lightType == LightType_Point)
    {
        float3 lightToSurface = surface.worldPos - light.position;
        float distance = sqrt(dot(lightToSurface, lightToSurface));

        if (light.angularSizeOrInvRange > 0)
        {
            attenuation = square(saturate(1.0 - square(square(distance * light.angularSizeOrInvRange))));

            if (attenuation == 0)
                return;
        }

        float rDistance = 1.0 / distance;
        incidentVector = lightToSurface * rDistance;

        halfAngularSize = atan(min(light.radius * rDistance, 1));
        if (light.lightType == LightType_Spot)
        {
            float LdotD = dot(incidentVector, light.direction);
            float directionAngle = acos(LdotD);
            spotlight = 1 - smoothstep(light.innerAngle, light.outerAngle, directionAngle);

            if (spotlight == 0)
                return;
        }
    }
    else
    {
        return;
    }

    // Geometric shadowing for normal mapped surfaces.
    // Disabled because the geometryNormal information is usually unavailable in deferred shading,
    // and because some meshes misbehave with this shadowing enabled due to odd use of normal maps.
    float clippedDisk = 1; //  GetClippedDiskArea(incidentVector, surface.geometryNormal, halfAngularSize * 2);

    // A good enough approximation for (1 - cos(halfAngularSize)), numerically more accurate for small angular sizes
    float solidAngleOverTwoPi = 0.5 * square(halfAngularSize); 
    
    float diffuseIrradianceOverTwoPi = light.radiance * solidAngleOverTwoPi * clippedDisk * spotlight * attenuation;

    o_diffuseRadiance = Lambert(surface, incidentVector) * diffuseIrradianceOverTwoPi;
    o_specularRadiance = GGX(surface, incidentVector, viewIncident, halfAngularSize) * o_diffuseRadiance * 2 * PI;
}

float GetLightProbeWeight(LightProbeConstants lightProbe, float3 position)
{
    float weight = 1;

    [unroll]
    for (uint nPlane = 0; nPlane < 6; nPlane++)
    {
        float4 plane = lightProbe.frustumPlanes[nPlane];

        float planeResult = plane.w - dot(plane.xyz, position.xyz);

        weight *= saturate(planeResult);
    }

    return weight;
}