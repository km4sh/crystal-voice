#include "ui/DevicePanel.h"
#include "ui/Theme.h"

DevicePanel::DevicePanel (AudioEngine& e) : engine (e)
{
    for (auto* component : std::initializer_list<juce::Component*> {
        &inputBox, &outputBox, &channelBox, &rateBox, &bufferBox, &inputHint,
        &outputHint, &rateLabel, &bufferLabel, &statusLabel, &retryButton }) addAndMakeVisible (component);
    retryButton.onClick = [this]
    { applyRoute (engine.getRequestedSetup().inputDeviceName, engine.getRequestedSetup().outputDeviceName,
                   engine.getRequestedSetup().sampleRate, engine.getRequestedSetup().bufferSize); };
    for (auto* label : { &inputHint, &outputHint, &rateLabel, &bufferLabel, &statusLabel })
    { label->setFont (theme::font (12)); label->setColour (juce::Label::textColourId, theme::muted); }
    inputHint.setText ("Choose a physical microphone or audio interface", juce::dontSendNotification);
    outputHint.setText ("In other apps, select CABLE Output as your mic", juce::dontSendNotification);
    inputBox.setTooltip ("Audio source. Channel selection below supports either input on an audio interface.");
    outputBox.setTooltip ("Send audio to CABLE Input; other applications capture CABLE Output.");
    rateBox.setTooltip ("The device may require its Windows mix rate. RNNoise requires 48 kHz.");
    bufferBox.setTooltip ("Auto uses the device default. Smaller buffers lower latency and increase CPU pressure.");
    inputBox.onChange = [this]
    { if (! updating) applyRoute (inputBox.getSelectedId() == 1 ? juce::String() : inputBox.getText(), engine.getRequestedSetup().outputDeviceName); };
    outputBox.onChange = [this]
    { if (! updating) applyRoute (engine.getRequestedSetup().inputDeviceName, outputBox.getSelectedId() == 1 ? juce::String() : outputBox.getText()); };
    channelBox.onChange = [this]
    {
        if (updating) return;
        engine.setInputChannel (channelBox.getSelectedId() == 3 ? -1 : channelBox.getSelectedId() - 1);
        refresh();
    };
    rateBox.onChange = [this]
    {
        if (updating || ! juce::isPositiveAndBelow (rateBox.getSelectedId() - 1, rates.size())) return;
        applyRoute (engine.getRequestedSetup().inputDeviceName, engine.getRequestedSetup().outputDeviceName,
                    rates[rateBox.getSelectedId() - 1]);
    };
    bufferBox.onChange = [this]
    {
        if (updating) return;
        const int index = bufferBox.getSelectedId() - 2;
        const int preference = juce::isPositiveAndBelow (index, buffers.size()) ? buffers[index] : 0;
        auto* device = engine.getDeviceManager().getCurrentAudioDevice();
        const int effective = preference > 0 ? preference : device != nullptr ? device->getDefaultBufferSize() : 0;
        const auto error = engine.setDeviceConfig (engine.getRequestedSetup().inputDeviceName,
                                                  engine.getRequestedSetup().outputDeviceName, 0, effective);
        if (error.isEmpty()) { engine.setPreferredBufferSize (preference); engine.requestPersist(); }
        refresh();
    };
    engine.getDeviceManager().addChangeListener (this); refresh(); startTimer (500);
}

DevicePanel::~DevicePanel() { stopTimer(); engine.getDeviceManager().removeChangeListener (this); }
void DevicePanel::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }
void DevicePanel::timerCallback()
{
    if (refreshPending) refresh();
    if (isShowing()) updateStatus();
}

void DevicePanel::applyRoute (const juce::String& input, const juce::String& output, double rate, int buffer)
{
    engine.setDeviceConfig (input, output, rate, buffer);
    refresh();
}

void DevicePanel::refresh()
{
    if (inputBox.isPopupActive() || outputBox.isPopupActive() || channelBox.isPopupActive()
        || rateBox.isPopupActive() || bufferBox.isPopupActive()) { refreshPending = true; return; }
    refreshPending = false;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& requested = engine.getRequestedSetup();
    auto* type = engine.getDeviceManager().getCurrentDeviceTypeObject();
    auto fillDevices = [] (juce::ComboBox& box, const juce::StringArray& names, const juce::String& selected)
    {
        box.clear (juce::dontSendNotification); box.addItem ("Not connected", 1);
        for (int i = 0; i < names.size(); ++i) box.addItem (names[i], i + 2);
        const int index = names.indexOf (selected);
        if (selected.isEmpty()) box.setSelectedId (1, juce::dontSendNotification);
        else if (index >= 0) box.setSelectedId (index + 2, juce::dontSendNotification);
        else box.setText (selected + " (disconnected)", juce::dontSendNotification);
    };
    fillDevices (inputBox, type != nullptr ? type->getDeviceNames (true) : juce::StringArray(), requested.inputDeviceName);
    fillDevices (outputBox, type != nullptr ? type->getDeviceNames (false) : juce::StringArray(), requested.outputDeviceName);
    channelBox.clear (juce::dontSendNotification);
    channelBox.addItem ("Channel 1 - mono", 1); channelBox.addItem ("Channel 2 - mono", 2);
    channelBox.addItem ("Channels 1 + 2 - stereo", 3);
    channelBox.setSelectedId (engine.getInputChannel() < 0 ? 3 : engine.getInputChannel() + 1, juce::dontSendNotification);
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    rates = device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>();
    if (rates.isEmpty()) rates.add (requested.sampleRate > 0 ? requested.sampleRate : 48000.0);
    const double rate = device != nullptr ? device->getCurrentSampleRate() : requested.sampleRate;
    if (! rates.contains (rate) && rate > 0) rates.add (rate);
    rateBox.clear (juce::dontSendNotification);
    for (int i = 0; i < rates.size(); ++i) rateBox.addItem (juce::String (rates[i] / 1000.0, 1) + " kHz", i + 1);
    rateBox.setSelectedId (rates.indexOf (rate) + 1, juce::dontSendNotification);
    buffers = device != nullptr ? device->getAvailableBufferSizes() : juce::Array<int>();
    bufferBox.clear (juce::dontSendNotification); bufferBox.addItem ("Auto", 1);
    for (int i = 0; i < buffers.size(); ++i) bufferBox.addItem (juce::String (buffers[i]) + " samples", i + 2);
    const int index = buffers.indexOf (engine.getPreferredBufferSize());
    bufferBox.setSelectedId (index >= 0 ? index + 2 : 1, juce::dontSendNotification);
    bufferBox.setEnabled (buffers.size() > 1);
    if (device != nullptr)
    { channelBox.setItemEnabled (2, device->getInputChannelNames().size() > 1);
      channelBox.setItemEnabled (3, device->getInputChannelNames().size() > 1); }
    updateStatus();
}

void DevicePanel::updateStatus()
{
    auto* device = engine.getDeviceManager().getCurrentAudioDevice();
    auto error = engine.getDeviceError();
    juce::String text;
    if (error.isNotEmpty()) text = "Could not change audio device: " + error;
    else if (device == nullptr) text = "Connect a microphone and select a destination to start.";
    else
    {
        const double rate = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const double latency = rate > 0 ? 1000.0 * (device->getInputLatencyInSamples()
            + device->getOutputLatencyInSamples() + buffer + engine.getGraph().getLatencySamples()) / rate : 0.0;
        text = juce::String (rate / 1000.0, 1) + " kHz  /  " + juce::String (buffer) + " samples  /  "
            + juce::String (latency, 1) + " ms estimated host latency";
        for (const auto& effect : engine.getChain().entries())
            if (! effect.bypassed && effect.displayName.containsIgnoreCase ("rnnoise") && rate != 48000.0)
            { text = "RNNoise requires 48 kHz. Change the device sample rate."; error = text; break; }
    }
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setTooltip (text);
    statusLabel.setColour (juce::Label::textColourId, error.isNotEmpty() ? theme::warning : theme::muted);
    const bool showRetry = error.isNotEmpty();
    if (retryButton.isVisible() != showRetry) { retryButton.setVisible (showRetry); resized(); }
}

void DevicePanel::paint (juce::Graphics& g)
{
    theme::card (g, inputCard.toFloat()); theme::card (g, outputCard.toFloat());
    g.setColour (theme::accent); g.setFont (theme::font (10, true));
    g.drawText ("01 / SOURCE", inputCard.getX() + 16, 12, 150, 15, juce::Justification::centredLeft);
    g.drawText ("02 / DESTINATION", outputCard.getX() + 16, 12, 200, 15, juce::Justification::centredLeft);
    g.setColour (theme::text); g.setFont (theme::font (17, true));
    g.drawText ("Microphone", inputCard.getX() + 16, 31, 220, 24, juce::Justification::centredLeft);
    g.drawText ("Virtual microphone", outputCard.getX() + 16, 31, 260, 24, juce::Justification::centredLeft);
}

void DevicePanel::resized()
{
    auto r = getLocalBounds(); auto cards = r.removeFromTop (178);
    inputCard = cards.removeFromLeft ((cards.getWidth() - 12) / 2);
    cards.removeFromLeft (12); outputCard = cards;
    auto input = inputCard.reduced (16, 0); input.removeFromTop (64);
    inputBox.setBounds (input.removeFromTop (36)); input.removeFromTop (8);
    channelBox.setBounds (input.removeFromTop (30)); input.removeFromTop (5);
    inputHint.setBounds (input.removeFromTop (20));
    auto output = outputCard.reduced (16, 0); output.removeFromTop (64);
    outputBox.setBounds (output.removeFromTop (36)); output.removeFromTop (7);
    auto rateRow = output.removeFromTop (16), bufferRow = output.removeFromTop (30);
    const int width = (rateRow.getWidth() - 10) / 2;
    rateLabel.setBounds (rateRow.removeFromLeft (width)); rateRow.removeFromLeft (10); bufferLabel.setBounds (rateRow);
    rateBox.setBounds (bufferRow.removeFromLeft (width)); bufferRow.removeFromLeft (10); bufferBox.setBounds (bufferRow);
    outputHint.setBounds (output.removeFromTop (20));
    r.removeFromTop (6); auto statusRow = r.removeFromTop (30);
    if (retryButton.isVisible()) { retryButton.setBounds (statusRow.removeFromRight (64)); statusRow.removeFromRight (8); }
    statusLabel.setBounds (statusRow);
}
