#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace isolation
{
constexpr int maxChannels = 16, maxSamples = 8192;
constexpr uint32_t protocolVersion = 1;

// Windows Interlocked operations on aligned, cached, process-shared memory.
struct alignas (4) SharedWord
{
    int32_t load() const noexcept;
    void store (int32_t) noexcept;
    void increment() noexcept;
    volatile int32_t value = 0;
};

struct SharedAudio
{
    uint32_t version = protocolVersion;
    SharedWord request, complete, activity, revision, latency, rendered;
    int samples = 0, channels = 0;
    int64_t samplePosition = 0;
    float input[maxChannels][maxSamples] {};
    float output[maxChannels][maxSamples] {};
};

// One deadline for the whole callback, so waiting cannot multiply with chain length.
class CallbackBudget
{
public:
    explicit CallbackBudget (double milliseconds);
    ~CallbackBudget();
    static double deadline (double fallbackMilliseconds);
private:
    double previous;
};

class AudioTransport
{
public:
    AudioTransport();
    ~AudioTransport();
    bool create (juce::String& error);
    bool open (const juce::String& name, juce::String& error);
    bool attachWorker (uint32_t processId, juce::String& error);
    void terminateWorker(); // Non-realtime only. Own job/process handles, never a reused PID.
    bool workerExited() const noexcept;
    juce::String name() const { return token; }
    SharedAudio* data() const { return shared; }

    enum class AudioResult { complete, pending, busy, dead, invalid };
    AudioResult exchange (juce::AudioBuffer<float>&, int inputs, int64_t position, double deadlineMs) noexcept;
    double pendingAge() const noexcept;
    bool waitForRequest (int timeoutMs) const noexcept;
    void completeRequest (int32_t sequence) noexcept;
    void wakeWorker() noexcept;
    static uint32_t currentProcessId() noexcept;
private:
    struct Native;
    std::unique_ptr<Native> native;
    juce::String token;
    SharedAudio* shared = nullptr;
    int32_t sequence = 0, pending = 0;
    std::atomic<double> pendingStarted { 0 };
};

// Registers only this worker's audio thread with MMCSS; changes no system settings.
class AudioPriority
{
public:
    AudioPriority();
    ~AudioPriority();
private:
    struct Native;
    std::unique_ptr<Native> native;
};
}
