#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdlib>

namespace isolationTest
{
inline juce::PluginDescription description (const juce::String& mode)
{
    juce::PluginDescription desc;
    desc.name = "Isolated gain"; desc.pluginFormatName = "VST3"; desc.manufacturerName = "Crystal Voice Tests";
    desc.fileOrIdentifier = "fixture:" + mode; desc.uniqueId = mode.hashCode(); desc.numInputChannels = desc.numOutputChannels = 1;
    return desc;
}
class Plugin final : public juce::AudioPluginInstance
{
public:
    explicit Plugin (juce::PluginDescription description)
        : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::mono(), true)
                                               .withOutput ("Out", juce::AudioChannelSet::mono(), true)), desc (std::move (description)) {}
    void fillInPluginDescription (juce::PluginDescription& output) const override { output = desc; }
    const juce::String getName() const override { return desc.name; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        ++renders;
        if (desc.fileOrIdentifier.contains ("native-crash") && renders >= 3)
            *reinterpret_cast<volatile int*> (uintptr_t (1)) = 7;
        if (desc.fileOrIdentifier.contains ("process-crash") && renders >= 3) std::_Exit (72);
        if (desc.fileOrIdentifier.contains ("process-hang")) for (;;) juce::Thread::sleep (100);
        if (desc.fileOrIdentifier.contains ("delayed")) juce::Thread::sleep (30);
        buffer.applyGain (gain.load());
    }
    void getStateInformation (juce::MemoryBlock& result) override
    {
        if (captures++ > 0)
        {
            if (desc.fileOrIdentifier.contains ("state-hang")) for (;;) juce::Thread::sleep (100);
            if (desc.fileOrIdentifier.contains ("state-crash")) std::_Exit (73);
            if (desc.fileOrIdentifier.contains ("state-slow")) juce::Thread::sleep (80);
        }
        const float value = gain.load(); result.replaceAll (&value, sizeof (value));
    }
    void setStateInformation (const void* data, int bytes) override
    { if (desc.fileOrIdentifier.contains ("set-crash")) std::_Exit (74);
      if (bytes == 4) { float value; std::memcpy (&value, data, 4); gain = value; updateHostDisplay (juce::AudioProcessorListener::ChangeDetails{}.withNonParameterStateChanged (true)); } }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
private:
    juce::PluginDescription desc;
    std::atomic<float> gain { 0.5f };
    int captures = 0, renders = 0;
};
inline std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& desc, double, int, juce::String& error)
{
    if (desc.fileOrIdentifier.contains ("load-crash")) std::_Exit (71);
    if (! desc.fileOrIdentifier.startsWith ("fixture:")) { error = "Unknown isolation fixture."; return {}; }
    return std::make_unique<Plugin> (desc);
}
}
