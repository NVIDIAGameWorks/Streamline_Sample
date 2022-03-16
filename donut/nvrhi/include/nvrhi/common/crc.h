#pragma once

#include <stdint.h>
#include <nvrhi/common/containers.h>
#include <nmmintrin.h>

namespace nvrhi {

extern const bool CpuSupportsSSE42;
extern const uint32_t CrcTable[];

#ifdef _MSC_VER
#define NVRHI_FORCEINLINE __forceinline
#else
#define NVRHI_FORCEINLINE __attribute__((__always_inline__))
#endif /* _MSC_VER */

class CrcHash
{
private:
    uint32_t crc;
public:
    CrcHash()
        : crc(~0u)
    {
    }

    uint32_t Get()
    {
        return ~crc;
    }

    template<size_t size> NVRHI_FORCEINLINE void AddBytesSSE42(const void* p)
    {
        static_assert(size % 4 == 0, "Size of hashable types must be multiple of 4");

        const uint32_t* data = (const uint32_t*)p;

        const size_t numIterations = size / sizeof(uint32_t);
        for (size_t i = 0; i < numIterations; i++)
        {
            crc = _mm_crc32_u32(crc, data[i]);
        }
    }

    NVRHI_FORCEINLINE void AddBytes(const char* p, size_t size)
    {
        for (size_t idx = 0; idx < size; idx++)
            crc = CrcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }

    template<typename T> void Add(const T& value)
    {
        if (CpuSupportsSSE42)
            AddBytesSSE42<sizeof(value)>((void*)&value);
        else
            AddBytes((char*)&value, sizeof(value));
    }

    void Add(const void* p, size_t size)
    {
        if (CpuSupportsSSE42)
        {
            uint32_t *data = (uint32_t *)p;
            const size_t numIterations = size / sizeof(uint32_t);
            for(size_t i = 0; i < numIterations; i++)
            {
                crc = _mm_crc32_u32(crc, data[i]);
            }

            if (size % sizeof(uint32_t))
            {
                AddBytes((char *)&data[numIterations], size % sizeof(uint32_t));
            }
        } else {
            AddBytes((char *)p, size);
        }
    }

    template <typename T> void AddVector(const nvrhi::vector<T>& vec)
    {
        Add(vec.data(), vec.size() * sizeof(T));
    }
};

} // namespace nvrhi
