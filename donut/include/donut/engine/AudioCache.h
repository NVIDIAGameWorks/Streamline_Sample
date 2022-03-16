#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
#include <string>

#ifdef _WIN32
#define HAVE_PPL
#endif

#ifdef HAVE_PPL
#include <ppl.h>
#endif

namespace donut::vfs
{
    class IBlob;
    class IFileSystem;
}

namespace donut::engine::audio
{

class AudioCache;

// AudioData : handle issued by the AudioCache with basic interface to
// audio sample data.
//
class AudioData
{
public:

    // duration of the sample (in seconds)
    float duration() const { return float(samplesSize) / float(byteRate); }

    uint32_t nsamples() const { return samplesSize / (bitsPerSample * nchannels); }

    // true if the audia data is playable
    bool valid() const { return m_data && samples; }

public:

    enum class Format
    {
        WAVE_UNDEFINED = 0,
        WAVE_PCM_INTEGER = 1
    } format = Format::WAVE_UNDEFINED;

    uint32_t nchannels = 0,          // 1 = mono, 2 = stereo, ...
             sampleRate = 0,         // in Hz
             byteRate = 0;           // = sampleRate * nchannels * bitsPerSample / 8

    uint16_t bitsPerSample = 0,
             blockAlignment = 0;     // = nchacnnels * bitsPerSample / 8

    uint32_t samplesSize = 0;        // size in bytes of the samples data

    void const * samples = nullptr;  // pointer to samples data start in m_data

private:

    friend class AudioCache;

    std::shared_ptr<donut::vfs::IBlob> m_data;
};

// AudioCache : cache for audio data with synch & async read from 
// donut vfs::IFileSystem
//
// XXXX manuelk : we'll have to replace MSVC concurrency::task_group with
// std::thread for cross-platform compatibility
//
class AudioCache
{
public:

    AudioCache(std::shared_ptr<vfs::IFileSystem> fs);

    // Release all cached audio files
    void Reset();

public:

    // Synchronous read
    std::shared_ptr<AudioData const> LoadFromFile(const std::filesystem::path & path);

#ifdef HAVE_PPL
    // Asynchronous read
    std::shared_ptr<AudioData const> LoadFromFileAsync(const std::filesystem::path & path, concurrency::task_group & taskGroup);
#endif

private:

    static std::shared_ptr<AudioData const> importRiff(std::shared_ptr<donut::vfs::IBlob> blob, char const * filepath);

    std::shared_ptr<AudioData const> loadAudioFile (const std::filesystem::path & path);

    bool findInCache(const std::filesystem::path & path, std::shared_ptr<AudioData const> & result);

    void sendAudioLoadedMessage(std::shared_ptr<AudioData const> audio, char const * path);

private:

    std::mutex m_LoadedDataMutex;

    std::map<std::string, std::shared_ptr<AudioData const>> m_LoadedAudioData;

    std::shared_ptr<donut::vfs::IFileSystem> m_fs;
};

} // namespace donut::engine::audio
