#include <donut/core/vfs/SQLiteFS.h>
#include <sqlite3.h>

#ifdef DONUT_WITH_LZ4
#include <lz4.h>
#endif

#include <cstring>

using namespace donut::vfs;

SQLiteFileSystem::SQLiteFileSystem(const std::filesystem::path& databaseFile, bool readonly, const std::string& password)
    : m_db(nullptr)
    , m_readFileStatement(nullptr)
{
    int flags = 0;
    if (readonly)
        flags |= SQLITE_OPEN_READONLY;

    int result;
    result = sqlite3_open_v2(databaseFile.generic_string().c_str(), &m_db, flags, nullptr);

    if (result != SQLITE_OK)
        return;

    result = sqlite3_prepare_v3(
        m_db, 
        "SELECT data, compressed, original_size FROM files WHERE name=?1 LIMIT 1", 
        -1,                         // length of the query string in bytes, -1 means a null terminated string
        SQLITE_PREPARE_PERSISTENT,  // will be executed many times
        &m_readFileStatement, 
        nullptr                     // pointer to the unused portion of the query string, we don't need that
    );

    if (result != SQLITE_OK)
        return;

}

SQLiteFileSystem::~SQLiteFileSystem()
{
    if (m_readFileStatement)
    {
        sqlite3_finalize(m_readFileStatement);
        m_readFileStatement = nullptr;
    }

    if (m_db)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool SQLiteFileSystem::isOpen()
{
    if (m_db == nullptr) return false;
    if (m_readFileStatement == nullptr) return false;
    return true;
}

bool SQLiteFileSystem::fileExists(const std::filesystem::path& name)
{
    // not implemented
    return false;
}

std::shared_ptr<IBlob> SQLiteFileSystem::readFile(const std::filesystem::path& name)
{
    size_t size = 0;
    int originalSize = 0;
    int compressedSize = 0;
    void* data = nullptr;
    std::string nameString = name.lexically_normal().generic_string();

    {
        // use a mutex to prevent multiple threads from using the same statement object at the same time
        std::lock_guard<std::mutex> guard(m_mutex);

        
        int result;
        result = sqlite3_reset(m_readFileStatement);

        if (result != SQLITE_OK)
            return nullptr;

        result = sqlite3_bind_text(
            m_readFileStatement,
            1,                  // parameter index
            nameString.c_str(),
            -1,                 // number of bytes in the argument, -1 means a null terminated string
            SQLITE_TRANSIENT    // sqlite will make a copy of the value
        );

        if (result != SQLITE_OK)
            return nullptr;

        result = sqlite3_step(m_readFileStatement);

        if (result != SQLITE_ROW)
        {
#if 0
            OutputDebugStringA("File not found: ");
            OutputDebugStringA(nameString.c_str());
            OutputDebugStringA("\n");
            __debugbreak();
#endif
            return nullptr;
        }

        int sqlite_size = sqlite3_column_bytes(m_readFileStatement, 0);
        const void* sqlite_data = sqlite3_column_blob(m_readFileStatement, 0);

        if (sqlite_size <= 0 || sqlite_data == nullptr)
            return nullptr;

        compressedSize = sqlite3_column_int(m_readFileStatement, 1);
        originalSize = sqlite3_column_int(m_readFileStatement, 2);

        size = static_cast<size_t>(sqlite_size);
        data = malloc(size);
        memcpy(data, sqlite_data, size);
    }

#ifdef DONUT_WITH_LZ4
    if (compressedSize > 0)
    {
        void* uncompressedData = malloc(originalSize);
        int uncompressedLength = originalSize;

        int result = LZ4_decompress_safe((const char*)data, (char*)uncompressedData, std::min(compressedSize, (int)size), uncompressedLength);
        
        free(data);

        if (result <= 0)
        {
            free(uncompressedData);
            return nullptr;
        }

        data = uncompressedData;
        size = uncompressedLength;
    }
#endif

    size = originalSize;

    return std::make_shared<Blob>(data, size);
}

bool SQLiteFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    // not implemented
    return false;
}

bool SQLiteFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results)
{
    // not implemented
    return false;
}
