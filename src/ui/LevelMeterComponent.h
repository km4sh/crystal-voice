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
// The thin line holds peaks of the same VU envelope for 200 ms, then releases at 60 dB/s.
class LevelMeterComponent : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeterComponent()
    {
        setTooltip ("Bar and number: VU average in dBFS. Thin line: recent maximum of the same VU level (200 ms hold, 60 dB/s release). Hover over the number for the separate sample peak. Red warns of sample peaks at 0 dBFS.");
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
