#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/Metering.h"

namespace meterScale
{
    constexpr float minDb = -60.0f;   // linker Rand der Skala
    constexpr float maxDb =   0.0f;   // rechter Rand

    // dB -> normierte X-Position [0..1] über die Meterbreite.
    inline float dbToNorm (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }
}

// The unchanged segmented display follows the audio-thread VU envelope.
// The thin line independently holds sample peaks for 500 ms, then releases at 20 dB/s.
class LevelMeterComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeterComponent()
    {
        setTooltip ("Bar and number: VU average in dBFS. Thin line: sample peak, held for 500 ms, then falling at 20 dB/s. Red means the sample peak reaches 0 dBFS.");
    }
    void setLevel (LevelReading r) { level = r; repaint(); }
    void paint (juce::Graphics& g) override;
private:
    LevelReading level;
};

// dB-Beschriftung unter den Metern, exakt an meterScale ausgerichtet.
class DbScaleComponent : public juce::Component
{
public:
    void paint (juce::Graphics& g) override;
};
