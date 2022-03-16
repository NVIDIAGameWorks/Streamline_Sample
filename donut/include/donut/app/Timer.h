#pragma once
#include <chrono>

namespace donut::app
{
    class HiResTimer
    {
    public:

        std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
        std::chrono::time_point<std::chrono::high_resolution_clock> stopTime;

        HiResTimer()
        {
        }

        void Start()
        {
            startTime = std::chrono::high_resolution_clock::now();
        }

        void Stop()
        {
            stopTime = std::chrono::high_resolution_clock::now();
        }

        double Seconds()
        {
            auto duration = stopTime - startTime;
            return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() * 1e-9;
        }

        double Milliseconds()
        {
            return Seconds() * 1e3;
        }
    };
}