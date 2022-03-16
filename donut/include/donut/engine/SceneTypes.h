#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <unordered_map>

struct LightConstants;
struct LightProbeConstants;

namespace donut::engine
{
    enum class TextureAlphaMode
    {
        UNKNOWN = 0,
        STRAIGHT = 1,
        PREMULTIPLIED = 2,
        OPAQUE_ = 3,
        CUSTOM = 4,
    };

    struct LoadedTexture
    {
        nvrhi::TextureHandle texture;
        TextureAlphaMode alphaMode = TextureAlphaMode::UNKNOWN;
        uint32_t originalBitsPerPixel = 0;
        std::string relativePath;
    };

    struct VertexAttribute
    {
        enum Enum
        {
            NONE = 0,

            POSITION = 0x01,
            TEXCOORD1 = 0x02,
            TEXCOORD2 = 0x04,
            NORMAL = 0x08,
            TANGENT = 0x10,
            BITANGENT = 0x20,
            TRANSFORM = 0x40,
            PREV_TRANSFORM = 0x80,

            ALL = 0xFF
        };

        static nvrhi::VertexAttributeDesc GetAttributeDesc(VertexAttribute::Enum attribute, const char* name, uint32_t bufferIndex);
    };


    struct SceneLoadingStats
    {
        std::atomic<uint32_t> ObjectsTotal;
        std::atomic<uint32_t> ObjectsLoaded;
    };

    enum MaterialDomain
    {
        MD_OPAQUE,
        MD_ALPHA_TESTED,
        MD_TRANSPARENT
    };

    struct Material
    {
        std::string name;
        MaterialDomain domain = MD_OPAQUE;
        std::shared_ptr<LoadedTexture> diffuseTexture;
        std::shared_ptr<LoadedTexture> specularTexture;
        std::shared_ptr<LoadedTexture> normalsTexture;
        std::shared_ptr<LoadedTexture> emissiveTexture;
        nvrhi::BufferHandle materialConstants;
        dm::float3 diffuseColor = 0.f;
        dm::float3 specularColor = 0.f;
        dm::float3 emissiveColor = 0.f;
        float shininess = 0.f;
        float opacity = 1.f;
        uint32_t specularTextureType = 0;
        int materialID = 0;
    };


    struct InputAssemblerBindings
    {
        VertexAttribute::Enum vertexBuffers[16];
        uint32_t numVertexBuffers;
    };

    struct BufferGroup
    {
        nvrhi::BufferHandle indexBuffer;
        std::unordered_map<VertexAttribute::Enum, nvrhi::BufferHandle> vertexBuffers;
    };

    struct MeshInfo
    {
        std::string name;
        Material* material;
        BufferGroup* buffers;
        dm::box3 objectSpaceBounds;
        uint32_t indexOffset;
        uint32_t vertexOffset;
        uint32_t numIndices;
        uint32_t numVertices;
    };

    struct MeshInstance
    {
        std::string name;
        MeshInfo* mesh;
        dm::affine3 localTransform;
        dm::affine3 previousTransform;
        dm::box3 transformedBounds;
        dm::float3 transformedCenter;
        uint32_t instanceOffset;
    };

    class IMeshSet
    {
    public:
        virtual const std::vector<MeshInfo*>& GetMeshes() const = 0;
        virtual const std::vector<MeshInstance*>& GetMeshInstances() const = 0;
        virtual const std::vector<Material*>& GetMaterials() const = 0;
    };

    enum struct LightType
    {
        DIRECTIONAL = 1,
        SPOT = 2,
        POINT = 3
    };

    class IShadowMap;

    class Light
    {
    public:
        std::string name;
        std::shared_ptr<IShadowMap> shadowMap;
        dm::rgb color = dm::colors::white;

        virtual LightType GetLightType() = 0;
        virtual void FillLightConstants(LightConstants& lightConstants) const;
    };

    class DirectionalLight : public Light
    {
    public:
        dm::float3 direction = dm::float3(0.f, -1.f, 0.f);
        float irradiance = 1.f;         // Target irradiance of surfaces lit by this light; multiplied by `color`.
        dm::degrees_f angularSize = 1.f;    // Angular size of the light source. Must be greater than 0. Irradiance does not depend on angular size.
        std::vector<std::shared_ptr<IShadowMap>> perObjectShadows;

        virtual LightType GetLightType() override { return LightType::DIRECTIONAL; }
        virtual void FillLightConstants(LightConstants& lightConstants) const override;
    };

    class SpotLight : public Light
    {
    public:
        dm::float3 position = 0.f;
        dm::float3 direction = dm::float3(0.f, -1.f, 0.f);
        float flux = 1.f;       // Overall amount of light emitted by the light if it was omnidirectional; multiplied by `color`.
        float radius = 0.01f;   // Radius of the light sphere, in world units. Must be greater than 0.
        float range = 10.f;     // Range of influence for the light. 0 means infinite range.
        dm::degrees_f innerAngle = 60.f;    // Apex angle of the full-bright cone; constant intensity inside the inner cone, smooth falloff between inside and outside.
        dm::degrees_f outerAngle = 90.f;    // Apex angle of the light cone - everything outside of that cone is dark.

        virtual LightType GetLightType() override { return LightType::SPOT; }
        virtual void FillLightConstants(LightConstants& lightConstants) const override;
    };

    class PointLight : public Light
    {
    public:
        dm::float3 position = 0.f;
        float flux = 1.f;       // Overall amount of light emitted by the light; multiplied by `color`.
        float radius = 0.2f;    // Radius of the light sphere, in world units. Must be greater than 0.
        float range = 10.f;     // Range of influence for the light. 0 means infinite range.

        virtual LightType GetLightType() override { return LightType::POINT; }
        virtual void FillLightConstants(LightConstants& lightConstants) const override;
    };

    struct CameraPreset
    {
        std::string name;
        dm::float3 position = 0.f;
        dm::float3 lookAt = dm::float3(1.f, 0.f, 0.f);
        dm::float3 up = dm::float3(0.f, 1.f, 0.f);
        dm::degrees_f verticalFov = 60.f;
    };

    struct LightProbe
    {
        std::string name;
        nvrhi::TextureHandle diffuseMap;
        nvrhi::TextureHandle specularMap;
        nvrhi::TextureHandle environmentBrdf;
        uint32_t diffuseArrayIndex = 0;
        uint32_t specularArrayIndex = 0;
        float diffuseScale = 1.f;
        float specularScale = 1.f;
        bool enabled = true;
        dm::frustum bounds = dm::frustum::infinite();

        bool IsActive() const;
        void FillLightProbeConstants(LightProbeConstants& lightProbeConstants) const;
    };
}