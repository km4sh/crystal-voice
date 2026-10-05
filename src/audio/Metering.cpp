#include "audio/Metering.h"

LevelReading computeLevel (const juce::AudioBuffer<float>& buffer)
{
    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return {};

    double sumSq = 0.0;
    float  peak  = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float s = std::isfinite (d[i]) ? d[i] : 0.0f;
            sumSq += (double) s * s;
            peak = juce::jmax (peak, std::abs (s));
        }
    }
    const float rms = (float) std::sqrt (sumSq / (double) (numCh * n));
    return { rms, peak };
}

void LevelMeter::PeakHold::prepare (double sampleRate, double holdSeconds, double releaseDbPerSecond)
{
    holdSamples = juce::jmax (1, (int) std::round (sampleRate * holdSeconds));
    release = std::pow (10.0, -releaseDbPerSecond / (20.0 * sampleRate));
    reset();
}

void LevelMeter::PeakHold::reset() { level = 0.0; remaining = 0; }

void LevelMeter::PeakHold::process (double value)
{
    if (value >= level) { level = value; remaining = holdSamples; }
    else if (remaining > 0) --remaining;
    else
    {
        level = juce::jmax (value, level * release);
        if (level < 0.000001) level = 0.0;
    }
}

void LevelMeter::prepare (double sampleRate)
{
    if (! std::isfinite (sampleRate) || sampleRate <= 0.0) sampleRate = 48000.0;
    constexpr double damping = 0.8152637502528736;
    constexpr double angularFrequency = 13.597198260488485;
    const double decay = damping * angularFrequency;
    const double frequency = angularFrequency * std::sqrt (1.0 - damping * damping);
    const double attenuation = std::exp (-decay / sampleRate);
    const double sine = std::sin (frequency / sampleRate), cosine = std::cos (frequency / sampleRate);
    positionCoefficient = attenuation * (cosine + decay / frequency * sine);
    velocityToPosition = attenuation * sine / frequency;
    positionToVelocity = -angularFrequency * angularFrequency * velocityToPosition;
    velocityCoefficient = attenuation * (cosine - decay / frequency * sine);
    samplePeakHold.prepare (sampleRate, 0.5, 20.0);
    vuPeakHold.prepare (sampleRate, 0.2, 60.0);
    prepared = true;
    reset();
}

void LevelMeter::reset()
{
    position = velocity = 0.0; samplePeakHold.reset(); vuPeakHold.reset();
    rms_.store (0.0f, std::memory_order_relaxed); peak_.store (0.0f, std::memory_order_relaxed);
    vu_.store (0.0f, std::memory_order_relaxed); heldPeak_.store (0.0f, std::memory_order_relaxed);
    heldVu_.store (0.0f, std::memory_order_relaxed);
}

void LevelMeter::process (const juce::AudioBuffer<float>& buffer)
{
    if (! prepared) prepare (48000.0);
    const juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels(), samples = buffer.getNumSamples();
    if (samples <= 0) return; // No elapsed audio time: do not advance the envelope.
    const auto* const* data = buffer.getArrayOfReadPointers();
    const double channelScale = channels > 0 ? 1.0 / channels : 0.0;
    double sumSquares = 0.0;
    float blockPeak = 0.0f;
    // Match the existing dBFS scale for a steady sine: mean(abs(sine)) * pi/(2*sqrt(2)).
    constexpr double sineCalibration = 1.1107207345395915;
    for (int sample = 0; sample < samples; ++sample)
    {
        double rectified = 0.0;
        float samplePeak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
        {
            const float value = data[channel] != nullptr && std::isfinite (data[channel][sample]) ? data[channel][sample] : 0.0f;
            const float magnitude = std::abs (value);
            rectified += magnitude;
            sumSquares += (double) value * value;
            samplePeak = juce::jmax (samplePeak, magnitude);
        }
        // Rectify before combining channels, so opposite polarities cannot cancel.
        rectified *= channelScale;
        const double displacement = position - rectified;
        const double nextPosition = rectified + positionCoefficient * displacement + velocityToPosition * velocity;
        velocity = positionToVelocity * displacement + velocityCoefficient * velocity;
        position = nextPosition;
        blockPeak = juce::jmax (blockPeak, samplePeak);
        samplePeakHold.process (samplePeak);
        // The marker and bar must measure the same quantity. Preserve raw peaks separately
        // for clipping, rather than placing waveform crest factors on a VU-average display.
        vuPeakHold.process (juce::jmax (0.0, position * sineCalibration));
    }
    // This is VU ballistics on a digital scale, not a new analog 0-VU reference level.
    rms_.store ((float) std::sqrt (sumSquares * channelScale / samples), std::memory_order_relaxed);
    peak_.store (blockPeak, std::memory_order_relaxed);
    vu_.store ((float) juce::jmax (0.0, position * sineCalibration), std::memory_order_relaxed);
    heldPeak_.store ((float) samplePeakHold.level, std::memory_order_relaxed);
    heldVu_.store ((float) vuPeakHold.level, std::memory_order_relaxed);
}
