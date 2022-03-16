#pragma once

#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <vector>
#include <memory>

struct PlanarViewConstants;

namespace donut::engine
{
    class IView;

    struct ViewType
    {
        enum Enum
        {
            PLANAR = 0x01,
            STEREO = 0x02,
            CUBEMAP = 0x04
        };
    };

    class ICompositeView
    {
    public:
        virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const = 0;
        virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const = 0;
    };

    class IView : public ICompositeView
    {
    public:
        virtual void FillPlanarViewConstants(PlanarViewConstants& constants) const;

        virtual nvrhi::ViewportState GetViewportState() const = 0;
        virtual nvrhi::TextureSubresourceSet GetSubresources() const = 0;
        virtual bool IsReverseDepth() const = 0;
        virtual bool IsOrthographicProjection() const = 0;
        virtual bool IsStereoView() const = 0;
        virtual bool IsCubemapView() const = 0;
        virtual bool IsMeshVisible(const dm::box3& bbox) const = 0;
        virtual dm::float3 GetViewOrigin() const = 0;
        virtual dm::float3 GetViewDirection() const = 0;
        virtual dm::frustum GetViewFrustum() const = 0;
        virtual dm::frustum GetProjectionFrustum() const = 0;
        virtual dm::affine3 GetViewMatrix() const = 0;
        virtual dm::affine3 GetInverseViewMatrix() const = 0;
        virtual dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const = 0;
        virtual dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const = 0;
        virtual dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const = 0;
        virtual dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const = 0;
        virtual nvrhi::Rect GetViewExtent() const = 0;
        virtual dm::float2 GetPixelOffset() const = 0;

        virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;
    };


    class PlanarView : public IView
    {
    public:
        nvrhi::Viewport m_Viewport;
        nvrhi::Rect m_ScissorRect;
        dm::affine3 m_ViewMatrix;
        dm::float4x4 m_ProjMatrix;
        dm::float4x4 m_PixelOffsetMatrix;
        dm::float4x4 m_PixelOffsetMatrixInv;
        dm::float4x4 m_ViewProjMatrix;
        dm::float4x4 m_ViewProjOffsetMatrix;
        dm::affine3 m_ViewMatrixInv;
        dm::float4x4 m_ProjMatrixInv;
        dm::float4x4 m_ViewProjMatrixInv;
        dm::float4x4 m_ViewProjOffsetMatrixInv;
        dm::frustum m_ViewFrustum;
        dm::frustum m_ProjectionFrustum;
        dm::float2 m_PixelOffset;
        int m_ArraySlice;
        bool m_ReverseDepth;
    public:
        PlanarView();
        void SetViewport(const nvrhi::Viewport& viewport);
        void SetMatrices(const dm::affine3& viewMatrix, const dm::float4x4& projMatrix);
        void SetPixelOffset(const dm::float2 jitter);
        void SetArraySlice(int arraySlice);

        virtual nvrhi::ViewportState GetViewportState() const override;
        virtual nvrhi::TextureSubresourceSet GetSubresources() const override;
        virtual bool IsReverseDepth() const override;
        virtual bool IsOrthographicProjection() const override;
        virtual bool IsStereoView() const override;
        virtual bool IsCubemapView() const override;
        virtual bool IsMeshVisible(const dm::box3& bbox) const override;
        virtual dm::float3 GetViewOrigin() const override;
        virtual dm::float3 GetViewDirection() const override;
        virtual dm::frustum GetViewFrustum() const override;
        virtual dm::frustum GetProjectionFrustum() const override;
        virtual dm::affine3 GetViewMatrix() const override;
        virtual dm::affine3 GetInverseViewMatrix() const override;
        virtual dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override;
        virtual nvrhi::Rect GetViewExtent() const override;
        virtual dm::float2 GetPixelOffset() const override;
    };

    class CompositeView : public ICompositeView
    {
    public:
        std::vector<std::shared_ptr<IView>> m_ChildViews;
    public:

        void AddView(std::shared_ptr<IView> view);

        virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;
    };

    template<typename ChildType>
    class StereoView : public IView
    {
    private:
        typedef IView Super;

    public:
        ChildType LeftView;
        ChildType RightView;

        virtual nvrhi::ViewportState GetViewportState() const override
        {
            nvrhi::ViewportState left = LeftView.GetViewportState();
            nvrhi::ViewportState right = RightView.GetViewportState();

            for (size_t i = 0; i < right.viewports.size(); i++)
                left.addViewport(right.viewports[i]);
            for (size_t i = 0; i < right.scissorRects.size(); i++)
                left.addScissorRect(right.scissorRects[i]);

            return left;
        }

        virtual nvrhi::TextureSubresourceSet GetSubresources() const override
        {
            return LeftView.GetSubresources(); // TODO: not really...
        }

        virtual bool IsReverseDepth() const override
        {
            return LeftView.IsReverseDepth();
        }

        virtual bool IsOrthographicProjection() const override
        {
            return LeftView.IsOrthographicProjection();
        }

        virtual bool IsStereoView() const override
        {
            return true;
        }

        virtual bool IsCubemapView() const override
        {
            return false;
        }

        virtual bool IsMeshVisible(const dm::box3& bbox) const override
        {
            return LeftView.IsMeshVisible(bbox) || RightView.IsMeshVisible(bbox);
        }

        virtual dm::float3 GetViewOrigin() const override
        {
            return (LeftView.GetViewOrigin() + RightView.GetViewOrigin()) * 0.5f;
        }

        virtual dm::float3 GetViewDirection() const override
        {
            return LeftView.GetViewDirection();
        }

        virtual dm::frustum GetViewFrustum() const override
        {
            dm::frustum left = LeftView.GetViewFrustum();
            dm::frustum right = LeftView.GetViewFrustum();

            // not robust but should work for regular stereo views
            left.planes[dm::frustum::RIGHT_PLANE] = right.planes[dm::frustum::RIGHT_PLANE];

            return left;
        }

        virtual dm::frustum GetProjectionFrustum() const override
        {
            dm::frustum left = LeftView.GetProjectionFrustum();
            dm::frustum right = LeftView.GetProjectionFrustum();

            // not robust but should work for regular stereo views
            left.planes[dm::frustum::RIGHT_PLANE] = right.planes[dm::frustum::RIGHT_PLANE];

            return left;
        }

        virtual dm::affine3 GetViewMatrix() const override
        {
            assert(false);
            return dm::affine3::identity();
        }

        virtual dm::affine3 GetInverseViewMatrix() const override
        {
            assert(false);
            return dm::affine3::identity();
        }

        virtual dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        virtual dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        virtual dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        virtual dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override
        {
            assert(false);
            (void)includeOffset;
            return dm::float4x4::identity();
        }

        virtual nvrhi::Rect GetViewExtent() const override
        {
            nvrhi::Rect left = LeftView.GetViewExtent();
            nvrhi::Rect right = RightView.GetViewExtent();

            return nvrhi::Rect(
                std::min(left.minX, right.minX),
                std::max(left.maxX, right.maxX),
                std::min(left.minY, right.minY),
                std::max(left.maxY, right.maxY));
        }

        virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override
        {
            if (supportedTypes & ViewType::STEREO)
                return 1;

            return 2;
        }

        virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override
        {
            if (supportedTypes & ViewType::STEREO)
            {
                assert(index == 0);
                return this;
            }

            assert(index < 2);
            if (index == 0)
                return &LeftView;
            return &RightView;
        }

        virtual dm::float2 GetPixelOffset() const override
        {
            return LeftView.GetPixelOffset();
        }
    };

    typedef StereoView<PlanarView> StereoPlanarView;

    class CubemapView : public IView
    {
    private:
        typedef IView Super;

        PlanarView m_FaceViews[6];
        dm::affine3 m_ViewMatrix;
        dm::affine3 m_ViewMatrixInv;
        dm::float4x4 m_ProjMatrix;
        dm::float4x4 m_ProjMatrixInv;
        dm::float4x4 m_ViewProjMatrix;
        dm::float4x4 m_ViewProjMatrixInv;
        float m_CullDistance;
        float m_NearPlane;
        dm::float3 m_Center;
        dm::box3 m_CullingBox;
        int m_FirstArraySlice;
    public:
        CubemapView();
        void SetTransform(dm::affine3 viewMatrix, float zNear, float cullDistance, bool useReverseInfiniteProjections = true);
        void SetArrayViewports(int resolution, int firstArraySlice);
        float GetNearPlane() const;
        dm::box3 GetCullingBox() const;

        virtual nvrhi::ViewportState GetViewportState() const override;
        virtual nvrhi::TextureSubresourceSet GetSubresources() const override;
        virtual bool IsReverseDepth() const override;
        virtual bool IsOrthographicProjection() const override;
        virtual bool IsStereoView() const override;
        virtual bool IsCubemapView() const override;
        virtual bool IsMeshVisible(const dm::box3& bbox) const override;
        virtual dm::float3 GetViewOrigin() const override;
        virtual dm::float3 GetViewDirection() const override;
        virtual dm::frustum GetViewFrustum() const override;
        virtual dm::frustum GetProjectionFrustum() const override;
        virtual dm::affine3 GetViewMatrix() const override;
        virtual dm::affine3 GetInverseViewMatrix() const override;
        virtual dm::float4x4 GetProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetInverseProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetViewProjectionMatrix(bool includeOffset = true) const override;
        virtual dm::float4x4 GetInverseViewProjectionMatrix(bool includeOffset = true) const override;
        virtual nvrhi::Rect GetViewExtent() const override;
        virtual dm::float2 GetPixelOffset() const override;

        virtual uint32_t GetNumChildViews(ViewType::Enum supportedTypes) const override;
        virtual const IView* GetChildView(ViewType::Enum supportedTypes, uint32_t index) const override;

        static uint32_t* GetCubemapCoordinateSwizzle();
    };
}
