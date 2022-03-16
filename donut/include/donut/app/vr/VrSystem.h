#pragma once

#include <donut/core/math/math.h>

struct IDXGIAdapter;
struct ID3D11Device;
struct ID3D12CommandQueue;
struct ID3D11Texture2D;
struct ID3D11Resource;
struct ID3D12Resource;
struct _LUID;

namespace donut::app
{
    enum struct VrResult
    {
        OK,
        NoDevice,
        WrongAdapter,
        DisplayLost,
        OtherError
    };

    enum struct VrApi
    {
        OculusVR,
    };

    class VrSystem
    {
    public:
        virtual ~VrSystem() {}

        static IDXGIAdapter* GetRequiredAdapter();
        static VrResult CreateD3D11(ID3D11Device* pDevice, VrSystem** ppSystem);
        static VrResult CreateD3D12(const _LUID* pAdapterLuid, ID3D12CommandQueue* pCommandQueue, VrSystem** ppSystem);

        virtual VrApi GetApi() const = 0;

        virtual VrResult AcquirePose() = 0;
        virtual VrResult PresentOculusVR() { return VrResult::OtherError; }
        virtual void SetOculusPerfHudMode(int mode) { (void)mode; }
        virtual void Recenter() = 0;

        virtual int GetSwapChainBufferCount() const { return 0; }
        virtual int GetCurrentSwapChainBuffer() const { return 0; }
        virtual ID3D11Texture2D* GetSwapChainBufferD3D11(int index) const { (void)index; return nullptr; }
        virtual ID3D12Resource* GetSwapChainBufferD3D12(int index) const { (void)index; return nullptr; }

        virtual dm::int2 GetSwapChainSize() const = 0;
        virtual dm::float4x4 GetProjectionMatrix(int eye, float zNear, float zFar) const = 0;
        virtual dm::float4x4 GetReverseProjectionMatrix(int eye, float zNear) const = 0;
        virtual dm::affine3 GetEyeToOriginTransform(int eye) const = 0;
    };
}