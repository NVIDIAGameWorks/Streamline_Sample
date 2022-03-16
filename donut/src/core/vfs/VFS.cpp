#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <fstream>
#include <assert.h>
#include <algorithm>

#ifdef _WIN32
#include <Shlwapi.h>
#else
#define USE_STD_FILESYSTEM 1 /*ADAM*/
#endif // _WIN32


#if USE_STD_FILESYSTEM
extern "C" {
#include <glob.h>
}
#endif // USE_STD_FILESYSTEM

using namespace donut::vfs;

Blob::Blob(void* data, size_t size)
    : m_data(data)
    , m_size(size)
{

}

const void* Blob::data() const
{
    return m_data;
}

size_t Blob::size() const
{
    return m_size;
}

Blob::~Blob()
{
    if (m_data)
    {
        free(m_data);
        m_data = nullptr;
    }

    m_size = 0;
}

bool NativeFileSystem::fileExists(const std::filesystem::path& name)
{
    return std::filesystem::exists(name);
}

std::shared_ptr<IBlob> NativeFileSystem::readFile(const std::filesystem::path& name)
{
    // TODO: better error reporting

    std::ifstream file(name, std::ios::binary);

    if (!file.is_open())
    {
        // file does not exist or is locked
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    uint64_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        // file larger than size_t
        assert(false);
        return nullptr;
    }

    char* data = static_cast<char*>(malloc(size));

    if (data == nullptr)
    {
        // out of memory
        assert(false);
        return nullptr;
    }

    file.read(data, size);

    if (!file.good())
    {
        // reading error
        assert(false);
        return nullptr;
    }

    return std::make_shared<Blob>(data, size);
}

bool NativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    // TODO: better error reporting

    std::ofstream file(name, std::ios::binary);

    if (!file.is_open())
    {
        // file does not exist or is locked
        return false;
    }

    if (size > 0)
    {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    if (!file.good())
    {
        // writing error
        return false;
    }

    return true;
}

bool NativeFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results)
{
#ifndef USE_STD_FILESYSTEM
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern.generic_string().c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isDot = strcmp(findData.cFileName, ".") == 0;
        bool isDotDot = strcmp(findData.cFileName, "..") == 0;

        if ((isDirectory == directories) && !isDot && !isDotDot)
        {
            results.push_back(findData.cFileName);
        }
    } while (FindNextFileA(hFind, &findData) != 0);

    FindClose(hFind);

    return true;
#else // USE_STD_FILESYSTEM
    glob64_t glob_matches;

    if (0/*success*/ == glob64(pattern.c_str(), 0 /*flags*/, nullptr /*errfunc*/, &glob_matches))
    {
        for (int i=0; i<glob_matches.gl_pathc; ++i)
        {
            const char* globentry = (glob_matches.gl_pathv)[i];
            std::error_code ec, ec2;
            std::filesystem::directory_entry entry(globentry, ec);
            if (!ec)
            {
                if (directories == entry.is_directory(ec2) && !ec2)
                {
                    results.push_back(entry.path().filename().c_str());
                }
            }
        }
        globfree64(&glob_matches);
    
        return true;
    }

    return false;
#endif // USE_STD_FILESYSTEM
}

RelativeFileSystem::RelativeFileSystem(std::shared_ptr<IFileSystem> parent, const std::filesystem::path& basePath)
    : m_parent(parent)
    , m_basePath(basePath.lexically_normal())
{
}

bool RelativeFileSystem::fileExists(const std::filesystem::path& name)
{
    return m_parent->fileExists(m_basePath / name.relative_path());
}

std::shared_ptr<IBlob> RelativeFileSystem::readFile(const std::filesystem::path& name)
{
    return m_parent->readFile(m_basePath / name.relative_path());
}

bool RelativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    return m_parent->writeFile(m_basePath / name.relative_path(), data, size);
}

bool RelativeFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results)
{
    return m_parent->enumerate(m_basePath / pattern.relative_path(), directories, results);
}

void RootFileSystem::mount(const std::filesystem::path& path, std::shared_ptr<IFileSystem> fs)
{
    if (findMountPoint(path, nullptr, nullptr))
    {
        log::error("Cannot mount a filesystem at %s: there is another FS that includes this path", path.c_str());
        return;
    }

    m_MountPoints.push_back(std::make_pair(path.lexically_normal().generic_string(), fs));
}

void donut::vfs::RootFileSystem::mount(const std::filesystem::path& path, const std::filesystem::path& nativePath)
{
    mount(path, std::make_shared<RelativeFileSystem>(std::make_shared<NativeFileSystem>(), nativePath));
}

bool RootFileSystem::unmount(const std::filesystem::path& path)
{
    std::string spath = path.lexically_normal().generic_string();

    for (size_t index = 0; index < m_MountPoints.size(); index++)
    {
        if (m_MountPoints[index].first == spath)
        {
            m_MountPoints.erase(m_MountPoints.begin() + index);
            return true;
        }
    }

    return false;
}

bool RootFileSystem::findMountPoint(const std::filesystem::path& path, std::filesystem::path* pRelativePath, IFileSystem** ppFS)
{
    std::string spath = path.lexically_normal().generic_string();

    for (auto it : m_MountPoints)
    {
        if (spath.find(it.first, 0) == 0)
        {
            if (pRelativePath)
            {
                std::string relative = spath.substr(it.first.size());
                *pRelativePath = relative;
            }

            if (ppFS)
            {
                *ppFS = it.second.get();
            }

            return true;
        }
    }

    return false;
}

bool RootFileSystem::fileExists(const std::filesystem::path& name)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->fileExists(relativePath);
    }

    return false;
}

std::shared_ptr<IBlob> RootFileSystem::readFile(const std::filesystem::path& name)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->readFile(relativePath);
    }

    return nullptr;
}

bool RootFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(name, &relativePath, &fs))
    {
        return fs->writeFile(relativePath, data, size);
    }

    return false;
}

bool RootFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results)
{
    std::filesystem::path relativePath;
    IFileSystem* fs = nullptr;

    if (findMountPoint(pattern, &relativePath, &fs))
    {
        return fs->enumerate(relativePath, directories, results);
    }

    return false;
}
