
#include <donut/app/vr/VrSystem.h>

#include <d3d11_1.h>
#if USE_DX12
#include <d3d12.h>
#endif

#ifdef DONUT_WITH_OCULUS_VR
#include <OVR_CAPI_D3D.h>
#endif

#include <assert.h>
#include <vector>
#include <wrl.h>
#include <memory>

using namespace donut::math;
using namespace Microsoft::WRL;

namespace donut::app
{

#ifdef DONUT_WITH_OCULUS_VR
    class OculusVrSystem : public VrSystem
    {
        friend class VrSystem;
    private:
        ovrSession					m_oculusSession;
        ovrTextureSwapChain			m_oculusTextureSwapChain;
        ovrFovPort					m_projectionParameters[2];
        ovrPosef					m_eyePosesOculusHMD[2];
        ovrPosef					m_poseOculusHMD[2];
        int							m_frameCount;
        int2						m_vrSwapChainSize;

        OculusVrSystem()
            : m_oculusSession(nullptr)
            , m_oculusTextureSwapChain(nullptr)
            , m_frameCount(0)
        {
            memset(m_projectionParameters, 0, sizeof(m_projectionParameters));
            memset(m_eyePosesOculusHMD, 0, sizeof(m_eyePosesOculusHMD));
            memset(m_poseOculusHMD, 0, sizeof(m_poseOculusHMD));
        }

        VrResult Init(const LUID* pAdapterLuid, IUnknown* pDeviceOrQueue)
        {
            // Init libovr
            ovrInitParams ovrParams = {};

            if (OVR_FAILURE(ovr_Initialize(&ovrParams)))
                return VrResult::NoDevice;

            // Connect to HMD
            ovrGraphicsLuid oculusLuid = {};
            if (OVR_FAILURE(ovr_Create(&m_oculusSession, &oculusLuid)))
                return VrResult::NoDevice;

            assert(m_oculusSession);
            if (!m_oculusSession)
                return VrResult::OtherError;

            if (pAdapterLuid)
            {
                static_assert(sizeof(LUID) == sizeof(oculusLuid), "LUIDs must be of the same size");
                bool match = (memcmp(pAdapterLuid, &oculusLuid, sizeof(LUID)) == 0);

                if (!match)
                    return VrResult::WrongAdapter;
            }

            // Create swap texture set for both eyes side-by-side
            ovrHmdDesc hmdDesc = ovr_GetHmdDesc(m_oculusSession);
            ovrSizei eyeSizeLeft = ovr_GetFovTextureSize(m_oculusSession, ovrEye_Left, hmdDesc.DefaultEyeFov[0], 1.0f);
            ovrSizei eyeSizeRight = ovr_GetFovTextureSize(m_oculusSession, ovrEye_Right, hmdDesc.DefaultEyeFov[1], 1.0f);
            m_vrSwapChainSize = { max(eyeSizeLeft.w, eyeSizeRight.w) * 2, max(eyeSizeLeft.h, eyeSizeRight.h) };

            ovrTextureSwapChainDesc swapDesc =
            {
                ovrTexture_2D,
                OVR_FORMAT_R8G8B8A8_UNORM_SRGB,
                1, m_vrSwapChainSize.x, m_vrSwapChainSize.y, 1, 1,
                ovrFalse,
                ovrTextureMisc_DX_Typeless,
                ovrTextureBind_DX_RenderTarget
            };

            if (OVR_FAILURE(ovr_CreateTextureSwapChainDX(
                m_oculusSession,
                pDeviceOrQueue,
                &swapDesc,
                &m_oculusTextureSwapChain
            )))
                return VrResult::OtherError;

            if (!m_oculusTextureSwapChain)
                return VrResult::OtherError;

            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            rtvDesc.Texture2D.MipSlice = 0;

            for (int i = 0; i < 2; ++i)
            {
                // Store the eye FOVs and offset vectors for later use
                m_projectionParameters[i] = hmdDesc.DefaultEyeFov[i];
                ovrEyeRenderDesc eyeRenderDesc = ovr_GetRenderDesc(m_oculusSession, ovrEyeType(i), hmdDesc.DefaultEyeFov[i]);
                m_eyePosesOculusHMD[i] = eyeRenderDesc.HmdToEyePose;
            }

            return VrResult::OK;
        }
    public:

        virtual ~OculusVrSystem()
        {
            if (m_oculusTextureSwapChain)
            {
                ovr_DestroyTextureSwapChain(m_oculusSession, m_oculusTextureSwapChain);
                m_oculusTextureSwapChain = nullptr;
            }

            if (m_oculusSession)
            {
                ovr_Destroy(m_oculusSession);
                m_oculusSession = nullptr;
            }

            ovr_Shutdown();
        }

        static IDXGIAdapter* GetRequiredAdapter()
        {
            ovrInitParams ovrParams = {};

            if (OVR_FAILURE(ovr_Initialize(&ovrParams)))
                return nullptr;

            ovrSession session;
            ovrGraphicsLuid oculusLuid = {};
            ovrResult result = ovr_Create(&session, &oculusLuid);
            ovr_Shutdown();

            if (OVR_FAILURE(result))
                return nullptr;

            static_assert(sizeof(LUID) == sizeof(oculusLuid), "LUIDs must be of the same size");

            ComPtr<IDXGIFactory1> pDxgiFactory;
            if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pDxgiFactory)))
            {
                UINT nAdapter = 0;
                IDXGIAdapter* pAdapter = nullptr;
                while (SUCCEEDED(pDxgiFactory->EnumAdapters(nAdapter, &pAdapter)))
                {
                    DXGI_ADAPTER_DESC adapterDesc;
                    pAdapter->GetDesc(&adapterDesc);

                    if (memcmp(&adapterDesc.AdapterLuid, &oculusLuid, sizeof(oculusLuid)) == 0)
                        return pAdapter;

                    pAdapter->Release();
                    pAdapter = nullptr;

                    nAdapter++;
                }
            }

            return nullptr;
        }

        virtual VrApi GetApi() const override
        {
            return VrApi::OculusVR;
        }

        virtual VrResult AcquirePose() override
        {
            m_frameCount++;
            ovr_GetEyePoses(m_oculusSession, m_frameCount, true, m_eyePosesOculusHMD, m_poseOculusHMD, nullptr);
            return VrResult::OK;
        }

        virtual VrResult PresentOculusVR() override
        {
            // Commit changes to the swap chain
            ovr_CommitTextureSwapChain(m_oculusSession, m_oculusTextureSwapChain);

            // Submit the frame to the Oculus runtime
            ovrResult result;

            ovrLayerEyeFov layerMain = {};
            layerMain.Header.Type = ovrLayerType_EyeFov;
            layerMain.Header.Flags = ovrLayerFlag_HighQuality;
            layerMain.ColorTexture[0] = m_oculusTextureSwapChain;
            layerMain.Viewport[ovrEye_Left].Size.w = m_vrSwapChainSize.x / 2;
            layerMain.Viewport[ovrEye_Left].Size.h = m_vrSwapChainSize.y;
            layerMain.Viewport[ovrEye_Right].Pos.x = m_vrSwapChainSize.x / 2;
            layerMain.Viewport[ovrEye_Right].Size.w = m_vrSwapChainSize.x / 2;
            layerMain.Viewport[ovrEye_Right].Size.h = m_vrSwapChainSize.y;
            layerMain.Fov[ovrEye_Left] = m_projectionParameters[ovrEye_Left];
            layerMain.Fov[ovrEye_Right] = m_projectionParameters[ovrEye_Right];
            layerMain.RenderPose[ovrEye_Left] = m_poseOculusHMD[ovrEye_Left];
            layerMain.RenderPose[ovrEye_Right] = m_poseOculusHMD[ovrEye_Right];

            ovrLayerHeader * layerList[] = { (ovrLayerHeader*)&layerMain };
            result = ovr_SubmitFrame(m_oculusSession, m_frameCount, nullptr, layerList, dim(layerList));

            if (result == ovrError_DisplayLost)
            {
                return VrResult::DisplayLost;
            }
            else if (OVR_FAILURE(result))
            {
                // ovrErrorInfo errorInfo;
                // ovr_GetLastErrorInfo(&errorInfo);
                // WARN("ovr_SubmitFrame failed with error code: %d\nError message: %s", result, errorInfo.ErrorString);
            }

            return VrResult::OK;
        }
        
        virtual void SetOculusPerfHudMode(int mode) override
        {
            ovr_SetInt(m_oculusSession, OVR_PERF_HUD_MODE, mode);
        }

        virtual void Recenter() override
        {
            ovr_RecenterTrackingOrigin(m_oculusSession);
        }

        virtual int GetSwapChainBufferCount() const override
        {
            int swapLength = 0;

            ovr_GetTextureSwapChainLength(m_oculusSession, m_oculusTextureSwapChain, &swapLength);

            return swapLength;
        }

        virtual int GetCurrentSwapChainBuffer() const override
        {
            int currentIndex = -1;

            ovr_GetTextureSwapChainCurrentIndex(m_oculusSession, m_oculusTextureSwapChain, &currentIndex);

            return currentIndex;
        }

        virtual ID3D11Texture2D* GetSwapChainBufferD3D11(int index) const override
        {
            ID3D11Texture2D* swapTex = nullptr;
            ovr_GetTextureSwapChainBufferDX(m_oculusSession, m_oculusTextureSwapChain, index, IID_PPV_ARGS(&swapTex));
            return swapTex;
        }

        virtual ID3D12Resource* GetSwapChainBufferD3D12(int index) const override
        {
            ID3D12Resource* swapTex = nullptr;
#if USE_DX12
            ovr_GetTextureSwapChainBufferDX(m_oculusSession, m_oculusTextureSwapChain, index, IID_PPV_ARGS(&swapTex));
#else
            (void)index;
#endif
            return swapTex;
        }

        virtual int2 GetSwapChainSize() const override
        {
            return m_vrSwapChainSize;
        }

        virtual float4x4 GetProjectionMatrix(int eye, float zNear, float zFar) const override
        {
            const ovrFovPort& params = m_projectionParameters[eye];

            return perspProjD3DStyle(-params.LeftTan, params.RightTan, -params.DownTan, params.UpTan, zNear, zFar);
        }

        virtual float4x4 GetReverseProjectionMatrix(int eye, float zNear) const override
        {
            const ovrFovPort& params = m_projectionParameters[eye];

            return perspProjD3DStyleReverse(-params.LeftTan, params.RightTan, -params.DownTan, params.UpTan, zNear);
        }

        virtual affine3 GetEyeToOriginTransform(int eye) const override
        {
            ovrPosef hmdPose = m_poseOculusHMD[eye];
            // negate Z for RH to LH coordinate space transform, also negate x and y in quaternion to account for LH vs RH rotation
            hmdPose.Position.z = -hmdPose.Position.z;

            quat hmdOrientation =
            {
                hmdPose.Orientation.w,		// Note, different layout from Oculus quaternion!
                -hmdPose.Orientation.x,
                -hmdPose.Orientation.y,
                hmdPose.Orientation.z
            };

            affine3 aff = hmdOrientation.toAffine();
            aff.m_translation = float3(&hmdPose.Position.x);
            return aff;
        }
    };
#endif

    IDXGIAdapter* VrSystem::GetRequiredAdapter()
    {
        IDXGIAdapter* pAdapter = nullptr;

#ifdef DONUT_WITH_OCULUS_VR
        pAdapter = OculusVrSystem::GetRequiredAdapter();
        if (pAdapter) return pAdapter;
#endif

        return nullptr;
    }

    VrResult VrSystem::CreateD3D11(ID3D11Device* pDevice, VrSystem** ppSystem)
    {
        *ppSystem = nullptr;
        VrResult result = VrResult::OtherError;

        IDXGIDevice* pDxgiDevice = nullptr;
        IDXGIAdapter* pAdapter = nullptr;
        pDevice->QueryInterface(&pDxgiDevice);
        pDxgiDevice->GetAdapter(&pAdapter);
        pDxgiDevice->Release();

        DXGI_ADAPTER_DESC adapterDesc;
        pAdapter->GetDesc(&adapterDesc);
        pAdapter->Release();

#ifdef DONUT_WITH_OCULUS_VR
        OculusVrSystem* oculus = new OculusVrSystem();
        result = oculus->Init(&adapterDesc.AdapterLuid, pDevice);
        if (result == VrResult::OK)
        {
            *ppSystem = oculus;
            return VrResult::OK;
        }

        delete oculus;

        if (result == VrResult::WrongAdapter)
            return VrResult::WrongAdapter;
#endif

        return result;
    }

    VrResult VrSystem::CreateD3D12(const LUID* pAdapterLuid, ID3D12CommandQueue* pCommandQueue, VrSystem** ppSystem)
    {
        *ppSystem = nullptr;
        VrResult result = VrResult::OtherError;

#ifdef DONUT_WITH_OCULUS_VR
        OculusVrSystem* oculus = new OculusVrSystem();
        result = oculus->Init(pAdapterLuid, (IUnknown*)pCommandQueue);
        if (result == VrResult::OK)
        {
            *ppSystem = oculus;
            return VrResult::OK;
        }

        delete oculus;
#endif

        return result;
    }

}