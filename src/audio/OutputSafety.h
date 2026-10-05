#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Prepared off the audio thread. No allocation, locks, logging or disk I/O in process().
class OutputSafety
{
public:
    void prepare (double rate, int channels, bool muted, bool bypassed)
    {
        muteGain.reset (rate, 0.005); bypassMix.reset (rate, 0.005); snapshotGain.reset (rate, 0.005);
        muteGain.setCurrentAndTargetValue (muted ? 0.0f : 1.0f);
        bypassMix.setCurrentAndTargetValue (bypassed ? 1.0f : 0.0f);
        snapshotGain.setCurrentAndTargetValue (1.0f);
        dryHistory.setSize (juce::jmax (1, channels), juce::jmax (2, (int) std::ceil (rate) + 1));
        dryHistory.clear(); lastWet.assign ((size_t) juce::jmax (1, channels), 0.0f); position = 0;
    }

    struct Counts { uint64_t invalid = 0, clipped = 0; };
    Counts process (float* const* output, int outs, const float* const* input, int ins,
                    int samples, bool muted, bool bypassed, bool snapshot, int latency)
    {
        Counts counts;
        muteGain.setTargetValue (muted ? 0.0f : 1.0f);
        bypassMix.setTargetValue (bypassed ? 1.0f : 0.0f);
        snapshotGain.setTargetValue (snapshot ? 0.0f : 1.0f);
        const int capacity = dryHistory.getNumSamples();
        latency = juce::jlimit (0, capacity - 1, latency);
        for (int sample = 0; sample < samples; ++sample)
        {
            const float mix = bypassMix.getNextValue(), gain = muteGain.getNextValue() * snapshotGain.getNextValue();
            for (int channel = 0; channel < outs; ++channel)
            {
                if (output[channel] == nullptr) continue;
                float dry = ins > 0 && input[channel % ins] != nullptr ? input[channel % ins][sample] : 0.0f;
                if (! std::isfinite (dry)) { dry = 0.0f; ++counts.invalid; }
                if (channel < dryHistory.getNumChannels())
                {
                    auto* history = dryHistory.getWritePointer (channel);
                    history[position] = dry;
                    dry = history[(position + capacity - latency) % capacity];
                }
                float wet = snapshot && channel < (int) lastWet.size() ? lastWet[(size_t) channel] : output[channel][sample];
                if (! std::isfinite (wet)) { wet = 0.0f; ++counts.invalid; }
                if (! snapshot && channel < (int) lastWet.size()) lastWet[(size_t) channel] = juce::jlimit (-1.0f, 1.0f, wet);
                const float value = (wet + mix * (dry - wet)) * gain;
                if (! std::isfinite (value)) { output[channel][sample] = 0.0f; ++counts.invalid; }
                else
                {
                    if (std::abs (value) > 1.0f) ++counts.clipped;
                    output[channel][sample] = juce::jlimit (-1.0f, 1.0f, value);
                }
            }
            position = (position + 1) % capacity;
        }
        return counts;
    }
private:
    juce::SmoothedValue<float> muteGain, bypassMix, snapshotGain;
    juce::AudioBuffer<float> dryHistory;
    std::vector<float> lastWet;
    int position = 0;
};
