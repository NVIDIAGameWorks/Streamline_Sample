//----------------------------------------------------------------------------------
// File:        Scene.cpp
// SDK Version: 2.0
// Email:       vrsupport@nvidia.com
// Site:        http://developer.nvidia.com/
//
// Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------------

#include <iterator>
#include <string.h>

#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShadowMap.h>
#include <donut/engine/View.h>

using namespace donut::math;
#include <donut/shaders/light_cb.h>

using namespace donut::engine;

void Light::FillLightConstants(LightConstants& lightConstants) const
{
    lightConstants.color = color;
    lightConstants.shadowCascades = int4(-1);
    lightConstants.perObjectShadows = int4(-1);
    if (shadowMap)
        lightConstants.outOfBoundsShadow = shadowMap->IsLitOutOfBounds() ? 1.f : 0.f;
    else
        lightConstants.outOfBoundsShadow = 1.f;
}

void DirectionalLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Directional;
    lightConstants.direction = normalize(direction);
    float clampedAngularSize = clamp(angularSize, 0.1f, 90.f);
    lightConstants.angularSizeOrInvRange = dm::radians(clampedAngularSize);
    lightConstants.radiance = irradiance / (1.f - cosf(dm::radians(clampedAngularSize) * 0.5f));
}

float square(float x) { return x * x; }

void SpotLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Spot;
    lightConstants.direction = normalize(direction);
    lightConstants.position = position;
    lightConstants.radius = radius;
    lightConstants.angularSizeOrInvRange = (range <= 0.f) ? 0.f : 1.f / range;
    lightConstants.radiance = flux / (8 * square(dm::PI_f * radius));
    lightConstants.color = color;
    lightConstants.innerAngle = dm::radians(innerAngle);
    lightConstants.outerAngle = dm::radians(outerAngle);
}

void PointLight::FillLightConstants(LightConstants& lightConstants) const
{
    Light::FillLightConstants(lightConstants);

    lightConstants.lightType = LightType_Point;
    lightConstants.position = position;
    lightConstants.radius = radius;
    lightConstants.angularSizeOrInvRange = (range <= 0.f) ? 0.f : 1.f / range;
    lightConstants.radiance = 100.f * flux / (8 * square(dm::PI_f * radius));
    lightConstants.color = color;
}

nvrhi::VertexAttributeDesc VertexAttribute::GetAttributeDesc(Enum attribute, const char* name, uint32_t bufferIndex)
{
    nvrhi::VertexAttributeDesc result = {};
    strncpy(result.name, name, std::size(result.name)-1);
    result.bufferIndex = bufferIndex;
    result.arraySize = 1;

    switch (attribute)
    {
    case VertexAttribute::POSITION:
        result.format = nvrhi::Format::RGB32_FLOAT;
        result.elementStride = sizeof(float3);
        break;
    case VertexAttribute::TEXCOORD1:
    case VertexAttribute::TEXCOORD2:
        result.format = nvrhi::Format::RG32_FLOAT;
        result.elementStride = sizeof(float2);
        break;
    case VertexAttribute::NORMAL:
    case VertexAttribute::TANGENT:
    case VertexAttribute::BITANGENT:
        result.format = nvrhi::Format::RGBA8_SNORM;
        result.elementStride = sizeof(uint32_t);
        break;
    case VertexAttribute::TRANSFORM:
        result.format = nvrhi::Format::RGBA32_FLOAT;
        result.arraySize = 3;
        result.elementStride = sizeof(float3x4) * 2;
        result.isInstanced = true;
        break;
    case VertexAttribute::PREV_TRANSFORM:
        result.format = nvrhi::Format::RGBA32_FLOAT;
        result.arraySize = 3;
        result.elementStride = sizeof(float3x4) * 2;
        result.isInstanced = true;
        result.offset = sizeof(float3x4);
        break;

    default:
        assert(!"unknown attribute");
    }

    return result;
}

bool LightProbe::IsActive() const
{
    if (!enabled)
        return false;
    if (bounds.isempty())
        return false;
    if ((diffuseScale == 0.f || !diffuseMap) && (specularScale == 0.f || !specularMap))
        return false;

    return true;
}

void LightProbe::FillLightProbeConstants(LightProbeConstants& lightProbeConstants) const
{
    lightProbeConstants.diffuseArrayIndex = diffuseArrayIndex;
    lightProbeConstants.specularArrayIndex = specularArrayIndex;
    lightProbeConstants.diffuseScale = diffuseScale;
    lightProbeConstants.specularScale = specularScale;
    lightProbeConstants.mipLevels = specularMap ? static_cast<float>(specularMap->GetDesc().mipLevels) : 0.f;

    for (uint32_t nPlane = 0; nPlane < frustum::PLANES_COUNT; nPlane++)
    {
        lightProbeConstants.frustumPlanes[nPlane] = float4(bounds.planes[nPlane].normal, bounds.planes[nPlane].distance);
    }
}
