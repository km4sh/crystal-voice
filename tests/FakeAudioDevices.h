#pragma once
#include "audio/MicVSTDeviceManager.h"
#include <utility>

// No OS endpoints or worker threads: tests open and render only in-memory devices.
namespace testAudio
{
struct OpenAttempt
{
    juce::String input, output;
    double sampleRate;
    int bufferSize;
};

struct Backend
{
    juce::StringArray inputs { "Default virtual mic", "Microphone A", "Microphone B" };
    juce::StringArray outputs { "Default speakers", "CABLE Input" };
    juce::String openError, failingInput;
    int inputChannelCount = 2, starts = 0;
    juce::Array<OpenAttempt> attempts;
};

class Device final : public juce::AudioIODevice
{
public:
    Device (juce::String type, juce::String input, juce::String output, std::shared_ptr<Backend> state)
        : AudioIODevice ("In-memory audio device", type), inputName (std::move (input)),
          outputName (std::move (output)), backend (std::move (state)) {}
    ~Device() override { close(); }
    juce::StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    juce::StringArray getInputChannelNames() override
    {
        juce::StringArray names;
        for (int i = 0; i < backend->inputChannelCount; ++i) names.add ("Channel " + juce::String (i + 1));
        return names;
    }
    juce::Array<double> getAvailableSampleRates() override { return { 44100.0, 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 64, 128, 480 }; }
    int getDefaultBufferSize() override { return 128; }
    juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double sr, int bs) override
    {
        backend->attempts.add ({ inputName, outputName, sr, bs });
        if (backend->openError.isNotEmpty() && (backend->failingInput.isEmpty() || inputName == backend->failingInput))
            return backend->openError;
        inputChannels = ins; outputChannels = outs;
        for (int i = backend->inputChannelCount; i <= inputChannels.getHighestBit(); ++i) inputChannels.clearBit (i);
        sampleRate = sr; blockSize = bs; opened = true;
        return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback* next) override
    {
        stop(); callback = next;
        if (callback != nullptr) { ++backend->starts; callback->audioDeviceAboutToStart (this); }
    }
    void stop() override
    {
        if (auto* previous = std::exchange (callback, nullptr)) previous->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outputChannels; }
    juce::BigInteger getActiveInputChannels() const override { return inputChannels; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    juce::AudioBuffer<float> render()
    {
        const int numIns = inputChannels.countNumberOfSetBits(), numOuts = outputChannels.countNumberOfSetBits();
        juce::AudioBuffer<float> inputs (numIns, blockSize), outputs (numOuts, blockSize);
        int active = 0;
        for (int physical = 0; physical <= inputChannels.getHighestBit(); ++physical)
            if (inputChannels[physical])
            {
                juce::FloatVectorOperations::fill (inputs.getWritePointer (active++), 0.2f * (physical + 1), blockSize);
            }
        outputs.clear();
        if (callback != nullptr) callback->audioDeviceIOCallbackWithContext (inputs.getArrayOfReadPointers(), numIns,
            outputs.getArrayOfWritePointers(), numOuts, blockSize, {});
        return outputs;
    }

    const juce::String inputName, outputName;
private:
    std::shared_ptr<Backend> backend;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::BigInteger inputChannels, outputChannels;
    double sampleRate = 48000.0;
    int blockSize = 128;
    bool opened = false;
};

class Type final : public juce::AudioIODeviceType
{
public:
    Type (juce::String name, std::shared_ptr<Backend> state) : AudioIODeviceType (name), backend (std::move (state)) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool input) const override { return input ? backend->inputs : backend->outputs; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* device, bool input) const override
    {
        auto* fake = dynamic_cast<Device*> (device);
        return fake != nullptr ? getDeviceNames (input).indexOf (input ? fake->inputName : fake->outputName) : -1;
    }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice (const juce::String& output, const juce::String& input) override
    { return new Device (getTypeName(), input, output, backend); }
private:
    std::shared_ptr<Backend> backend;
};

inline MicVSTDeviceManager::DeviceTypesFactory devices (std::shared_ptr<Backend> lowLatency = std::make_shared<Backend>(),
                                                       std::shared_ptr<Backend> shared = {})
{
    return [lowLatency, shared] (juce::OwnedArray<juce::AudioIODeviceType>& types)
    {
        types.add (new Type (MicVSTDeviceManager::lowLatencyTypeName, lowLatency));
        if (shared != nullptr) types.add (new Type (MicVSTDeviceManager::sharedTypeName, shared));
    };
}
}
