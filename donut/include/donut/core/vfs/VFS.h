#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <vector>

namespace donut::vfs
{
    class IBlob
    {
    public:
        virtual ~IBlob() { }
        virtual const void* data() const = 0;
        virtual size_t size() const = 0;
    };

    class Blob : public IBlob
    {
    private:
        void* m_data;
        size_t m_size;

    public:
        Blob(void* data, size_t size);
        virtual ~Blob() override;
        virtual const void* data() const override;
        virtual size_t size() const override;
    };

    class IFileSystem
    {
    public:
        virtual bool fileExists(const std::filesystem::path& name) = 0;
        virtual std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) = 0;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) = 0;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results) = 0;
    };

    class NativeFileSystem : public IFileSystem
    {
    public:
        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results) override;
    };

    class RelativeFileSystem : public IFileSystem
    {
    private:
        std::shared_ptr<IFileSystem> m_parent;
        std::filesystem::path m_basePath;
    public:
        RelativeFileSystem(std::shared_ptr<IFileSystem> parent, const std::filesystem::path& basePath);

        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results) override;
    };

    class RootFileSystem : public IFileSystem
    {
    private:
        std::vector<std::pair<std::string, std::shared_ptr<IFileSystem>>> m_MountPoints;

        bool findMountPoint(const std::filesystem::path& path, std::filesystem::path* pRelativePath, IFileSystem** ppFS);
    public:
        void mount(const std::filesystem::path& path, std::shared_ptr<IFileSystem> fs);
        void mount(const std::filesystem::path& path, const std::filesystem::path& nativePath);
        bool unmount(const std::filesystem::path& path);

        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::string>& results) override;
    };
}
