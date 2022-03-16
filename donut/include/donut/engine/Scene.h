#pragma once

#include <donut/engine/ObjectFile.h>
#include <donut/engine/SceneTypes.h>
#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <vector>
#ifdef _WIN32
#include <ppl.h>
#endif // _WIN32
#include <memory>
#include <filesystem>

namespace Json
{
    class Value;
}

namespace donut::vfs
{
    class IFileSystem;
}

namespace donut::engine
{
    class TextureCache;
    
    class Scene : public IMeshSet
    {
    private:
        std::shared_ptr<vfs::IFileSystem>   m_fs;
        std::vector<std::unique_ptr<ObjectFile>> m_objects;
        dm::box3                            m_SceneBounds;

        std::vector<MeshInfo*>              m_Meshes;
        std::vector<MeshInstance*>          m_MeshInstances;
        std::vector<Material*>              m_Materials;

        bool LoadObjectFiles(Json::Value& objectListNode, const std::filesystem::path& rootPath, TextureCache& textureCache, VertexAttribute::Enum attributeMask);
        bool LoadLights(Json::Value& lightListNode);
        bool LoadCameras(Json::Value& cameraListNode, Json::Value& activeCameraNode);

    protected:
        virtual ObjectFile* CreateObjectFile();

    public:
        Scene(std::shared_ptr<vfs::IFileSystem> fs);

        std::vector<std::shared_ptr<Light>> Lights;
        std::shared_ptr<Light> GetLightByName(const std::string& name);

        std::vector<std::shared_ptr<CameraPreset>> Cameras;
        std::shared_ptr<CameraPreset> DefaultCamera;
        std::shared_ptr<CameraPreset> GetCameraByName(const std::string& name);

        void CreateRenderingResources(nvrhi::IDevice* device);
        dm::box3 GetSceneBounds() { return m_SceneBounds; }

        virtual bool Load(const std::filesystem::path& jsonFileName, VertexAttribute::Enum attributeMask, TextureCache& textureCache);

        static const SceneLoadingStats& GetLoadingStats();

        ObjectFile* GetObjectFile(uint32_t index);
        uint32_t GetNumObjects();

        void UpdateMeshBounds();
        void UpdateTransformBuffers(nvrhi::ICommandList* commandList);

        // IMeshSet implementation
        const std::vector<MeshInfo*>& GetMeshes() const override { return m_Meshes; }
        const std::vector<MeshInstance*>& GetMeshInstances() const override { return m_MeshInstances; }
        const std::vector<Material*>& GetMaterials() const override { return m_Materials; }
    };
}
