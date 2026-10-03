#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"

class DevicePanel : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit DevicePanel (AudioEngine&);
    ~DevicePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const { return 214; }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void updateStatus();
    void applyRoute (const juce::String& input, const juce::String& output, double rate = 0, int buffer = 0);
    AudioEngine& engine;
    juce::ComboBox inputBox, outputBox, channelBox, rateBox, bufferBox;
    juce::TextButton retryButton { "Retry" };
    juce::Label inputHint, outputHint, rateLabel { {}, "Sample rate" }, bufferLabel { {}, "Buffer" }, statusLabel;
    juce::Array<double> rates;
    juce::Array<int> buffers;
    juce::Rectangle<int> inputCard, outputCard;
    bool updating = false, refreshPending = false;
};
