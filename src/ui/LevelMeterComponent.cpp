#include "ui/LevelMeterComponent.h"
#include "ui/Theme.h"

namespace
{
    // Gitter-/Beschriftungsmarken (dB). 0 dB rechts.
    const int kTicks[] = { -60, -48, -36, -24, -12, -6, 0 };
}

void LevelMeterComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::background);
    g.fillRect (b);

    auto dbX = [&] (float db) { return b.getX() + meterScale::dbToNorm (db) * b.getWidth(); };

    const float vuDb = juce::Decibels::gainToDecibels (level.vu, meterScale::minDb);
    const float vuX  = dbX (vuDb);
    constexpr int segments = 40;
    const float pitch = b.getWidth() / segments;
    for (int i = 0; i < segments; ++i)
    {
        const float x = b.getX() + i * pitch;
        const auto colour = i >= 38 ? theme::danger : i >= 34 ? theme::warning : theme::accent;
        g.setColour (x < vuX ? colour : theme::raised);
        g.fillRect (juce::Rectangle<float> (x + 1, b.getY() + 2, juce::jmax (1.0f, pitch - 2), b.getHeight() - 4));
    }

    // Keep a full-scale peak inside the frame instead of clipping it at x == width.
    const float peakDb = juce::Decibels::gainToDecibels (level.heldPeak, meterScale::minDb);
    if (peakDb > meterScale::minDb)
    {
        g.setColour (level.heldPeak >= 1.0f ? theme::danger : theme::warning);
        const int px = juce::roundToInt (juce::jlimit (b.getX() + 1, juce::jmax (b.getX() + 1, b.getRight() - 3), dbX (peakDb) - 1));
        g.fillRect (px, 1, 2, juce::jmax (0, getHeight() - 2));
    }

    g.setColour (theme::border);
    g.drawRect (b.reduced (0.5f), 1.0f);
}

void DbScaleComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme::muted);
    g.setFont (theme::font (10));

    for (int db : kTicks)
    {
        const float x = b.getX() + meterScale::dbToNorm ((float) db) * b.getWidth();
        // Mittiger Text um die Markierung; Ränder etwas einrücken, damit nichts abgeschnitten wird.
        auto just = db == kTicks[0] ? juce::Justification::centredLeft
                  : db == 0         ? juce::Justification::centredRight
                                    : juce::Justification::centred;
        juce::Rectangle<float> r (juce::jlimit (b.getX(), juce::jmax (b.getX(), b.getRight() - 32), x - 16),
                                  b.getY(), 32, b.getHeight());
        g.drawText (juce::String (db), r, just);
    }
}
