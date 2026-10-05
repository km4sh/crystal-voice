#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "audio/isolation/AudioTransport.h"
#include "audio/isolation/Protocol.h"
#include <deque>

namespace isolation
{
class PluginWorker final : private juce::AsyncUpdater,
                           private juce::AudioProcessorListener
{
public:
    // Tests inject native-fault fixtures into the worker executable only.
    using Factory = std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, double, int, juce::String&)>;
    explicit PluginWorker (std::function<void()> quit, Factory = {});
    ~PluginWorker() override;
    bool connect (const juce::String& commandLine);
    static bool isWorkerCommandLine (const juce::String& commandLine);
private:
    void handleConnectionMade();
    void handleConnectionLost();
    void handleMessageFromCoordinator (const juce::MemoryBlock&);
    void sendMessageToCoordinator (const juce::MemoryBlock&);
    void handleAsyncUpdate() override;
    void execute (const Packet&);
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void prepare (double, int);
    void reply (const Packet&, const juce::MemoryBlock&, bool success = true);

    struct PlayingHead final : juce::AudioPlayHead
    {
        std::atomic<int64_t> samples { 0 };
        std::atomic<double> rate { 48000 };
        juce::Optional<PositionInfo> getPosition() const override;
    } head;
    struct AudioThread;
    struct EditorWindow;
    struct Connection;
    std::unique_ptr<Connection> connection;
    std::function<void()> onQuit;
    Factory factory;
    AudioTransport transport;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> processor;
    std::unique_ptr<AudioThread> audio;
    std::unique_ptr<EditorWindow> editor;
    juce::CriticalSection queueLock;
    std::deque<Packet> commands;
    std::atomic<bool> disconnected { false };
};
}
