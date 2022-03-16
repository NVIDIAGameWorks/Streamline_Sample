
#include <donut/render/PlanarShadowMap.h>

using namespace donut::math;
#include <donut/shaders/light_cb.h>

using namespace donut::engine;
using namespace donut::render;

PlanarShadowMap::PlanarShadowMap(
    nvrhi::IDevice* device, 
    int resolution, 
    nvrhi::Format format)
{
    nvrhi::TextureDesc desc;
    desc.width = resolution;
    desc.height = resolution;
    desc.sampleCount = 1;
    desc.isRenderTarget = true;
    desc.isTypeless = true;
    desc.format = format;
    desc.debugName = "ShadowMap";
    desc.useClearValue = true;
    desc.clearValue = nvrhi::Color(1.f);
    desc.initialState = nvrhi::ResourceStates::SHADER_RESOURCE;
    desc.keepInitialState = true;
    desc.dimension = nvrhi::TextureDimension::Texture2DArray;
    m_ShadowMapTexture = device->createTexture(desc);
    
    m_ShadowMapSize = float2(static_cast<float>(resolution));
    m_TextureSize = m_ShadowMapSize;

    m_View = std::make_shared<engine::PlanarView>();
    m_View->SetViewport(nvrhi::Viewport(float(resolution), float(resolution)));
    m_View->SetArraySlice(0);
}

PlanarShadowMap::PlanarShadowMap(
    nvrhi::IDevice* device, 
    nvrhi::ITexture* texture, 
    uint32_t arraySlice, 
    const nvrhi::Viewport& viewport)
{
    m_ShadowMapTexture = texture;
    
    const nvrhi::TextureDesc& textureDesc = m_ShadowMapTexture->GetDesc();
    m_TextureSize = float2(static_cast<float>(textureDesc.width), static_cast<float>(textureDesc.height));
    m_ShadowMapSize = float2(viewport.maxX - viewport.minX, viewport.maxY - viewport.minY);

    m_View = std::make_shared<engine::PlanarView>();
    m_View->SetViewport(viewport);
    m_View->SetArraySlice(arraySlice);
}

bool PlanarShadowMap::SetupWholeSceneDirectionalLightView(const DirectionalLight& light, box3_arg sceneBounds, float fadeRangeWorld)
{
    // Choose a world-space up-vector
    float3 vecUp = { 0.0f, 0.0f, 1.0f };
    if (all(isnear(light.direction.xy(), 0.0f)))
        vecUp = float3(1.0f, 0.0f, 0.0f);

    affine3 viewToWorld = lookatZ(light.direction, vecUp);
    affine3 worldToView = transpose(viewToWorld);

    // Transform scene AABB into view space and recalculate bounds
    box3 boundsView = sceneBounds * worldToView;
    float3 vecDiamOriginal = boundsView.diagonal();

    // Select maximum diameter along X and Y, so that shadow map texels will be square
    float maxXY = max(vecDiamOriginal.x, vecDiamOriginal.y);
    float3 vecDiam = float3(maxXY, maxXY, vecDiamOriginal.z);
    boundsView = boundsView.grow(0.5f * (vecDiam - vecDiamOriginal));

    // Calculate orthogonal projection matrix to fit the scene bounds
    float4x4 projection = orthoProjD3DStyle(
        boundsView.m_mins.x,
        boundsView.m_maxs.x,
        boundsView.m_mins.y,
        boundsView.m_maxs.y,
        -boundsView.m_maxs.z,
        -boundsView.m_mins.z);

    bool viewIsModified = m_View->GetViewMatrix() != worldToView || any(m_View->GetProjectionMatrix(false) != projection);

    m_View->SetMatrices(worldToView, projection);

    m_FadeRangeTexels = clamp(
        float2(fadeRangeWorld * m_ShadowMapSize) / boundsView.diagonal().xy(),
        float2(1.f), 
        m_ShadowMapSize * 0.5f);

    return viewIsModified;
}

bool PlanarShadowMap::SetupDynamicDirectionalLightView(const DirectionalLight& light, float3 anchor, float3 halfShadowBoxSize, float3 preViewTranslation, float fadeRangeWorld)
{
    // Choose a world-space up-vector
    float3 vecUp = { 0.0f, 0.0f, 1.0f };
    if (all(isnear(light.direction.xy(), 0.0f)))
        vecUp = float3(1.0f, 0.0f, 0.0f);

    affine3 viewToWorld = lookatZ(light.direction, vecUp);
    affine3 worldToView = transpose(viewToWorld);

    // Snap the anchor to the texel grid to avoid jitter
    float2 texelSize = float2(2.f) * halfShadowBoxSize.xy() / m_ShadowMapSize;

    float3 anchorView = worldToView.transformPoint(anchor - preViewTranslation);
    anchorView.xy() = float2(round(anchorView.xy() / texelSize)) * texelSize; // why doesn't it compile without float2(...) ?
    float3 center = viewToWorld.transformPoint(anchorView) + preViewTranslation;

    // Make sure we didn't move the anchor too far
    assert(length(center - anchor) < max(texelSize.x, texelSize.y) * 2);

    worldToView = translation(-center) * worldToView;

    // Build the projection matrix
    float4x4 projection = orthoProjD3DStyle(
        -halfShadowBoxSize.x, halfShadowBoxSize.x, 
        -halfShadowBoxSize.y, halfShadowBoxSize.y, 
        -halfShadowBoxSize.z, halfShadowBoxSize.z);

    bool viewIsModified = m_View->GetViewMatrix() != worldToView || any(m_View->GetProjectionMatrix(false) != projection);

    m_View->SetMatrices(worldToView, projection);

    m_FadeRangeTexels = clamp(
        float2(fadeRangeWorld * m_ShadowMapSize) / (halfShadowBoxSize.xy() * 2.f),
        float2(1.f),
        m_ShadowMapSize * 0.5f);

    return viewIsModified;
}

void PlanarShadowMap::SetLitOutOfBounds(bool litOutOfBounds)
{
    m_IsLitOutOfBounds = litOutOfBounds;
}

void PlanarShadowMap::SetFalloffDistance(float distance)
{
    m_FalloffDistance = distance;
}

std::shared_ptr<PlanarView> PlanarShadowMap::GetPlanarView()
{
    return m_View;
}

dm::float4x4 PlanarShadowMap::GetWorldToUvzwMatrix() const
{
    // Calculate alternate matrix that maps to [0, 1] UV space instead of [-1, 1] clip space
    float4x4 matClipToUvzw =
    {
        0.5f,  0,    0, 0,
        0,    -0.5f, 0, 0,
        0,     0,    1, 0,
        0.5f,  0.5f, 0, 1,
    };

    return m_View->m_ViewProjMatrix * matClipToUvzw;
}

const ICompositeView& PlanarShadowMap::GetView() const
{
    return *m_View;
}

nvrhi::ITexture* PlanarShadowMap::GetTexture() const
{
    return m_ShadowMapTexture;
}

uint32_t PlanarShadowMap::GetNumberOfCascades() const
{
    return 1;
}

const IShadowMap* PlanarShadowMap::GetCascade(uint32_t index) const
{
    return this;
}

uint32_t PlanarShadowMap::GetNumberOfPerObjectShadows() const
{
    return 0;
}

const IShadowMap* PlanarShadowMap::GetPerObjectShadow(uint32_t index) const
{
    return nullptr;
}

dm::int2 PlanarShadowMap::GetTextureSize() const
{
    const nvrhi::TextureDesc& textureDesc = m_ShadowMapTexture->GetDesc();
    return int2(textureDesc.width, textureDesc.height);
}

dm::box2 PlanarShadowMap::GetUVRange() const
{
    nvrhi::ViewportState viewportState = m_View->GetViewportState();
    const nvrhi::Viewport& viewport = viewportState.viewports[0];
    float2 topLeft = float2(viewport.minX, viewport.minY);
    float2 bottomRight = float2(viewport.maxX, viewport.maxY);

    return box2(
        (topLeft + 1.f) / m_TextureSize, 
        (bottomRight - 1.f) / m_TextureSize);
}

dm::float2 PlanarShadowMap::GetFadeRangeInTexels() const
{
    return m_FadeRangeTexels;
}

bool PlanarShadowMap::IsLitOutOfBounds() const
{
    return m_IsLitOutOfBounds;
}

void PlanarShadowMap::FillShadowConstants(struct ShadowConstants& constants) const
{
    constants.matWorldToUvzwShadow = GetWorldToUvzwMatrix();
    constants.shadowMapArrayIndex = m_View->GetSubresources().baseArraySlice;
    box2 uvRange = GetUVRange();
    float2 fadeUV = GetFadeRangeInTexels() / m_TextureSize;
    constants.shadowMapCenterUV = uvRange.center();
    constants.shadowFadeBias = uvRange.diagonal() / (2.f * fadeUV);
    constants.shadowFadeScale = -1.f / fadeUV;
    constants.shadowFalloffDistance = m_FalloffDistance;
    constants.shadowMapSizeTexels = float2(GetTextureSize());
    constants.shadowMapSizeTexelsInv = 1.f / constants.shadowMapSizeTexels;
}

void PlanarShadowMap::Clear(nvrhi::ICommandList* commandList)
{
    commandList->clearTextureFloat(m_ShadowMapTexture, m_View->GetSubresources(), nvrhi::Color(1.f));
}
