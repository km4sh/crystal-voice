#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace
{
    class HelpWindow : public juce::DocumentWindow
    {
    public:
        HelpWindow() : DocumentWindow ("Crystal Voice - Quick setup", theme::background, closeButton)
        {
            auto* body = new juce::TextEditor();
            body->setMultiLine (true); body->setReadOnly (true); body->setCaretVisible (false);
            body->setFont (theme::font (15));
            body->setText (
                "YOUR MICROPHONE, REFINED.\n\n"
                "1. Choose your physical microphone in Microphone. For an audio interface, choose Channel 1 or Channel 2.\n\n"
                "2. Choose CABLE Input as Destination. In your game, chat or streaming app, choose CABLE Output as the microphone.\n\n"
                "3. Add VST3 effects. Search by name or manufacturer, then Add. Use Open to edit, the grip to reorder, or the row menu to move/remove.\n\n"
                "4. Mute mic silences the output. Bypass effects sends the unprocessed mic so you can compare.\n\n"
                "5. Closing the main window keeps audio running in the tray. Right-click the tray icon to quit.\n\n"
                "RNNoise requires 48 kHz. Choose matching sample rates in your audio device settings. Reported latency is a host estimate; the cable and receiving application add their own delay.\n\n"
                "Settings are saved automatically in %APPDATA%\\CrystalVoice. Missing effects stay in the chain with their saved settings. Rescan after reinstalling a plugin.\n\n"
                "VB-CABLE: https://vb-audio.com/Cable/\nProject: https://github.com/km4sh/crystal-voice\n\n"
                "FONT LICENSE // JETBRAINS MONO 2.304\n\n"
                + juce::String::fromUTF8 (BinaryData::OFL_txt, BinaryData::OFL_txtSize), false);
            theme::window (*this); setContentOwned (body, false);
            setResizable (true, false); setResizeLimits (480, 440, 1000, 1000);
            centreWithSize (580, 620);
        }
        void closeButtonPressed() override { setVisible (false); }
    };
    juce::String levelText (LevelReading level)
    {
        if (level.vu < 0.00001f) return "-inf dB";
        return juce::String (juce::Decibels::gainToDecibels (level.vu), 1) + " dB";
    }
}

MainComponent::MainComponent (AudioEngine& e) : engine (e), devicePanel (e), pluginList (e)
{
    for (auto* component : std::initializer_list<juce::Component*> {
        &devicePanel, &pluginList, &inMeter, &outMeter, &inScale, &outScale, &inLabel, &outLabel,
        &inReading, &outReading, &status, &performance, &muteButton,
        &howToButton, &autostartToggle, &updateToggle, &versionLink }) addAndMakeVisible (component);
    for (auto* label : { &inLabel, &outLabel })
    { label->setFont (theme::font (11, true)); label->setColour (juce::Label::textColourId, theme::muted); }
    for (auto* label : { &inReading, &outReading })
    { label->setFont (theme::font (13, true)); label->setJustificationType (juce::Justification::centredRight);
      label->setText ("-inf dB", juce::dontSendNotification); }
    status.setFont (theme::font (13, true));
    status.setJustificationType (juce::Justification::centredLeft);
    performance.setFont (theme::font (12));
    performance.setColour (juce::Label::textColourId, theme::muted);
    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::danger.withAlpha (0.16f));
    muteButton.setColour (juce::TextButton::textColourOnId, theme::danger);
    muteButton.onClick = [this] { engine.setMuted (muteButton.getToggleState()); refreshStatus(); };
    howToButton.onClick = [this] { showHowTo(); };
    howToButton.setColour (juce::TextButton::textColourOffId, theme::warning);
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { AutostartRegistry::setEnabled (autostartToggle.getToggleState()); };
    autostartToggle.setTooltip ("Start silently in the tray when you sign in to Windows");
    if (juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--profile"))
    { autostartToggle.setEnabled (false); autostartToggle.setTooltip ("Startup is disabled in an isolated test profile."); }
    updateToggle.onClick = [this]
    {
        const bool enabled = updateToggle.getToggleState();
        if (onUpdateCheckToggled) onUpdateCheckToggled (enabled);
        if (enabled) startUpdateCheck();
    };
    currentVersion = juce::JUCEApplication::getInstance()->getApplicationVersion();
    versionLink.setButtonText ("v" + currentVersion);
    versionLink.setFont (theme::font (12), false);
    versionLink.setURL (juce::URL ("https://github.com/km4sh/crystal-voice"));
    versionLink.setColour (juce::HyperlinkButton::textColourId, theme::muted);
    engine.onStatusChanged = [this] { refreshStatus(); };
    refreshStatus(); setSize (1000, 700); startTimerHz (24);
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (observedWindow != nullptr) observedWindow->removeComponentListener (this);
    engine.onStatusChanged = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    const WorkspaceLayout layout (getLocalBounds());
    // Static, subdued technical grid. Only the two meters repaint at audio UI cadence.
    g.setColour (theme::border.withAlpha (0.15f));
    for (int x = 20; x < getWidth(); x += 32) g.drawVerticalLine (x, 0, (float) getHeight());
    for (int y = 20; y < getHeight(); y += 32) g.drawHorizontalLine (y, 0, (float) getWidth());
    auto title = layout.header;
    theme::caption (g, "// REALTIME VOICE PROCESSOR", title.removeFromTop (18), theme::muted);
    g.setColour (theme::accent); g.setFont (theme::font (30, true));
    g.drawText ("CRYSTAL VOICE_", title.removeFromTop (38).withWidth (420), juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::font (12));
    g.drawText ("MICROPHONE > EFFECTS > GAME / CHAT / STREAM", title.withWidth (440), juce::Justification::centredLeft);
    g.setColour (theme::border); g.drawHorizontalLine (layout.header.getBottom() + 6, 20, (float) getWidth() - 20);
    theme::card (g, inputMeterCard.toFloat()); theme::card (g, outputMeterCard.toFloat());
    g.setColour (theme::border); g.drawHorizontalLine (layout.footer.getY() - 8, 20, (float) getWidth() - 20);
}

void MainComponent::resized()
{
    const WorkspaceLayout layout (getLocalBounds());
    auto header = layout.header;
    muteButton.setBounds (header.removeFromRight (132).withHeight (34).translated (0, 22));
    header.removeFromRight (16);
    status.setBounds (header.removeFromRight (210).withHeight (34).translated (0, 22));
    auto footer = layout.footer;
    versionLink.setBounds (footer.removeFromRight (60));
    footer.removeFromRight (10); howToButton.setBounds (footer.removeFromRight (88));
    updateToggle.setBounds (footer.removeFromLeft (174));
    autostartToggle.setBounds (footer.removeFromLeft (190));
    performance.setBounds (footer.reduced (12, 0));
    devicePanel.setBounds (layout.routing);
    inputMeterCard = layout.inputMeter; outputMeterCard = layout.outputMeter;
    auto layoutMeter = [] (juce::Rectangle<int> bounds, juce::Label& label,
                            juce::Label& reading, LevelMeterComponent& meter, DbScaleComponent& scale)
    {
        auto content = bounds.reduced (12, 10);
        auto title = content.removeFromTop (20);
        reading.setBounds (title.removeFromRight (86)); label.setBounds (title);
        content.removeFromTop (6); meter.setBounds (content.removeFromTop (16));
        content.removeFromTop (4); scale.setBounds (content.removeFromTop (16));
    };
    layoutMeter (inputMeterCard, inLabel, inReading, inMeter, inScale);
    layoutMeter (outputMeterCard, outLabel, outReading, outMeter, outScale);
    pluginList.setBounds (layout.effects);
}

void MainComponent::visibilityChanged()
{
    if (isShowing()) { readoutTicks = 0; refreshStatus(); startTimerHz (24); }
    else stopTimer();
}

void MainComponent::parentHierarchyChanged()
{
    if (observedWindow != nullptr) observedWindow->removeComponentListener (this);
    auto* window = getTopLevelComponent();
    observedWindow = window != this ? window : nullptr;
    if (observedWindow != nullptr) observedWindow->addComponentListener (this);
    visibilityChanged();
}

void MainComponent::componentVisibilityChanged (juce::Component&)
{
    // JUCE does not call a child's visibilityChanged when only its window is shown.
    // Follow the window too, including first launch and returning from the tray.
    visibilityChanged();
}

void MainComponent::timerCallback()
{
    if (! isShowing()) return;
    const auto in = engine.inputLevel(), out = engine.outputLevel();
    inMeter.setLevel (in); outMeter.setLevel (out);
    // The bars retain 24 Hz motion; numbers use that same VU envelope at 6 Hz.
    if (readoutTicks++ % 4 == 0)
    {
        inReading.setText (levelText (in), juce::dontSendNotification);
        outReading.setText (levelText (out), juce::dontSendNotification);
        const auto peakText = [] (LevelReading level)
        {
            return "Sample peak: " + (level.heldPeak > 0.0f
                ? juce::String (juce::Decibels::gainToDecibels (level.heldPeak), 1) : juce::String ("-inf"))
                + " dBFS (500 ms hold, 20 dB/s release). The number shows VU average.";
        };
        inReading.setTooltip (peakText (in)); outReading.setTooltip (peakText (out));
    }
    inReading.setColour (juce::Label::textColourId, in.heldPeak >= 1.0f ? theme::danger : theme::text);
    outReading.setColour (juce::Label::textColourId, out.heldPeak >= 1.0f ? theme::danger : theme::text);
    // Registry access is limited to once per second, not every meter frame.
    static int ticks = 0;
    if (++ticks % 24 == 0)
    {
        autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
        refreshStatus();
    }
}

void MainComponent::refreshStatus()
{
    muteButton.setToggleState (engine.isMuted(), juce::dontSendNotification);
    muteButton.setButtonText (engine.isMuted() ? "[ UNMUTE ]" : "[ MUTE MIC ]");
    pluginList.refreshProcessingState();
    const bool routed = engine.isRunning() && engine.getRequestedSetup().inputDeviceName.isNotEmpty()
        && engine.getRequestedSetup().outputDeviceName.isNotEmpty();
    status.setText (engine.isMuted() ? "[ MIC MUTED ]" : engine.getDeviceError().isNotEmpty() ? "[ DEVICE ERROR ]"
        : ! routed ? "[ NOT CONNECTED ]" : engine.isMasterBypassed() ? "[ DRY MIC ]" : "[ SIGNAL LIVE ]", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, engine.isMuted() ? theme::danger
        : routed && engine.getDeviceError().isEmpty() && ! engine.isMasterBypassed() ? theme::accent : theme::warning);
    performance.setText ("AUDIO CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "%",
                         juce::dontSendNotification);
}

void MainComponent::showHowTo()
{
    if (! helpWindow) helpWindow = std::make_unique<HelpWindow>();
    helpWindow->setVisible (true); helpWindow->toFront (true);
}

void MainComponent::setUpdateCheckEnabled (bool on, bool runIfOn)
{
    updateToggle.setToggleState (on, juce::dontSendNotification);
    if (on && runIfOn) startUpdateCheck();
}

void MainComponent::startUpdateCheck()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    updateChecker.start (currentVersion, [safe] (UpdateChecker::Result result)
    {
        if (auto* self = safe.getComponent())
        {
            self->versionLink.setButtonText ("Update");
            self->versionLink.setURL (juce::URL (result.releaseUrl));
            self->versionLink.setColour (juce::HyperlinkButton::textColourId, theme::accent);
            if (self->onUpdateFound) self->onUpdateFound (result.latestVersion, result.releaseUrl);
        }
    });
}
