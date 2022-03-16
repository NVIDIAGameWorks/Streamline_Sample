#pragma once

#include <donut/core/math/math.h>
#include <donut/core/json.h>
#include <memory>
#include <unordered_map>
#include <optional>
#include <json/value.h>

namespace donut::engine::animation
{
    using namespace std;
    using namespace donut::math;

    class AbstractTrack
    {
    public:
        virtual void Load(Json::Value& node) { }
        virtual float GetStartTime() const { return 0.f; }
        virtual float GetEndTime() const { return 0.f; }
    };

    enum class InterpolationMode
    {
        Step,
        Linear,
        Spline
    };

    template<typename T>
    struct Keyframe
    {
        float time;
        T value;
    };

    template<typename T> constexpr T Zero() { return T(0); }
    template<> constexpr float2 Zero() { return float2(0.f); }
    template<> constexpr float3 Zero() { return float3(0.f); }
    
    template<typename T> T Interpolate(InterpolationMode mode, T a, T b, T c, T d, float u)
    {
        switch (mode)
        {
        case InterpolationMode::Step:
            return b;

        case InterpolationMode::Linear:
            return lerp(b, c, u);

        case InterpolationMode::Spline: {
            // Catmull-Rom spline, see https://en.wikipedia.org/wiki/Cubic_Hermite_spline#Interpolation_on_the_unit_interval_with_matched_derivatives_at_endpoints
            // a = p[n-1], b = p[n], c = p[n+1], d = p[n+2]
            T i = -a + 3.f * b - 3.f * c + d;
            T j = 2.f * a - 5.f * b + 4.f * c - d;
            T k = -a + c;
            return 0.5f * ((i * u + j) * u + k) * u + b;
        }

        default:
            assert(!"Unknown interpolation mode");
            return b;
        }
    }

    template<> bool Interpolate(InterpolationMode mode, bool a, bool b, bool c, bool d, float u) { return b; }
    template<> int Interpolate(InterpolationMode mode, int a, int b, int c, int d, float u) 
    { 
        if (mode == InterpolationMode::Step)
            return b;
        else
            return int(lerp(float(b), float(c), u)); 
    }

    template<typename T>
    class Track : public AbstractTrack
    {
    private:
        std::vector<Keyframe<T>> m_Keyframes;
        InterpolationMode m_Mode = InterpolationMode::Step;

    public:
        optional<T> Evaluate(float time, bool extrapolateLastValues = false)
        {
            size_t count = m_Keyframes.size();

            if (count == 0)
                return optional<T>();

            if(time <= m_Keyframes[0].time)
                return optional<T>(m_Keyframes[0].value);

            if (m_Keyframes.size() == 1 || time >= m_Keyframes[count - 1].time)
            {
                if (extrapolateLastValues)
                    return optional<T>(m_Keyframes[count - 1].value);
                else
                    return optional<T>();
            }

            for (size_t offset = 0; offset < count; offset++)
            {
                float tb = m_Keyframes[offset].time;
                float tc = m_Keyframes[offset + 1].time;

                if (tb <= time && time < tc)
                {
                    T b = m_Keyframes[offset].value;
                    T c = m_Keyframes[offset + 1].value;
                    T a = (offset > 0) ? m_Keyframes[offset - 1].value : b;
                    T d = (offset < count - 2) ? m_Keyframes[offset + 2].value : c;
                    float u = (time - tb) / (tc - tb);

                    T y = Interpolate(m_Mode, a, b, c, d, u);

                    return optional<T>(y);
                }
            }

            // shouldn't get here if the keyframes are properly ordered in time
            return optional<T>();
        }

        virtual void Load(Json::Value& node) override
        {
            if (node["mode"].isString())
            {
                string mode = node["mode"].asString();
                if (mode == "step")
                    m_Mode = InterpolationMode::Step;
                else if (mode == "linear")
                    m_Mode = InterpolationMode::Linear;
                if (mode == "spline")
                    m_Mode = InterpolationMode::Spline;
            }

            Json::Value& valuesNode = node["values"];
            if (valuesNode.isArray())
            {
                for (Json::Value& valueNode : valuesNode)
                {
                    Keyframe<T> keyframe;
                    keyframe.time = valueNode["time"].asFloat();
                    keyframe.value = json::Read<T>(valueNode["value"], Zero<T>());
                    m_Keyframes.push_back(keyframe);
                }

                sort(m_Keyframes.begin(), m_Keyframes.end(), [](const Keyframe<T>& a, const Keyframe<T>&b) { return a.time < b.time; });
            }
        }

        virtual float GetStartTime() const override
        {
            if (m_Keyframes.size() > 0)
                return m_Keyframes[0].time;

            return 0.f;
        }

        virtual float GetEndTime() const override
        {
            if (m_Keyframes.size() > 0)
                return m_Keyframes[m_Keyframes.size() - 1].time;

            return 0.f;
        }
    };

    class Sequence
    {
    private:
        unordered_map<string, shared_ptr<AbstractTrack>> m_Tracks;
        float m_Duration = 0.f;

    public:
        template<typename T>
        shared_ptr<Track<T>> GetTrack(const std::string& name)
        {
            shared_ptr<AbstractTrack> atrack = m_Tracks[name];
            
            if (!atrack)
                return nullptr;

            return dynamic_pointer_cast<Track<T>>(atrack);
        }

        template<typename T>
        optional<T> Evaluate(const std::string& name, float time, bool extrapolateLastValues = false)
        {
            shared_ptr<Track<T>> track = GetTrack<T>(name);
            if (!track)
                return optional<T>();

            return track->Evaluate(time, extrapolateLastValues);
        }

        void Load(Json::Value& node)
        {
            m_Duration = 0.f;

            for (auto& trackNode : node)
            {
                string type = trackNode["type"].asString();
                string name = trackNode["name"].asString();

                shared_ptr<AbstractTrack> track;
                if (type == "bool")
                    track = make_shared<Track<bool>>();
                else if(type == "int")
                    track = make_shared<Track<int>>();
                else if(type == "float")
                    track = make_shared<Track<float>>();
                else if (type == "float2")
                    track = make_shared<Track<float2>>();
                else if (type == "float3")
                    track = make_shared<Track<float3>>();
                else
                {
                    // throw an exception or something;
                    continue;
                }

                track->Load(trackNode);
                m_Tracks[name] = track;

                float trackEndTime = track->GetEndTime();
                m_Duration = std::max(m_Duration, trackEndTime);
            }
        }

        float GetDuration() const
        {
            return m_Duration;
        }
    };
}
