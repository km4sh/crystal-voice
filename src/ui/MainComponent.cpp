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
                "VB-CABLE: https://vb-audio.com/Cable/\nProject: https://github.com/km4sh/crystal-voice", false);
            setUsingNativeTitleBar (true); setContentOwned (body, false);
            setResizable (true, false); setResizeLimits (480, 440, 1000, 1000);
            centreWithSize (580, 620);
        }
        void closeButtonPressed() override { setVisible (false); }
    };
    juce::String levelText (LevelReading level)
    {
        if (level.peak < 0.00001f) return "Silence";
        return juce::String (juce::Decibels::gainToDecibels (level.peak), 1) + " dB";
    }
}

MainComponent::MainComponent (AudioEngine& e) : engine (e), devicePanel (e), pluginList (e)
{
    for (auto* component : std::initializer_list<juce::Component*> {
        &devicePanel, &pluginList, &inMeter, &outMeter, &inLabel, &outLabel,
        &inReading, &outReading, &status, &performance, &muteButton, &bypassButton,
        &howToButton, &autostartToggle, &updateToggle, &versionLink }) addAndMakeVisible (component);
    for (auto* label : { &inLabel, &outLabel })
    { label->setFont (theme::font (11, true)); label->setColour (juce::Label::textColourId, theme::muted); }
    for (auto* label : { &inReading, &outReading })
    { label->setFont (theme::font (13, true)); label->setJustificationType (juce::Justification::centredRight);
      label->setText ("Silence", juce::dontSendNotification); }
    status.setFont (theme::font (13, true));
    status.setJustificationType (juce::Justification::centredRight);
    performance.setFont (theme::font (12));
    performance.setColour (juce::Label::textColourId, theme::muted);
    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
    muteButton.onClick = [this] { engine.setMuted (muteButton.getToggleState()); refreshStatus(); };
    bypassButton.setClickingTogglesState (true);
    bypassButton.onClick = [this] { engine.setMasterBypass (bypassButton.getToggleState()); refreshStatus(); };
    howToButton.onClick = [this] { showHowTo(); };
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
    versionLink.setURL (juce::URL ("https://github.com/km4sh/crystal-voice"));
    versionLink.setColour (juce::HyperlinkButton::textColourId, theme::muted);
    engine.onStatusChanged = [this] { refreshStatus(); };
    refreshStatus(); setSize (900, 760); startTimerHz (24);
}

MainComponent::~MainComponent() { stopTimer(); engine.onStatusChanged = nullptr; }

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    auto mark = juce::Rectangle<float> (24, 23, 46, 46);
    g.setColour (theme::accent); g.fillRoundedRectangle (mark, 13);
    g.setColour (theme::background);
    for (int i = 0; i < 5; ++i)
    {
        const float height = i == 2 ? 26.0f : (i % 2 ? 18.0f : 10.0f);
        g.fillRoundedRectangle (33.0f + i * 6.0f, 46 - height / 2, 3.0f, height, 1.5f);
    }
    g.setColour (theme::text); g.setFont (theme::font (25, true));
    g.drawText ("Crystal Voice", 84, 20, 250, 34, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::font (13));
    g.drawText ("Your microphone, refined.", 85, 54, 280, 20, juce::Justification::centredLeft);
    theme::card (g, inputMeterCard.toFloat(), 12);
    theme::card (g, outputMeterCard.toFloat(), 12);
    g.setColour (theme::border); g.drawHorizontalLine (getHeight() - 58, 24.0f, (float) getWidth() - 24);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (24);
    auto header = r.removeFromTop (64);
    muteButton.setBounds (header.removeFromRight (106).withHeight (36).translated (0, 13));
    header.removeFromRight (10);
    status.setBounds (header.removeFromRight (164).withHeight (36).translated (0, 13));
    r.removeFromTop (20);
    auto footer = r.removeFromBottom (34);
    versionLink.setBounds (footer.removeFromRight (64));
    footer.removeFromRight (10); howToButton.setBounds (footer.removeFromRight (60));
    updateToggle.setBounds (footer.removeFromLeft (165));
    autostartToggle.setBounds (footer.removeFromLeft (185));
    performance.setBounds (footer.reduced (8, 0));
    r.removeFromBottom (24);
    devicePanel.setBounds (r.removeFromTop (devicePanel.preferredHeight()));
    r.removeFromTop (12);
    auto meters = r.removeFromTop (78);
    inputMeterCard = meters.removeFromLeft ((meters.getWidth() - 12) / 2);
    meters.removeFromLeft (12); outputMeterCard = meters;
    auto layoutMeter = [] (juce::Rectangle<int> bounds, juce::Label& label,
                            juce::Label& reading, LevelMeterComponent& meter)
    {
        auto content = bounds.reduced (16, 12);
        auto title = content.removeFromTop (22);
        reading.setBounds (title.removeFromRight (95)); label.setBounds (title);
        content.removeFromTop (7); meter.setBounds (content.removeFromTop (17));
    };
    layoutMeter (inputMeterCard, inLabel, inReading, inMeter);
    layoutMeter (outputMeterCard, outLabel, outReading, outMeter);
    r.removeFromTop (20); pluginList.setBounds (r);
    bypassButton.setBounds (pluginList.getRight() - 386, pluginList.getY() + 4, 136, 34);
}

void MainComponent::visibilityChanged()
{
    if (isShowing()) { refreshStatus(); startTimerHz (24); }
    else stopTimer();
}

void MainComponent::timerCallback()
{
    if (! isShowing()) return;
    const auto in = engine.inputLevel(), out = engine.outputLevel();
    inMeter.setLevel (in); outMeter.setLevel (out);
    inReading.setText (levelText (in), juce::dontSendNotification);
    outReading.setText (levelText (out), juce::dontSendNotification);
    outReading.setColour (juce::Label::textColourId, out.peak >= 0.999f ? theme::danger : theme::text);
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
    muteButton.setButtonText (engine.isMuted() ? "Unmute" : "Mute mic");
    bypassButton.setToggleState (engine.isMasterBypassed(), juce::dontSendNotification);
    const bool routed = engine.isRunning() && engine.getRequestedSetup().inputDeviceName.isNotEmpty()
        && engine.getRequestedSetup().outputDeviceName.isNotEmpty();
    status.setText (engine.isMuted() ? "MIC MUTED" : engine.getDeviceError().isNotEmpty() ? "NEEDS ATTENTION"
        : ! routed ? "NOT ROUTED" : engine.isMasterBypassed() ? "DRY MIC" : "LIVE", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, engine.isMuted() ? theme::danger
        : routed && engine.getDeviceError().isEmpty() ? theme::accent : theme::warning);
    performance.setText (juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "% audio CPU",
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
