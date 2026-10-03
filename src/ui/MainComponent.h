#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "ui/LevelMeterComponent.h"
#include "ui/PluginListView.h"
#include "ui/DevicePanel.h"
#include "state/AutostartRegistry.h"
#include "net/UpdateChecker.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AudioEngine&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void setUpdateCheckEnabled (bool on, bool runIfOn);
    std::function<void (bool)> onUpdateCheckToggled;
    std::function<void (const juce::String&, const juce::String&)> onUpdateFound;
private:
    void timerCallback() override;
    void refreshStatus();
    void showHowTo();
    void startUpdateCheck();
    AudioEngine& engine;
    DevicePanel devicePanel;
    PluginListView pluginList;
    LevelMeterComponent inMeter, outMeter;
    juce::Label inLabel { {}, "MIC INPUT" }, outLabel { {}, "PROCESSED OUTPUT" };
    juce::Label inReading, outReading, status, performance;
    juce::TextButton muteButton { "Mute mic" }, bypassButton { "Bypass effects" }, howToButton { "Help" };
    juce::ToggleButton autostartToggle { "Start with Windows" }, updateToggle { "Check for updates" };
    juce::HyperlinkButton versionLink;
    juce::String currentVersion;
    UpdateChecker updateChecker;
    juce::TooltipWindow tooltip { nullptr, 450 };
    std::unique_ptr<juce::DocumentWindow> helpWindow;
    juce::Rectangle<int> inputMeterCard, outputMeterCard;
};
