
#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <json/reader.h>

using namespace donut::math;
using namespace donut::vfs;

namespace donut::json
{
    bool LoadFromFile(IFileSystem& fs, const std::filesystem::path& jsonFileName, Json::Value& documentRoot)
    {
        std::shared_ptr<IBlob> data = fs.readFile(jsonFileName);
        if (!data)
        {
            log::error("Couldn't read file %s", jsonFileName.generic_string().c_str());
            return false;
        }

        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        Json::CharReader* reader = builder.newCharReader();

        const char* dataPtr = static_cast<const char*>(data->data());
        std::string errors;
        bool success = reader->parse(dataPtr, dataPtr + data->size(), &documentRoot, &errors);

        if (!success)
        {
            log::error("Couldn't parse JSON file %s:\n%s", jsonFileName.generic_string().c_str(), errors.c_str());
        }

        delete reader;

        return success;
    }

    template<>
    std::string Read<std::string>(const Json::Value& node, const std::string& defaultValue)
    {
        if (node.isString())
            return node.asString();

        return defaultValue;
    }

    template<>
    int Read<int>(const Json::Value& node, const int& defaultValue)
    {
        if (node.isNumeric())
            return node.asInt();

        return defaultValue;
    }

    template<>
    bool Read<bool>(const Json::Value& node, const bool& defaultValue)
    {
        if (node.isBool())
            return node.asBool();

        if (node.isNumeric())
            return node.asFloat() != 0.f;

        return defaultValue;
    }

    template<>
    float Read<float>(const Json::Value& node, const float& defaultValue)
    {
        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    float2 Read<float2>(const Json::Value& node, const float2& defaultValue)
    {
        if (node.isArray() && node.size() == 2)
            return float2(node[0].asFloat(), node[1].asFloat());

        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }

    template<>
    float3 Read<float3>(const Json::Value& node, const float3& defaultValue)
    {
        if (node.isArray() && node.size() == 3)
            return float3(node[0].asFloat(), node[1].asFloat(), node[2].asFloat());

        if (node.isNumeric())
            return node.asFloat();

        return defaultValue;
    }
}