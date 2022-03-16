#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>
#include <string>

namespace nvrhi {

bool FindPermutationInBlob(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, const void** pBinary, size_t* pSize);
void EnumeratePermutationsInBlob(const void* blob, size_t blobSize, std::vector<std::string>& permutations);
std::string FormatShaderNotFoundMessage(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants);

} // namespace nvrhi
