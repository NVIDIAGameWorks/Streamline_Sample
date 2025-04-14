/*
* Copyright (c) 2014-2022, NVIDIA CORPORATION. All rights reserved.
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

/*
License for glfw

Copyright (c) 2002-2006 Marcus Geelnard

Copyright (c) 2006-2019 Camilla Lowy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
*/
#pragma once
#if DONUT_WITH_DX11
#include <string>
#include <algorithm>
#include <locale>

#include <donut/core/log.h>
#include <donut/app/DeviceManager_DX11.h>

#include <Windows.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>

#include <nvrhi/d3d11.h>
#include <nvrhi/validation.h>

#include "DeviceManagerOverride.h"

// STREAMLINE
#include "../SLWrapper.h"
using nvrhi::RefCountPtr;
using namespace donut::app;

DeviceManagerOverride_DX11::DeviceManagerOverride_DX11()
{
}

IDXGIAdapter* DeviceManagerOverride_DX11::GetAdapter()
{
    return m_DxgiAdapter;
}

bool DeviceManagerOverride_DX11::CreateDevice()
{
    bool success = DeviceManager_DX11::CreateDevice();
    if (success)
    {
        SLWrapper::Get().ProxyToNative(m_Device, (void**)&m_Device_native);
        SLWrapper::Get().SetDevice_raw(m_Device_native);
    }
    return success;
}

bool DeviceManagerOverride_DX11::CreateSwapChain()
{
    bool success = DeviceManager_DX11::CreateSwapChain();
    if (success)
    {
        SLWrapper::Get().ProxyToNative(m_SwapChain, (void**) &m_SwapChain_native);
    }
    return success;
}

bool DeviceManagerOverride_DX11::BeginFrame()
{
    // Unimplemented: Latewarp integration for DX11
    return DeviceManager_DX11::BeginFrame();
}

void DeviceManagerOverride_DX11::DestroyDeviceAndSwapChain()
{
    DeviceManager_DX11::DestroyDeviceAndSwapChain();
    m_SwapChain_native = nullptr;
    m_Device_native = nullptr;
}

DeviceManager* CreateD3D11()
{
    return new DeviceManagerOverride_DX11();
}
#endif