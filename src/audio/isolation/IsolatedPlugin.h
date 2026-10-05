#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/isolation/AudioTransport.h"
#include <array>

// The host owns only this proxy. The DLL, processor, parameters and editor live
// in a distinct process for every instance, including duplicate plugin classes.
class IsolatedPlugin final : public juce::AudioPluginInstance, private juce::Timer
{
public:
    static std::unique_ptr<IsolatedPlugin> create (const juce::PluginDescription&, double rate, int block, juce::String& error);
    ~IsolatedPlugin() override;
    bool failed() const;
    juce::String failureReason() const;
    uint32_t workerProcessId() const;
    uint64_t missedBlocks() const { return deadlineMisses.load(); }
    int renderedBlocks() const;
    bool showRemoteEditor (juce::String& error);
    void hideRemoteEditor();
    const juce::MemoryBlock& lastGoodState() const;

    void fillInPluginDescription (juce::PluginDescription&) const override;
    const juce::String getName() const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    bool hasEditor() const override { return false; } // Open through showRemoteEditor().
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
private:
    struct Bridge;
    IsolatedPlugin (const juce::PluginDescription&, std::unique_ptr<Bridge>);
    void timerCallback() override;
    std::unique_ptr<Bridge> bridge;
    juce::PluginDescription description;
    std::array<float, isolation::maxChannels> lastSample {};
    juce::SmoothedValue<float> activityGain;
    std::atomic<uint64_t> deadlineMisses { 0 };
    int remoteRevision = 0;
    double preparedRate = 48000;
    int preparedBlock = 480;
};
