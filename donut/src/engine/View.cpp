
#include <donut/engine/View.h>
#include <algorithm>

using namespace donut::math;
using namespace donut::engine;

#include <donut/shaders/view_cb.h>

void IView::FillPlanarViewConstants(PlanarViewConstants& constants) const
{
    constants.matWorldToView = affineToHomogeneous(GetViewMatrix());
    constants.matViewToClip = GetProjectionMatrix(true);
    constants.matWorldToClip = GetViewProjectionMatrix(true);
    constants.matClipToView = GetInverseProjectionMatrix(true);
    constants.matViewToWorld = affineToHomogeneous(GetInverseViewMatrix());
    constants.matClipToWorld = GetInverseViewProjectionMatrix(true);

    nvrhi::ViewportState viewportState = GetViewportState();
    const nvrhi::Viewport& viewport = viewportState.viewports[0];
    constants.viewportOrigin = float2(viewport.minX, viewport.minY);
    constants.viewportSize = float2(viewport.width(), viewport.height());
    constants.viewportSizeInv = 1.f / constants.viewportSize;

    constants.clipToWindowScale = float2(0.5f * viewport.width(), -0.5f * viewport.height());
    constants.clipToWindowBias = constants.viewportOrigin + constants.viewportSize * 0.5f;

    constants.windowToClipScale = 1.f / constants.clipToWindowScale;
    constants.windowToClipBias = -constants.clipToWindowBias * constants.windowToClipScale;

    constants.cameraDirectionOrPosition = IsOrthographicProjection()
        ? float4(GetViewDirection(), 0.f)
        : float4(GetViewOrigin(), 1.f);

    constants.pixelOffset = GetPixelOffset();
}

PlanarView::PlanarView()
    : m_PixelOffsetMatrix(float4x4::identity())
    , m_PixelOffsetMatrixInv(float4x4::identity())
    , m_ViewMatrix(affine3::identity())
    , m_ProjMatrix(float4x4::identity())
    , m_ArraySlice(0)
{

}

void PlanarView::SetViewport(const nvrhi::Viewport& viewport)
{
    m_Viewport = viewport;
    m_ScissorRect = nvrhi::Rect(int(floor(viewport.minX)), int(ceil(viewport.maxX)), int(floor(viewport.minY)), int(ceil(viewport.maxY)));
}

void PlanarView::SetMatrices(const affine3& viewMatrix, const float4x4& projMatrix)
{
    m_ViewMatrix = viewMatrix;
    m_ProjMatrix = projMatrix;
    m_ViewProjMatrix = affineToHomogeneous(viewMatrix) * projMatrix;
    m_ViewProjOffsetMatrix = m_ViewProjMatrix * m_PixelOffsetMatrix;

    m_ViewMatrixInv = inverse(viewMatrix);
    m_ProjMatrixInv = inverse(projMatrix);
    m_ViewProjMatrixInv = m_ProjMatrixInv * affineToHomogeneous(m_ViewMatrixInv);
    m_ViewProjOffsetMatrixInv = m_PixelOffsetMatrixInv * m_ViewProjMatrixInv;

    m_ReverseDepth = (projMatrix[2][2] == 0.f);
    m_ViewFrustum = frustum(m_ViewProjMatrix, m_ReverseDepth);
    m_ProjectionFrustum = frustum(m_ProjMatrix, m_ReverseDepth);
}

void PlanarView::SetPixelOffset(const float2 offset)
{
    m_PixelOffset = offset;
    m_PixelOffsetMatrix = affineToHomogeneous(translation(float3(2.f * offset.x / (m_Viewport.maxX - m_Viewport.minX), -2.f * offset.y / (m_Viewport.maxY - m_Viewport.minY), 0.f)));
    m_PixelOffsetMatrixInv = inverse(m_PixelOffsetMatrix);
}

void PlanarView::SetArraySlice(int arraySlice)
{
    m_ArraySlice = arraySlice;
}

nvrhi::ViewportState PlanarView::GetViewportState() const
{
    return nvrhi::ViewportState()
        .addViewport(m_Viewport)
        .addScissorRect(m_ScissorRect);
}

nvrhi::TextureSubresourceSet PlanarView::GetSubresources() const
{
    return nvrhi::TextureSubresourceSet(0, 1, m_ArraySlice, 1);
}

bool PlanarView::IsReverseDepth() const
{
    return m_ReverseDepth;
}

bool PlanarView::IsOrthographicProjection() const
{
    return m_ProjMatrix[2][3] == 0.f;
}

bool PlanarView::IsStereoView() const
{
    return false;
}

bool PlanarView::IsCubemapView() const
{
    return false;
}

float3 PlanarView::GetViewOrigin() const
{
    return m_ViewMatrixInv.m_translation;
}

float3 PlanarView::GetViewDirection() const
{
    return m_ViewMatrixInv.m_linear[2];
}

frustum PlanarView::GetViewFrustum() const
{
    return m_ViewFrustum;
}

frustum PlanarView::GetProjectionFrustum() const
{
    return m_ProjectionFrustum;
}

affine3 PlanarView::GetViewMatrix() const
{
    return m_ViewMatrix;
}

affine3 PlanarView::GetInverseViewMatrix() const
{
    return m_ViewMatrixInv;
}

float4x4 PlanarView::GetProjectionMatrix(bool includeOffset) const
{
    return includeOffset ? m_ProjMatrix * m_PixelOffsetMatrix : m_ProjMatrix;
}

float4x4 PlanarView::GetInverseProjectionMatrix(bool includeOffset) const
{
    return includeOffset ? m_PixelOffsetMatrixInv * m_ProjMatrixInv : m_ProjMatrixInv;
}

float4x4 PlanarView::GetViewProjectionMatrix(bool includeOffset) const
{
    return includeOffset ? m_ViewProjOffsetMatrix : m_ViewProjMatrix;
}

float4x4 PlanarView::GetInverseViewProjectionMatrix(bool includeOffset) const
{
    return includeOffset ? m_ViewProjOffsetMatrixInv : m_ViewProjMatrixInv;
}

nvrhi::Rect PlanarView::GetViewExtent() const
{
    return m_ScissorRect;
}

float2 PlanarView::GetPixelOffset() const
{
    return m_PixelOffset;
}

bool PlanarView::IsMeshVisible(const dm::box3& bbox) const
{
    return m_ViewFrustum.intersectsWith(bbox);
}

uint32_t IView::GetNumChildViews(ViewType::Enum supportedTypes) const
{
    (void)supportedTypes;
    return 1;
}

const IView* IView::GetChildView(ViewType::Enum supportedTypes, uint32_t index) const
{
    (void)supportedTypes;
    assert(index == 0);
    return this;
}

void CompositeView::AddView(std::shared_ptr<IView> view)
{
    m_ChildViews.push_back(view);
}

uint32_t CompositeView::GetNumChildViews(ViewType::Enum supportedTypes) const
{
    (void)supportedTypes;
    return uint32_t(m_ChildViews.size());
}

const IView* CompositeView::GetChildView(ViewType::Enum supportedTypes, uint32_t index) const
{
    (void)supportedTypes;
    return m_ChildViews[index].get();
}

static const float3x3 g_CubemapViewMatrices[6] = {
    float3x3(
        0, 0, 1,
        0, 1, 0,
        -1, 0, 0
    ),
    float3x3(
        0, 0, -1,
        0, 1, 0,
        1, 0, 0
    ),
    float3x3(
        1, 0, 0,
        0, 0, 1,
        0, -1, 0
    ),
    float3x3(
        1, 0, 0,
        0, 0, -1,
        0, 1, 0
    ),
    float3x3(
        1, 0, 0,
        0, 1, 0,
        0, 0, 1
    ),
    float3x3(
        -1, 0, 0,
        0, 1, 0,
        0, 0, -1
    )
};

CubemapView::CubemapView()
    : m_ViewMatrix(affine3::identity())
    , m_ViewMatrixInv(affine3::identity())
    , m_ProjMatrix(float4x4::identity())
    , m_ProjMatrixInv(float4x4::identity())
    , m_ViewProjMatrix(float4x4::identity())
    , m_ViewProjMatrixInv(float4x4::identity())
    , m_NearPlane(0.f)
    , m_CullDistance(0.f)
    , m_Center(0.f)
    , m_FirstArraySlice(0)
{
}

void CubemapView::SetTransform(affine3 viewMatrix, float zNear, float cullDistance, bool useReverseInfiniteProjections)
{
    m_ViewMatrix = viewMatrix * scaling(float3(1, 1, -1));
    m_ViewMatrixInv = inverse(m_ViewMatrix);
    m_ProjMatrix = affineToHomogeneous(scaling<float, 3>(1.0f / zNear));
    m_ProjMatrixInv = inverse(m_ProjMatrix);
    m_ViewProjMatrix = affineToHomogeneous(m_ViewMatrix) * m_ProjMatrix;
    m_ViewProjMatrixInv = inverse(m_ViewProjMatrix);
    m_NearPlane = zNear;
    m_CullDistance = cullDistance;
    m_Center = inverse(viewMatrix).m_translation;
    m_CullingBox = box3(m_Center - m_CullDistance, m_Center + m_CullDistance);

    float4x4 faceProjMatrix;
    if (useReverseInfiniteProjections)
        faceProjMatrix = float4x4(
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 0, 1,
            0, 0, zNear, 0
        );
    else
        faceProjMatrix = perspProjD3DStyle(-1.f, 1.f, -1.f, 1.f, zNear, cullDistance);

    for (int face = 0; face < 6; face++)
    {
        affine3 faceViewMatrix = m_ViewMatrix * affine3(g_CubemapViewMatrices[face], float3(0.f));

        m_FaceViews[face].SetMatrices(faceViewMatrix, faceProjMatrix);
    }
}

void CubemapView::SetArrayViewports(int resolution, int firstArraySlice)
{
    m_FirstArraySlice = firstArraySlice;

    for (int face = 0; face < 6; face++)
    {
        m_FaceViews[face].SetViewport(nvrhi::Viewport(float(resolution), float(resolution)));
        m_FaceViews[face].SetArraySlice(face + firstArraySlice);
    }
}

float CubemapView::GetNearPlane() const
{
    return m_NearPlane;
}

box3 CubemapView::GetCullingBox() const
{
    return m_CullingBox;
}

nvrhi::ViewportState CubemapView::GetViewportState() const
{
    nvrhi::ViewportState result;

    for (int face = 0; face < 6; face++)
    {
        result.addViewport(m_FaceViews[face].m_Viewport);
        result.addScissorRect(m_FaceViews[face].m_ScissorRect);
    }

    return result;
}

bool CubemapView::IsMeshVisible(const dm::box3& bbox) const
{
    if (m_CullDistance <= 0)
        return true;

    return m_CullingBox.intersects(bbox);
}

nvrhi::TextureSubresourceSet CubemapView::GetSubresources() const
{
    return nvrhi::TextureSubresourceSet(0, 1, m_FirstArraySlice, 6);
}

bool CubemapView::IsReverseDepth() const
{
    return true;
}

bool CubemapView::IsOrthographicProjection() const
{
    return false;
}

bool CubemapView::IsStereoView() const
{
    return false;
}

bool CubemapView::IsCubemapView() const
{
    return true;
}

float3 CubemapView::GetViewOrigin() const
{
    return m_Center;
}

float3 CubemapView::GetViewDirection() const
{
    assert(false);
    return 0.f;
}

frustum CubemapView::GetViewFrustum() const
{
    box3 b = box3(m_Center - m_CullDistance, m_Center + m_CullDistance);

    frustum f;
    f.planes[frustum::LEFT_PLANE] = plane(1.f, 0.f, 0.f, b.m_mins.x);
    f.planes[frustum::RIGHT_PLANE] = plane(-1.f, 0.f, 0.f, -b.m_maxs.x);
    f.planes[frustum::BOTTOM_PLANE] = plane(0.f, 1.f, 0.f, b.m_mins.y);
    f.planes[frustum::TOP_PLANE] = plane(0.f, -1.f, 0.f, -b.m_maxs.y);
    f.planes[frustum::NEAR_PLANE] = plane(0.f, 0.f, 1.f, b.m_mins.z);
    f.planes[frustum::FAR_PLANE] = plane(0.f, 0.f, -1.f, -b.m_maxs.z);

    return f;
}

dm::frustum donut::engine::CubemapView::GetProjectionFrustum() const
{
    box3 b = box3(-m_CullDistance, m_CullDistance);

    frustum f;
    f.planes[frustum::LEFT_PLANE] = plane(1.f, 0.f, 0.f, b.m_mins.x);
    f.planes[frustum::RIGHT_PLANE] = plane(-1.f, 0.f, 0.f, -b.m_maxs.x);
    f.planes[frustum::BOTTOM_PLANE] = plane(0.f, 1.f, 0.f, b.m_mins.y);
    f.planes[frustum::TOP_PLANE] = plane(0.f, -1.f, 0.f, -b.m_maxs.y);
    f.planes[frustum::NEAR_PLANE] = plane(0.f, 0.f, 1.f, b.m_mins.z);
    f.planes[frustum::FAR_PLANE] = plane(0.f, 0.f, -1.f, -b.m_maxs.z);

    return f;
}

affine3 CubemapView::GetViewMatrix() const
{
    return m_ViewMatrix;
}

affine3 CubemapView::GetInverseViewMatrix() const
{
    return m_ViewMatrixInv;
}

float4x4 CubemapView::GetProjectionMatrix(bool includeOffset) const
{
    (void)includeOffset;
    return m_ProjMatrix;
}

float4x4 CubemapView::GetInverseProjectionMatrix(bool includeOffset) const
{
    (void)includeOffset;
    return m_ProjMatrixInv;
}

float4x4 CubemapView::GetViewProjectionMatrix(bool includeOffset) const
{
    (void)includeOffset;
    return m_ViewProjMatrix;
}

float4x4 CubemapView::GetInverseViewProjectionMatrix(bool includeOffset) const
{
    (void)includeOffset;
    return m_ViewProjMatrixInv;
}

nvrhi::Rect CubemapView::GetViewExtent() const
{
    return m_FaceViews[0].GetViewExtent();
}

float2 CubemapView::GetPixelOffset() const
{
    return 0.f;
}

uint32_t CubemapView::GetNumChildViews(ViewType::Enum supportedTypes) const
{
    if (supportedTypes & ViewType::CUBEMAP)
        return 1;

    return 6;
}

const IView* CubemapView::GetChildView(ViewType::Enum supportedTypes, uint32_t index) const
{
    if (supportedTypes & ViewType::CUBEMAP)
        return this;

    assert(index < 6);
    return &m_FaceViews[index];
}


typedef enum _NV_SWIZZLE_MODE
{
    NV_SWIZZLE_POS_X = 0,
    NV_SWIZZLE_NEG_X = 1,
    NV_SWIZZLE_POS_Y = 2,
    NV_SWIZZLE_NEG_Y = 3,
    NV_SWIZZLE_POS_Z = 4,
    NV_SWIZZLE_NEG_Z = 5,
    NV_SWIZZLE_POS_W = 6,
    NV_SWIZZLE_NEG_W = 7
}NV_SWIZZLE_MODE;

typedef enum _NV_SWIZZLE_OFFSET
{
    NV_SWIZZLE_OFFSET_X = 0,
    NV_SWIZZLE_OFFSET_Y = 4,
    NV_SWIZZLE_OFFSET_Z = 8,
    NV_SWIZZLE_OFFSET_W = 12
}NV_SWIZZLE_OFFSET;

static const uint32_t g_CubemapCoordinateSwizzle[16] = {
    (NV_SWIZZLE_NEG_Z << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_POS_Y << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_POS_X << NV_SWIZZLE_OFFSET_W),
    (NV_SWIZZLE_POS_Z << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_POS_Y << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_NEG_X << NV_SWIZZLE_OFFSET_W),
    (NV_SWIZZLE_POS_X << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_NEG_Z << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_POS_Y << NV_SWIZZLE_OFFSET_W),
    (NV_SWIZZLE_POS_X << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_POS_Z << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_NEG_Y << NV_SWIZZLE_OFFSET_W),
    (NV_SWIZZLE_POS_X << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_POS_Y << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_POS_Z << NV_SWIZZLE_OFFSET_W),
    (NV_SWIZZLE_NEG_X << NV_SWIZZLE_OFFSET_X) | (NV_SWIZZLE_POS_Y << NV_SWIZZLE_OFFSET_Y) | (NV_SWIZZLE_POS_W << NV_SWIZZLE_OFFSET_Z) | (NV_SWIZZLE_NEG_Z << NV_SWIZZLE_OFFSET_W),
    0
};

uint32_t* CubemapView::GetCubemapCoordinateSwizzle()
{
    return (uint32_t*)g_CubemapCoordinateSwizzle;
}
