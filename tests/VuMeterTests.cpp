#include "audio/Metering.h"

namespace
{
    constexpr float calibration = 1.1107207345395915f;
    void feedConstant (LevelMeter& meter, double rate, double seconds, float value, int channels = 1)
    {
        juce::AudioBuffer<float> buffer (channels, (int) std::round (rate * seconds));
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (channel), value, buffer.getNumSamples());
        meter.process (buffer);
    }
    void feedTone (LevelMeter& meter, double rate, int blockSize)
    {
        const int total = (int) rate;
        juce::AudioBuffer<float> buffer (1, blockSize);
        for (int offset = 0; offset < total; offset += blockSize)
        {
            const int count = juce::jmin (blockSize, total - offset);
            for (int sample = 0; sample < count; ++sample)
                buffer.setSample (0, sample, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi
                    * 1000.0 * (offset + sample) / rate));
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 1, count);
            meter.process (view);
        }
    }
}

struct VuMeterTests : juce::UnitTest
{
    VuMeterTests() : UnitTest ("VU detector and ballistics") {}
    void runTest() override
    {
        beginTest ("steady 1 kHz sine retains RMS-equivalent dBFS calibration at supported sample rates");
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            LevelMeter meter; meter.prepare (rate); feedTone (meter, rate, 480);
            expectWithinAbsoluteError (meter.read().vu, 0.25f / std::sqrt (2.0f), 0.0005f);
            expectWithinAbsoluteError (meter.read().heldPeak, 0.25f, 0.0001f);
        }

        beginTest ("300 ms rise reaches 99 percent with the same ballistics at every sample rate");
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            LevelMeter meter; meter.prepare (rate);
            feedConstant (meter, rate, 0.1, 0.5f);
            expect (meter.read().vu < 0.5f * calibration * 0.5f);
            feedConstant (meter, rate, 0.2, 0.5f);
            expectWithinAbsoluteError (meter.read().vu / (0.5f * calibration), 0.99f, 0.0005f);
        }

        beginTest ("small mechanical overshoot settles and silence falls using the same movement");
        {
            LevelMeter meter; meter.prepare (48000);
            feedConstant (meter, 48000, 0.4, 0.5f);
            expectWithinAbsoluteError (meter.read().vu / (0.5f * calibration), 1.012f, 0.0002f);
            feedConstant (meter, 48000, 1.0, 0.5f);
            expectWithinAbsoluteError (meter.read().vu, 0.5f * calibration, 0.0001f);
            feedConstant (meter, 48000, 0.3, 0.0f);
            expectWithinAbsoluteError (meter.read().vu / (0.5f * calibration), 0.01f, 0.0005f);
        }

        beginTest ("a short loud burst is not displayed as full-scale average but its peak is retained");
        {
            LevelMeter meter; meter.prepare (48000);
            feedConstant (meter, 48000, 0.01, 1.0f);
            expect (meter.read().vu < 0.02f); expectEquals (meter.read().peak, 1.0f);
            feedConstant (meter, 48000, 0.1, 0.0f);
            expect (meter.read().vu < 0.1f); expectEquals (meter.read().peak, 0.0f);
            expectEquals (meter.read().heldPeak, 1.0f);
            feedConstant (meter, 48000, 0.5, 0.0f);
            expectEquals (meter.read().heldPeak, 0.0f);
        }

        beginTest ("VU processing is independent of callback block size and GUI polling");
        {
            LevelMeter smallBlocks, largeBlocks;
            smallBlocks.prepare (48000); largeBlocks.prepare (48000);
            feedTone (smallBlocks, 48000, 64); feedTone (largeBlocks, 48000, 1024);
            const float before = smallBlocks.read().vu;
            expectWithinAbsoluteError (before, largeBlocks.read().vu, 0.000001f);
            bool unchanged = true;
            for (int read = 0; read < 100; ++read) unchanged = unchanged && smallBlocks.read().vu == before;
            expect (unchanged);
        }

        beginTest ("rectifying before channel averaging avoids opposite-polarity cancellation");
        {
            LevelMeter mono, stereo; mono.prepare (48000); stereo.prepare (48000);
            feedConstant (mono, 48000, 1.0, 0.5f);
            juce::AudioBuffer<float> buffer (2, 48000);
            juce::FloatVectorOperations::fill (buffer.getWritePointer (0), 0.5f, 48000);
            juce::FloatVectorOperations::fill (buffer.getWritePointer (1), -0.5f, 48000);
            stereo.process (buffer);
            expectWithinAbsoluteError (mono.read().vu, stereo.read().vu, 0.000001f);
            expectWithinAbsoluteError (stereo.read().rms, 0.5f, 0.000001f);
            expectEquals (stereo.read().peak, 0.5f);
        }

        beginTest ("restart clears all display state and empty blocks do not invent elapsed time");
        {
            LevelMeter meter; meter.prepare (48000); feedConstant (meter, 48000, 1.0, 0.5f);
            const auto before = meter.read(); juce::AudioBuffer<float> empty;
            meter.process (empty); expectEquals (meter.read().vu, before.vu);
            meter.prepare (96000); auto after = meter.read();
            expectEquals (after.rms, 0.0f); expectEquals (after.peak, 0.0f);
            expectEquals (after.vu, 0.0f); expectEquals (after.heldPeak, 0.0f);
            feedConstant (meter, 96000, 0.3, 0.5f);
            expectWithinAbsoluteError (meter.read().vu / (0.5f * calibration), 0.99f, 0.0005f);
            meter.reset(); expectEquals (meter.read().vu, 0.0f); expectEquals (meter.read().heldPeak, 0.0f);
        }
    }
};
static VuMeterTests vuMeterTests;
