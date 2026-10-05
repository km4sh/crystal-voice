#include "audio/isolation/AudioTransport.h"
#include <cmath>
#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <avrt.h>
#endif

namespace isolation
{
namespace { thread_local double callbackDeadline = 0; }
CallbackBudget::CallbackBudget (double ms) : previous (callbackDeadline)
{ callbackDeadline = juce::Time::getMillisecondCounterHiRes() + juce::jmax (0.0, ms); }
CallbackBudget::~CallbackBudget() { callbackDeadline = previous; }
double CallbackBudget::deadline (double fallback)
{ return callbackDeadline > 0 ? callbackDeadline : juce::Time::getMillisecondCounterHiRes() + fallback; }

#if JUCE_WINDOWS
static_assert (sizeof (LONG) == sizeof (int32_t));
int32_t SharedWord::load() const noexcept
{ return (int32_t) InterlockedCompareExchange (reinterpret_cast<volatile LONG*> (const_cast<volatile int32_t*> (&value)), 0, 0); }
void SharedWord::store (int32_t next) noexcept { InterlockedExchange (reinterpret_cast<volatile LONG*> (&value), (LONG) next); }
void SharedWord::increment() noexcept { InterlockedIncrement (reinterpret_cast<volatile LONG*> (&value)); }
struct AudioTransport::Native
{
    HANDLE mapping = nullptr, request = nullptr, complete = nullptr, process = nullptr, job = nullptr;
    ~Native() { for (auto handle : { job, process, request, complete, mapping }) if (handle != nullptr) CloseHandle (handle); }
};
struct AudioPriority::Native { HANDLE task = nullptr; };
AudioPriority::AudioPriority() : native (std::make_unique<Native>())
{ DWORD index = 0; native->task = AvSetMmThreadCharacteristicsW (L"Pro Audio", &index); if (native->task != nullptr) AvSetMmThreadPriority (native->task, AVRT_PRIORITY_HIGH); }
AudioPriority::~AudioPriority() { if (native->task != nullptr) AvRevertMmThreadCharacteristics (native->task); }
uint32_t AudioTransport::currentProcessId() noexcept { return (uint32_t) GetCurrentProcessId(); }
#else
int32_t SharedWord::load() const noexcept { return value; }
void SharedWord::store (int32_t next) noexcept { value = next; }
void SharedWord::increment() noexcept { ++value; }
struct AudioTransport::Native {};
struct AudioPriority::Native {};
AudioPriority::AudioPriority() = default;
AudioPriority::~AudioPriority() = default;
uint32_t AudioTransport::currentProcessId() noexcept { return 0; }
#endif

AudioTransport::AudioTransport() : native (std::make_unique<Native>()) {}
AudioTransport::~AudioTransport()
{
    terminateWorker();
   #if JUCE_WINDOWS
    if (shared != nullptr) UnmapViewOfFile (shared);
   #endif
}
bool AudioTransport::create (juce::String& error)
{
   #if JUCE_WINDOWS
    token = "Local\\CrystalVoiceAudio-" + juce::Uuid().toString().removeCharacters ("-");
    native->mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (SharedAudio), token.toWideCharPointer());
    native->request = CreateEventW (nullptr, FALSE, FALSE, (token + "-request").toWideCharPointer());
    native->complete = CreateEventW (nullptr, FALSE, FALSE, (token + "-complete").toWideCharPointer());
    if (native->mapping != nullptr) shared = static_cast<SharedAudio*> (MapViewOfFile (native->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedAudio)));
    if (shared != nullptr && native->request != nullptr && native->complete != nullptr)
    { new (shared) SharedAudio(); error.clear(); return true; }
   #endif
    error = "Cannot create the isolated audio transport."; return false;
}
bool AudioTransport::open (const juce::String& name, juce::String& error)
{
   #if JUCE_WINDOWS
    token = name;
    native->mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, token.toWideCharPointer());
    native->request = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (token + "-request").toWideCharPointer());
    native->complete = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (token + "-complete").toWideCharPointer());
    if (native->mapping != nullptr) shared = static_cast<SharedAudio*> (MapViewOfFile (native->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedAudio)));
    if (shared != nullptr && native->request != nullptr && native->complete != nullptr && shared->version == protocolVersion)
    { error.clear(); return true; }
   #endif
    error = "Cannot connect the isolated audio transport."; return false;
}
bool AudioTransport::attachWorker (uint32_t pid, juce::String& error)
{
   #if JUCE_WINDOWS
    native->process = OpenProcess (SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | PROCESS_SET_QUOTA, FALSE, (DWORD) pid);
    native->job = CreateJobObjectW (nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (native->process != nullptr && native->job != nullptr
        && SetInformationJobObject (native->job, JobObjectExtendedLimitInformation, &limits, sizeof (limits))
        && AssignProcessToJobObject (native->job, native->process)) { error.clear(); return true; }
    // A worker is never used without lifetime ownership. Clean up a failed launch.
    if (native->process != nullptr) TerminateProcess (native->process, 1);
   #endif
    error = "Cannot attach the plugin to its lifetime job."; return false;
}
void AudioTransport::terminateWorker()
{
   #if JUCE_WINDOWS
    if (native->job != nullptr) TerminateJobObject (native->job, 0);
   #endif
}
bool AudioTransport::workerExited() const noexcept
{
   #if JUCE_WINDOWS
    return native->process == nullptr || WaitForSingleObject (native->process, 0) == WAIT_OBJECT_0;
   #else
    return true;
   #endif
}
double AudioTransport::pendingAge() const noexcept
{
    const auto started = pendingStarted.load();
    return started > 0 && shared != nullptr && shared->request.load() != shared->complete.load()
        ? juce::Time::getMillisecondCounterHiRes() - started : 0.0;
}
AudioTransport::AudioResult AudioTransport::exchange (juce::AudioBuffer<float>& buffer, int inputs, int64_t position, double deadlineMs) noexcept
{
   #if JUCE_WINDOWS
    if (shared == nullptr || workerExited()) return AudioResult::dead;
    const int samples = buffer.getNumSamples(), channels = buffer.getNumChannels();
    if (samples <= 0 || samples > maxSamples || channels > maxChannels || inputs > channels) return AudioResult::invalid;
    if (shared->activity.load() != 0) return AudioResult::busy;
    if (pending != 0)
    {
        if (shared->complete.load() != pending) return AudioResult::pending;
        pending = 0; pendingStarted = 0; // Late output belongs to an older block. Never replay it.
    }
    if (juce::Time::getMillisecondCounterHiRes() >= deadlineMs) return AudioResult::pending;
    for (int ch = 0; ch < channels; ++ch)
        if (ch < inputs) std::memcpy (shared->input[ch], buffer.getReadPointer (ch), (size_t) samples * sizeof (float));
        else std::memset (shared->input[ch], 0, (size_t) samples * sizeof (float));
    shared->samples = samples; shared->channels = channels; shared->samplePosition = position;
    sequence = sequence == INT32_MAX ? 1 : sequence + 1;
    pending = sequence; pendingStarted = juce::Time::getMillisecondCounterHiRes();
    ResetEvent (native->complete); shared->request.store (pending); SetEvent (native->request);
    while (shared->complete.load() != pending)
    {
        const double remaining = deadlineMs - juce::Time::getMillisecondCounterHiRes();
        if (remaining <= 0) return AudioResult::pending;
        HANDLE handles[] { native->complete, native->process };
        const auto result = WaitForMultipleObjects (2, handles, FALSE, (DWORD) juce::jmax (0, (int) remaining));
        if (result == WAIT_OBJECT_0 + 1) return AudioResult::dead;
        if (result == WAIT_FAILED) return AudioResult::invalid;
        if (result == WAIT_TIMEOUT && remaining < 1.0) return AudioResult::pending;
    }
    for (int ch = 0; ch < channels; ++ch)
        std::memcpy (buffer.getWritePointer (ch), shared->output[ch], (size_t) samples * sizeof (float));
    pending = 0; pendingStarted = 0; return AudioResult::complete;
   #else
    juce::ignoreUnused (buffer, inputs, position, deadlineMs); return AudioResult::dead;
   #endif
}
bool AudioTransport::waitForRequest (int timeoutMs) const noexcept
{
   #if JUCE_WINDOWS
    return WaitForSingleObject (native->request, (DWORD) timeoutMs) == WAIT_OBJECT_0;
   #else
    juce::ignoreUnused (timeoutMs); return false;
   #endif
}
void AudioTransport::completeRequest (int32_t completed) noexcept
{
    shared->complete.store (completed);
   #if JUCE_WINDOWS
    SetEvent (native->complete);
   #endif
}
void AudioTransport::wakeWorker() noexcept
{
   #if JUCE_WINDOWS
    if (native->request != nullptr) SetEvent (native->request);
   #endif
}
}
