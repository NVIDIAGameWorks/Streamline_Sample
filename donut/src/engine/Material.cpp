/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <donut/engine/SceneTypes.h>

using namespace donut::math;
#include <donut/shaders/material_cb.h>

namespace donut::engine
{
    static int GetBindlessTextureIndex(const std::shared_ptr<LoadedTexture>& texture)
    {
        return texture ? texture->bindlessDescriptor.Get() : -1;
    }

    void Material::FillConstantBuffer(MaterialConstants& constants) const
    {
        // flags

        constants.flags = 0;

        if (useSpecularGlossModel)
            constants.flags |= MaterialFlags_UseSpecularGlossModel;

        if (baseOrDiffuseTexture && enableBaseOrDiffuseTexture)
            constants.flags |= MaterialFlags_UseBaseOrDiffuseTexture;

        if (metalRoughOrSpecularTexture && enableMetalRoughOrSpecularTexture)
            constants.flags |= MaterialFlags_UseMetalRoughOrSpecularTexture;

        if (emissiveTexture && enableEmissiveTexture)
            constants.flags |= MaterialFlags_UseEmissiveTexture;

        if (normalTexture && enableNormalTexture)
            constants.flags |= MaterialFlags_UseNormalTexture;

        if (occlusionTexture && enableOcclusionTexture)
            constants.flags |= MaterialFlags_UseOcclusionTexture;

        if (transmissionTexture && enableTransmissionTexture)
            constants.flags |= MaterialFlags_UseTransmissionTexture;

        if (doubleSided)
            constants.flags |= MaterialFlags_DoubleSided;

        // free parameters

        constants.domain = (int)domain;
        constants.baseOrDiffuseColor = baseOrDiffuseColor;
        constants.specularColor = specularColor;
        constants.emissiveColor = emissiveColor * emissiveIntensity;
        constants.roughness = roughness;
        constants.metalness = metalness;
        constants.normalTextureScale = normalTextureScale;
        constants.materialID = materialID;
        constants.occlusionStrength = occlusionStrength;
        constants.transmissionFactor = transmissionFactor;
        
        switch (domain)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case MaterialDomain::AlphaBlended:
        case MaterialDomain::TransmissiveAlphaBlended:
            constants.opacity = opacity;
            break;

        case MaterialDomain::Opaque:
        case MaterialDomain::AlphaTested:
        case MaterialDomain::Transmissive:
        case MaterialDomain::TransmissiveAlphaTested:
            constants.opacity = 1.f;
            break;
        default:
            break;
        }
        
        switch(domain)  // NOLINT(clang-diagnostic-switch-enum)
        {
        case MaterialDomain::AlphaTested:
        case MaterialDomain::TransmissiveAlphaTested:
            constants.alphaCutoff = alphaCutoff;
            break;

        case MaterialDomain::AlphaBlended:
        case MaterialDomain::TransmissiveAlphaBlended:
            constants.alphaCutoff = 0.f; // discard only if opacity == 0

        case MaterialDomain::Opaque:
        case MaterialDomain::Transmissive:
            constants.alphaCutoff = -1.f; // never discard
            break;
        default:
            break;
        }

        // bindless textures

        constants.baseOrDiffuseTextureIndex = GetBindlessTextureIndex(baseOrDiffuseTexture);
        constants.metalRoughOrSpecularTextureIndex = GetBindlessTextureIndex(metalRoughOrSpecularTexture);
        constants.normalTextureIndex = GetBindlessTextureIndex(normalTexture);
        constants.emissiveTextureIndex = GetBindlessTextureIndex(emissiveTexture);
        constants.occlusionTextureIndex = GetBindlessTextureIndex(occlusionTexture);
        constants.transmissionTextureIndex = GetBindlessTextureIndex(transmissionTexture);

        constants.padding1 = 0;
        constants.padding2 = 0;
        constants.padding3 = 0;
    }

    bool Material::SetProperty(const std::string& name, const dm::float4& value)
    {
#define FLOAT3_PROPERTY(pname) if (name == #pname) { pname = value.xyz(); dirty = true; return true; }
#define FLOAT_PROPERTY(pname) if (name == #pname) { pname = value.x; dirty = true; return true; }
#define BOOL_PROPERTY(pname) if (name == #pname) { pname = (value.x > 0.5f); dirty = true; return true; }
        FLOAT3_PROPERTY(baseOrDiffuseColor);
        FLOAT3_PROPERTY(specularColor);
        FLOAT3_PROPERTY(emissiveColor);
        FLOAT_PROPERTY(emissiveIntensity);
        FLOAT_PROPERTY(metalness);
        FLOAT_PROPERTY(roughness);
        FLOAT_PROPERTY(opacity);
        FLOAT_PROPERTY(alphaCutoff);
        FLOAT_PROPERTY(transmissionFactor);
        FLOAT_PROPERTY(normalTextureScale);
        FLOAT_PROPERTY(occlusionStrength);
        BOOL_PROPERTY(enableBaseOrDiffuseTexture);
        BOOL_PROPERTY(enableMetalRoughOrSpecularTexture);
        BOOL_PROPERTY(enableNormalTexture);
        BOOL_PROPERTY(enableEmissiveTexture);
        BOOL_PROPERTY(enableOcclusionTexture);
        BOOL_PROPERTY(enableTransmissionTexture);
#undef FLOAT3_PROPERTY
#undef FLOAT_PROPERTY
#undef BOOL_PROPERTY

        return false;
    }
}
