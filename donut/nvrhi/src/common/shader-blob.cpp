#include <nvrhi/common/shader-blob.h>
#include <nvrhi/common/crc.h>
#include <sstream>

namespace nvrhi {

static const char* g_BlobSignature = "NVSP";
static size_t g_BlobSignatureSize = 4;

struct ShaderBlobEntry
{
    uint32_t hashKeySize;
    uint32_t dataSize;
    uint32_t dataCrc;
    uint32_t defineHash;
    uint32_t flags;
};

bool FindPermutationInBlob(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, const void** pBinary, size_t* pSize)
{
    if (!blob || blobSize < g_BlobSignatureSize)
        return false;

    if (!pBinary || !pSize)
        return false;

    if (memcmp(blob, g_BlobSignature, g_BlobSignatureSize) != 0)
    {
        if (numConstants == 0)
        {
            *pBinary = blob;
            *pSize = blobSize;
            return true; // this blob is not a permutation blob, and no permutation is requested
        }
        else
        {
            return false; // this blob is not a permutation blob, but the caller requested a permutation
        }
    }

    blob = static_cast<const char*>(blob) + g_BlobSignatureSize;
    blobSize -= g_BlobSignatureSize;

    CrcHash hasher;
    const char equal = '=';
    const char semicolon = ';';
    for (uint32_t n = 0; n < numConstants; n++)
    {
        const ShaderConstant& constant = constants[n];
        hasher.AddBytes(constant.name, strlen(constant.name));
        hasher.AddBytes(&equal, 1);
        hasher.AddBytes(constant.value, strlen(constant.value));
        hasher.AddBytes(&semicolon, 1);
    }
    uint32_t defineHash = hasher.Get();

    while (blobSize > sizeof(ShaderBlobEntry))
    {
        const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

        if (header->dataSize == 0)
            return false; // last header in the blob is empty

        if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize)
            return false; // insufficient bytes in the blob, cannot continue

        if (header->defineHash == defineHash)
        {
            const char* binary = static_cast<const char*>(blob) + sizeof(ShaderBlobEntry) + header->hashKeySize;
            CrcHash dataHasher;
            dataHasher.AddBytes(binary, header->dataSize);
            if (dataHasher.Get() != header->dataCrc)
                return false; // crc mismatch, corrupted data in the blob

            *pBinary = binary;
            *pSize = header->dataSize;
            return true;
        }

        size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize;
        blob = static_cast<const char*>(blob) + offset;
        blobSize -= offset;
    }

    return false; // went through the blob, permutation not found
}

void EnumeratePermutationsInBlob(const void* blob, size_t blobSize, std::vector<std::string>& permutations)
{
    if (!blob || blobSize < g_BlobSignatureSize)
        return;

    if (memcmp(blob, g_BlobSignature, g_BlobSignatureSize) != 0)
        return;

    blob = static_cast<const char*>(blob) + g_BlobSignatureSize;
    blobSize -= g_BlobSignatureSize;

    while (blobSize > sizeof(ShaderBlobEntry))
    {
        const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

        if (header->dataSize == 0)
            return;

        if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize)
            return;

        if (header->hashKeySize > 0)
        {
            std::string hashKey;
            hashKey.resize(header->hashKeySize);

            memcpy(&hashKey[0], static_cast<const char*>(blob) + sizeof(ShaderBlobEntry), header->hashKeySize);

            permutations.push_back(hashKey);
        }
        else
        {
            permutations.push_back("<default>");
        }

        size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize;
        blob = static_cast<const char*>(blob) + offset;
        blobSize -= offset;
    }
}

std::string FormatShaderNotFoundMessage(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants)
{
    std::stringstream ss;
    ss << "Couldn't find the required shader permutation in the blob, or the blob is corrupted." << std::endl;
    ss << "Required permutation key: " << std::endl;

    if (numConstants)
    {
        for (uint32_t n = 0; n < numConstants; n++)
        {
            const ShaderConstant& constant = constants[n];
            ss << constant.name << '=' << constant.value << ';';
        }
    }
    else
    {
        ss << "<default>";
    }

    ss << std::endl;

    std::vector<std::string> permutations;
    EnumeratePermutationsInBlob(blob, blobSize, permutations);

    if (permutations.size() > 0)
    {
        ss << "Permutations available in the blob:" << std::endl;
        for (const std::string& key : permutations)
            ss << key << std::endl;
    }
    else
    {
        ss << "No permutations found in the blob.";
    }

    return ss.str();
}

} // namespace nvrhi