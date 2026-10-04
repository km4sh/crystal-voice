#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

struct LevelReading
{
    float rms = 0.0f, peak = 0.0f; // Unsmoothed block measurements.
    float vu = 0.0f, heldPeak = 0.0f; // Display envelope and independent sample-peak hold.
};

// Reine Funktion: RMS/Peak über alle Kanäle eines Buffers. Testbar ohne Hardware.
LevelReading computeLevel (const juce::AudioBuffer<float>& buffer);

// VU-style full-wave detector and second-order movement, independent of buffer/UI rate.
// prepare/reset/process run on the audio lifecycle thread; read only loads atomics.
class LevelMeter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const juce::AudioBuffer<float>& buffer);
    LevelReading read() const
    {
        return { rms_.load (std::memory_order_relaxed),
                 peak_.load (std::memory_order_relaxed),
                 vu_.load (std::memory_order_relaxed),
                 heldPeak_.load (std::memory_order_relaxed) };
    }
private:
    // Exact discrete-time solution of y'' + 2*zeta*omega*y' + omega^2*y = omega^2*abs(x).
    // About 300 ms to 99% on a step, with a small 1.2% mechanical overshoot.
    double position = 0.0, velocity = 0.0;
    double positionCoefficient = 0.0, velocityToPosition = 0.0,
        positionToVelocity = 0.0, velocityCoefficient = 0.0;
    double heldPeak = 0.0, peakRelease = 1.0;
    int holdSamples = 24000, holdRemaining = 0;
    bool prepared = false;
    std::atomic<float> rms_  { 0.0f };
    std::atomic<float> peak_ { 0.0f };
    std::atomic<float> vu_ { 0.0f }, heldPeak_ { 0.0f };
};
