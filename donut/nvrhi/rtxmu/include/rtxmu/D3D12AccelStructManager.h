/*
* Copyright (c) 2021 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "AccelStructManager.h"
#include "D3D12Suballocator.h"

namespace rtxmu
{

    struct DxAccelerationStructure : AccelerationStructure
    {
        Suballocator<ID3D12Device5, D3D12ScratchBlock>::SubAllocation updateGpuMemory;
        Suballocator<ID3D12Device5, D3D12ScratchBlock>::SubAllocation scratchGpuMemory;
        Suballocator<ID3D12Device5, D3D12AccelStructBlock>::SubAllocation resultGpuMemory;
        Suballocator<ID3D12Device5, D3D12AccelStructBlock>::SubAllocation compactionGpuMemory;
        Suballocator<ID3D12Device5, D3D12ReadBackBlock>::SubAllocation compactionSizeCpuMemory;
        Suballocator<ID3D12Device5, D3D12CompactionWriteBlock>::SubAllocation compactionSizeGpuMemory;
    };

    class DxAccelStructManager : public AccelStructManager<DxAccelerationStructure>
    {
    public:

        DxAccelStructManager(ID3D12Device5* device);
        
        // Initializes suballocator block size
        void Initialize(uint32_t suballocatorBlockSize = DefaultSuballocatorBlockSize);

        // Resets all queues and frees all memory in suballocators
        void Reset();

        // Receives acceleration structure inputs and returns a command list with build commands
        void PopulateUpdateCommandList(ID3D12GraphicsCommandList4*                                 commandList,
                                       const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* asInputs,
                                       const uint32_t                                              buildCount,
                                       const std::vector<uint64_t>&                                accelStructIds);

        // Receives acceleration structure inputs and returns a command list with build commands
        void PopulateBuildCommandList(ID3D12GraphicsCommandList4*                                 commandList,
                                      const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* asInputs,
                                      const uint64_t                                              buildCount,
                                      std::vector<uint64_t>&                                      accelStructIds);

        // Returns a command list with compaction copies if the acceleration structures are ready to be compacted
        void PopulateCompactionCommandList(ID3D12GraphicsCommandList4*  commandList,
                                           const std::vector<uint64_t>& accelStructIds);

        // Receives acceleration structure inputs and places UAV barriers for them
        void PopulateUAVBarriersCommandList(ID3D12GraphicsCommandList4*  commandList,
                                            const std::vector<uint64_t>& accelStructIds);

        // Performs copies to bring over any compaction size data
        void PopulateCompactionSizeCopiesCommandList(ID3D12GraphicsCommandList4* commandList,
                                                     const std::vector<uint64_t>& accelStructIds);

        // Remove all memory that an Acceleration Structure might use
        void RemoveAccelerationStructures(const std::vector<uint64_t>& accelStructIds);

        // Remove all memory used in build process, while only leaving the acceleration structure buffer itself in memory
        void GarbageCollection(const std::vector<uint64_t>& accelStructIds);

        // Returns GPUVA of the acceleration structure
        D3D12_GPU_VIRTUAL_ADDRESS GetAccelStructGPUVA(const uint64_t accelStructId);

        // Returns a "string" containing memory consumption information
        const char* GetLog();

        // Returns if acceleration structure has been compacted
        bool IsCompacted(const uint64_t accelStructId) { return m_asBufferBuildQueue[accelStructId]->isCompacted; }

    private:
        
        void CopyCompaction(ID3D12GraphicsCommandList4* commandList,
                            const uint64_t              accelStructId);

        void PostBuildRelease(const uint64_t accelStructId);

        void ReleaseAccelerationStructures(const uint64_t accelStructId);

        // Device responsible for allocating with this manager
        ID3D12Device5* m_device;

        // Suballocation buffers
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12ScratchBlock>>         m_scratchPool;
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12AccelStructBlock>>     m_resultPool;
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12ScratchBlock>>         m_updatePool;
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12AccelStructBlock>>     m_compactionPool;
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12CompactionWriteBlock>> m_compactionSizeGpuPool;
        std::unique_ptr<Suballocator<ID3D12Device5, D3D12ReadBackBlock>>        m_compactionSizeCpuPool;

    };
}