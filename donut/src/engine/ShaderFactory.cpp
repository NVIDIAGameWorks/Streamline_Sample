//----------------------------------------------------------------------------------
// File:        ShaderFactory.cpp
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

#include <donut/engine/ShaderFactory.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <iostream>

using namespace std;
using namespace donut::vfs;
using namespace donut::engine;

ShaderFactory::ShaderFactory(nvrhi::DeviceHandle rendererInterface,
    std::shared_ptr<IFileSystem> fs,
    std::filesystem::path frameworkPath,
    std::filesystem::path appPath)
	: m_Device(rendererInterface)
	, m_fs(fs)
    , m_frameworkPath(frameworkPath)
    , m_appPath(appPath)
{
}

void ShaderFactory::ClearCache()
{
	m_BytecodeCache.clear();
}

std::shared_ptr<IBlob> ShaderFactory::GetBytecode(ShaderLocation location, const char* fileName, const char* entryName)
{
    if (!entryName)
        entryName = "main";

    string adjustedName = fileName;
    {
        size_t pos = adjustedName.find(".hlsl");
        if (pos != string::npos)
            adjustedName.erase(pos, 5);
        for (char& c : adjustedName)
            if (c == '\\' || c == '/')
                c = '_';

        if (entryName && strcmp(entryName, "main"))
            adjustedName += "_" + string(entryName);
    }

    const std::filesystem::path& basePath = (location == ShaderLocation::FRAMEWORK) ? m_frameworkPath : m_appPath;
    std::filesystem::path shaderFilePath = basePath / (adjustedName + ".bin");

    std::shared_ptr<IBlob>& data = m_BytecodeCache[shaderFilePath.generic_string()];

    if (data)
        return data;

    data = m_fs->readFile(shaderFilePath);

    if (!data)
    {
        log::error("Couldn't read the binary file for shader %s from %s", fileName, shaderFilePath.generic_string().c_str());
        return nullptr;
    }

    return data;
}


nvrhi::ShaderHandle ShaderFactory::CreateShader(ShaderLocation location, const char* fileName, const char* entryName, const vector<ShaderMacro>* pDefines, nvrhi::ShaderType::Enum shaderType)
{
    nvrhi::ShaderDesc desc = nvrhi::ShaderDesc(shaderType);
    desc.debugName = fileName;
    return CreateShader(location, fileName, entryName, pDefines, desc);
}

nvrhi::ShaderHandle ShaderFactory::CreateShader(ShaderLocation location, const char* fileName, const char* entryName, const vector<ShaderMacro>* pDefines, const nvrhi::ShaderDesc& desc)
{
    std::shared_ptr<IBlob> byteCode = GetBytecode(location, fileName, entryName);

    if(!byteCode)
        return nullptr;

    vector<nvrhi::ShaderConstant> constants;
    if (pDefines)
    {
        for (const ShaderMacro& define : *pDefines)
            constants.push_back(nvrhi::ShaderConstant{ define.name.c_str(), define.definition.c_str() });
    }

    nvrhi::ShaderDesc descCopy = desc;
    descCopy.entryName = entryName;

    return m_Device->createShaderPermutation(descCopy, byteCode->data(), byteCode->size(), constants.data(), uint32_t(constants.size()));
} 

nvrhi::ShaderLibraryHandle donut::engine::ShaderFactory::CreateShaderLibrary(ShaderLocation location, const char* fileName, const std::vector<ShaderMacro>* pDefines)
{
    std::shared_ptr<IBlob> byteCode = GetBytecode(location, fileName, nullptr);

    if (!byteCode)
        return nullptr;

    vector<nvrhi::ShaderConstant> constants;
    if (pDefines)
    {
        for (const ShaderMacro& define : *pDefines)
            constants.push_back(nvrhi::ShaderConstant{ define.name.c_str(), define.definition.c_str() });
    }

    return m_Device->createShaderLibraryPermutation(byteCode->data(), byteCode->size(), constants.data(), uint32_t(constants.size()));
}
