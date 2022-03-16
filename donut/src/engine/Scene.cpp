#include <donut/engine/Scene.h>
#include <donut/engine/ObjectFile.h>
#include <donut/core/json.h>
#include <donut/core/log.h>
#include <json/value.h>

using namespace donut::math;
using namespace donut::vfs;
using namespace donut::engine;

static SceneLoadingStats g_LoadingStats;

const SceneLoadingStats& Scene::GetLoadingStats()
{
    return g_LoadingStats;
}

ObjectFile * Scene::GetObjectFile(uint32_t index)
{
    if (index >= m_objects.size())
        return nullptr;

    auto& obj = m_objects[index]; 
    return obj.get();
}

void Scene::UpdateMeshBounds()
{
    for (auto& object : m_objects)
        object->UpdateMeshBounds();
}

void Scene::UpdateTransformBuffers(nvrhi::ICommandList* commandList)
{
    for (auto& object : m_objects)
        object->UpdateTransformBuffer(commandList);
}

Scene::Scene(std::shared_ptr<IFileSystem> fs)
    : m_fs(fs)
{
}

bool Scene::Load(const std::filesystem::path& jsonFileName, VertexAttribute::Enum attributeMask, TextureCache& textureCache)
{
    memset(&g_LoadingStats, 0, sizeof(g_LoadingStats));

    std::filesystem::path rootPath = jsonFileName.parent_path();

    Json::Value documentRoot;
    if (!json::LoadFromFile(*m_fs, jsonFileName, documentRoot))
        return false;

    bool success;
    if (documentRoot.isArray())
    {
        // Old-style json format
        success = LoadObjectFiles(documentRoot, rootPath, textureCache, attributeMask);
    }
    else if(documentRoot.isObject())
    {
        // .fscene format
        success = LoadObjectFiles(documentRoot["models"], rootPath, textureCache, attributeMask);
        success = success && LoadLights(documentRoot["lights"]);
        success = success && LoadCameras(documentRoot["cameras"], documentRoot["active_camera"]);
    }
    else
    {
        log::error("Unrecognized structure of the scene description file");
        success = false;
    }

    m_SceneBounds = box3::empty();
    for (auto& object : m_objects)
    {
        m_SceneBounds |= object->GetSceneBounds();

        m_Materials.insert(m_Materials.end(), object->GetMaterials().begin(), object->GetMaterials().end());
        m_Meshes.insert(m_Meshes.end(), object->GetMeshes().begin(), object->GetMeshes().end());
        m_MeshInstances.insert(m_MeshInstances.end(), object->GetMeshInstances().begin(), object->GetMeshInstances().end());
    }

    // Assign material IDs
    int materialID = 1;
    for (auto material : m_Materials)
    {
        material->materialID = materialID;
        materialID++;
    }

    return success;
}

bool Scene::LoadObjectFiles(Json::Value& objectListNode, const std::filesystem::path& rootPath, TextureCache& textureCache, VertexAttribute::Enum attributeMask)
{
    bool success = true;

    if (!objectListNode.isArray())
        return false;

    concurrency::task_group taskGroup;
    for (auto& objectNode : objectListNode)
    {
        ++g_LoadingStats.ObjectsTotal;

        ObjectFile* object = CreateObjectFile();
        m_objects.push_back(std::unique_ptr<ObjectFile>(object));

        Json::Value& instances = objectNode["instances"];
        if (instances.size() == 0)
        {
            object->AddObjectInstance(affine3::identity());
        }
        else
        {
            for (auto& instanceNode : instances)
            {
                if (instanceNode.isArray() && instanceNode.size() == 16)
                {
                    float4x4 matrix;
                    for (uint32_t component = 0; component < 16; component++)
                        matrix.m_data[component] = float(instanceNode[component].asDouble());
                    object->AddObjectInstance(homogeneousToAffine(matrix));
                }
                else if (instanceNode.isObject())
                {
                    float3 translation = json::Read<float3>(instanceNode["translation"], 0.f);
                    float3 scaling = json::Read<float3>(instanceNode["scaling"], 1.f);
                    float3 rotation = json::Read<float3>(instanceNode["rotation"], 0.f);

                    dm::affine3 transform = dm::affine3::identity();
                    if (any(rotation != 0.f))
                    {
                        rotation = dm::radians(rotation);
                        transform *= dm::yawPitchRoll(rotation.x, rotation.y, rotation.z);
                    }
                    transform *= dm::scaling(scaling);
                    transform *= dm::translation(translation);

                    object->AddObjectInstance(transform);
                }
            }
        }

        std::filesystem::path fileName = rootPath / std::filesystem::path(objectNode["file"].asString());

        std::filesystem::path materialFileName = objectNode["materials"].asString();
        if (!materialFileName.empty())
            materialFileName = rootPath / materialFileName;

        ObjectLoadFlags::Enum flags = ObjectLoadFlags::DEFAULT;
        
        // Default to true because .fscene files use that model and do not specify anything
        if (json::Read<bool>(objectNode["metal-rough"], true))
            flags = static_cast<ObjectLoadFlags::Enum>(flags | ObjectLoadFlags::METAL_ROUGH_MODEL);

        taskGroup.run([this, &textureCache, &success, &taskGroup, object, attributeMask, fileName, materialFileName, flags]()
        {
            if (!object->Load(fileName, materialFileName, attributeMask, textureCache, g_LoadingStats, taskGroup, flags))
            {
                success = false;
                return;
            }

            ++g_LoadingStats.ObjectsLoaded;
        });
    }

    taskGroup.wait();

    return success;
}

bool Scene::LoadLights(Json::Value& lightListNode)
{
    for (auto& lightNode : lightListNode)
    {
        if(!lightNode.isObject())
            continue;

        Json::Value& typeNode = lightNode["type"];
        if(!typeNode.isString())
            continue;

        std::string lightType = typeNode.asString();
        if (lightType == "dir_light" || lightType == "directional")
        {
            std::shared_ptr<DirectionalLight> light = std::make_shared<DirectionalLight>();

            light->name = json::Read<std::string>(lightNode["name"], light->name);
            light->direction = normalize(json::Read<float3>(lightNode["direction"], light->direction));
            light->angularSize = json::Read<float>(lightNode["angularSize"], light->angularSize);

            if (lightNode["intensity"].isArray())
            {
                // .fscene version
                light->irradiance = 1.f;
                light->color = json::Read<float3>(lightNode["intensity"], light->color);
            }
            else
            {
                light->irradiance = json::Read<float>(lightNode["irradiance"], light->irradiance);
                light->color = json::Read<float3>(lightNode["color"], light->color);
            }

            Lights.push_back(light);
        }
        else if (lightType == "point_light" || lightType == "point" || lightType == "omni")
        {
            std::shared_ptr<PointLight> light = std::make_shared<PointLight>();

            light->name = json::Read<std::string>(lightNode["name"], light->name);
            light->position = json::Read<float3>(lightNode["position"], json::Read<float3>(lightNode["pos"], light->position));
            light->radius = json::Read<float>(lightNode["radius"], light->radius);
            light->range = json::Read<float>(lightNode["range"], light->range);

            if (lightNode["intensity"].isArray())
            {
                // .fscene version
                light->flux = 1.f;
                light->color = json::Read<float3>(lightNode["intensity"], light->color);
            }
            else
            {
                light->flux = json::Read<float>(lightNode["flux"], light->flux);
                light->color = json::Read<float3>(lightNode["color"], light->color);
            }

            Lights.push_back(light);
        }
        else if (lightType == "spot_light" || lightType == "spot")
        {
            std::shared_ptr<SpotLight> light = std::make_shared<SpotLight>();

            light->name = json::Read<std::string>(lightNode["name"], light->name);
            light->position = json::Read<float3>(lightNode["position"], json::Read<float3>(lightNode["pos"],  light->position));
            light->direction = normalize(json::Read<float3>(lightNode["direction"], light->direction));
            light->flux = json::Read<float>(lightNode["flux"], light->flux);
            light->color = json::Read<float3>(lightNode["color"], light->color);
            light->innerAngle = json::Read<float>(lightNode["innerAngle"], light->innerAngle);
            light->outerAngle = json::Read<float>(lightNode["outerAngle"], light->outerAngle);
            light->radius = json::Read<float>(lightNode["radius"], light->radius);
            light->range = json::Read<float>(lightNode["range"], light->range);

            Lights.push_back(light);
        }
        else
            continue;
    }

    return true;
}

bool Scene::LoadCameras(Json::Value& cameraListNode, Json::Value& activeCameraNode)
{
    for (auto& cameraNode : cameraListNode)
    {
        if (!cameraNode.isObject())
            continue;

        std::shared_ptr<CameraPreset> camera = std::make_shared<CameraPreset>();

        camera->name = json::Read<std::string>(cameraNode["name"], camera->name);
        camera->position = json::Read<float3>(cameraNode["pos"], camera->position);
        camera->lookAt = json::Read<float3>(cameraNode["target"], camera->lookAt);
        camera->up = json::Read<float3>(cameraNode["up"], camera->up);

        float focalLength = json::Read<float>(cameraNode["focal_length"], 21.f);
        float fovYFromFocalLength = dm::degrees(2.f * atanf(12.f / focalLength));
        camera->verticalFov = json::Read<float>(cameraNode["fovY"], fovYFromFocalLength);

        Cameras.push_back(camera);
    }

    if (activeCameraNode.isString())
    {
        DefaultCamera = GetCameraByName(activeCameraNode.asString());
    }

    return true;
}

std::shared_ptr<Light> Scene::GetLightByName(const std::string& name)
{
    for (auto light : Lights)
        if (light->name == name)
            return light;

    return nullptr;
}

std::shared_ptr<CameraPreset> Scene::GetCameraByName(const std::string& name)
{
    for (auto camera : Cameras)
        if (camera->name == name)
            return camera;

    return nullptr;
}

void Scene::CreateRenderingResources(nvrhi::IDevice* device)
{
    nvrhi::CommandListHandle commandList = device->createCommandList();
    commandList->open();

    for (auto& object : m_objects)
        object->CreateRenderingResources(device, commandList);

    commandList->close();
    device->executeCommandList(commandList);
}

uint32_t Scene::GetNumObjects()
{
    return (uint32_t)m_objects.size();
}

ObjectFile* donut::engine::Scene::CreateObjectFile()
{
    return new ObjectFile(m_fs);
}

