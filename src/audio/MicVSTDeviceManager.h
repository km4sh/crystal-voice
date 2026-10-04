#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

// AudioDeviceManager mit WASAPI "Low Latency Mode" (IAudioClient3, ab Win 10) als
// bevorzugtem Typ und klassischem Shared als Fallback. Low Latency meldet pro Gerät
// echte Buffer-Größen (min..max in Treiber-Schritten) -> Buffer-Dropdown im DevicePanel;
// Shared meldet genau EINE Größe (Windows-Mixer-Periode), das Dropdown bleibt dann weg.
// Exclusive bleibt bewusst außen vor (lockt Geräte gegen andere Apps).
struct MicVSTDeviceManager : juce::AudioDeviceManager
{
    using DeviceTypesFactory = std::function<void (juce::OwnedArray<juce::AudioIODeviceType>&)>;

    explicit MicVSTDeviceManager (DeviceTypesFactory factory = {}) : deviceTypesFactory (std::move (factory)) {}

    static constexpr const char* lowLatencyTypeName = "Windows Audio (Low Latency Mode)";
    static constexpr const char* sharedTypeName     = "Windows Audio";

    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        if (deviceTypesFactory) { deviceTypesFactory (types); return; }
        if (auto* ll = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                           juce::WASAPIDeviceMode::sharedLowLatency))
            types.add (ll);
        if (auto* shared = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                               juce::WASAPIDeviceMode::shared))
            types.add (shared);
    }

    juce::String preferredTypeName()
    {
        for (auto* t : getAvailableDeviceTypes())
            if (t->getTypeName() == lowLatencyTypeName)
                return lowLatencyTypeName;
        return sharedTypeName;
    }

    juce::String openDeviceSetup (const juce::String& typeName, const AudioDeviceSetup& setup)
    {
        // JUCE treats an unchanged setup as a no-op even when its device has stopped.
        if (auto* device = getCurrentAudioDevice(); device != nullptr && ! device->isPlaying()) closeAudioDevice();
        if (getCurrentAudioDeviceType() == typeName)
            return setAudioDeviceSetup (setup, true);

        // setCurrentAudioDeviceType() opens remembered/default devices before the
        // requested route. Initialise from explicit XML instead, with no default fallback.
        closeAudioDevice();
        juce::XmlElement xml ("DEVICESETUP");
        xml.setAttribute ("deviceType", typeName);
        xml.setAttribute ("audioInputDeviceName", setup.inputDeviceName);
        xml.setAttribute ("audioOutputDeviceName", setup.outputDeviceName);
        xml.setAttribute ("audioDeviceRate", setup.sampleRate);
        xml.setAttribute ("audioDeviceBufferSize", setup.bufferSize);
        xml.setAttribute ("audioDeviceInChans", setup.inputChannels.toString (2));
        xml.setAttribute ("audioDeviceOutChans", setup.outputChannels.toString (2));
        return juce::AudioDeviceManager::initialise (setup.inputChannels.countNumberOfSetBits(),
            setup.outputChannels.countNumberOfSetBits(), &xml, false, {}, &setup);
    }

private:
    DeviceTypesFactory deviceTypesFactory;
};
