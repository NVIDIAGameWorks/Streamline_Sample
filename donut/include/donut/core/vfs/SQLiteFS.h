#pragma once

#include <donut/core/vfs/VFS.h>
#include <mutex>

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

namespace donut::vfs
{
    class SQLiteFileSystem : public IFileSystem
    {
    private:
        sqlite3* m_db;
        sqlite3_stmt* m_readFileStatement;
        std::mutex m_mutex;

    public:
        SQLiteFileSystem(const std::filesystem::path& databaseFile, bool readonly, const std::string& password);
        ~SQLiteFileSystem();
        bool isOpen();

        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results) override;
    };
}