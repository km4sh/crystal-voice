#include "audio/AudioEngine.h"
#include "FakeAudioDevices.h"
#include <thread>

namespace
{
void dispatchPendingMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
}

struct AudioDeviceRegression : juce::UnitTest
{
    AudioDeviceRegression() : UnitTest ("In-memory audio device routing") {}
    void runTest() override
    {
        auto lowLatency = std::make_shared<testAudio::Backend>();
        auto shared = std::make_shared<testAudio::Backend>();
        AudioEngine engine (testAudio::devices (lowLatency, shared));

        beginTest ("startup opens only the chosen route, not the system defaults");
        expect (engine.initialise ("Microphone A", "CABLE Input", 44100.0).isEmpty());
        expect (engine.isRunning()); expectEquals (lowLatency->attempts.size(), 1);
        expect (shared->attempts.isEmpty());
        expectEquals (lowLatency->attempts[0].input, juce::String ("Microphone A"));
        expectEquals (lowLatency->attempts[0].output, juce::String ("CABLE Input"));
        expectEquals (engine.captureState().sampleRate, 44100.0);
        dispatchPendingMessages();

        beginTest ("an error queued by the previous stream cannot overwrite a successful device switch");
        std::thread driver ([&] { engine.audioDeviceError ("Old stream failed"); }); driver.join();
        expect (engine.setDeviceConfig ("Microphone B", "CABLE Input", 48000, 64).isEmpty());
        dispatchPendingMessages();
        expect (engine.getDeviceError().isEmpty()); expect (engine.isRunning());
        expectEquals (engine.captureState().inputDevice, juce::String ("Microphone B"));

        beginTest ("errors from the new stream still reach the interface");
        engine.audioDeviceError ("Current stream failed"); dispatchPendingMessages();
        expectEquals (engine.getDeviceError(), juce::String ("Current stream failed"));

        beginTest ("Retry opens a fresh stream and clears queued errors from the closed device");
        const int startsBeforeRetry = lowLatency->starts;
        engine.audioDeviceError ("Late error before Retry");
        expect (engine.retryAudioDevice().isEmpty()); dispatchPendingMessages();
        expect (lowLatency->starts > startsBeforeRetry); expect (engine.getDeviceError().isEmpty());

        beginTest ("automatic recovery opens a fresh stream even when the saved setup has not changed");
        const int startsBeforeRecovery = lowLatency->starts;
        engine.getDeviceManager().getCurrentAudioDevice()->stop();
        engine.getDeviceManager().sendChangeMessage(); dispatchPendingMessages();
        expect (engine.isRunning()); expect (lowLatency->starts > startsBeforeRecovery);
        expect (engine.getDeviceError().isEmpty());

        beginTest ("a driver-restored route clears its previous waiting-for-reconnect status");
        engine.getDeviceManager().getCurrentAudioDevice()->stop();
        lowLatency->inputs.removeString ("Microphone B");
        engine.getDeviceManager().sendChangeMessage(); dispatchPendingMessages();
        expect (engine.getDeviceError().contains ("Waiting")); expect (! engine.isRunning());
        lowLatency->inputs.add ("Microphone B");
        expect (engine.getDeviceManager().openDeviceSetup (MicVSTDeviceManager::lowLatencyTypeName, engine.getRequestedSetup()).isEmpty());
        engine.getDeviceManager().sendChangeMessage(); dispatchPendingMessages();
        expect (engine.isRunning()); expect (engine.getDeviceError().isEmpty());

        beginTest ("a rejected physical channel restores the previous channel and active route");
        lowLatency->inputChannelCount = 1;
        expect (engine.setInputChannel (1).isNotEmpty());
        expectEquals (engine.getInputChannel(), 0); expect (engine.isRunning());
        expectEquals (engine.getRequestedSetup().inputChannels.countNumberOfSetBits(), 1);
        expect (engine.getRequestedSetup().inputChannels[0]);
        expect (shared->attempts.isEmpty());
        lowLatency->inputChannelCount = 2;

        beginTest ("low-latency open failure retries the same route in shared mode");
        lowLatency->openError = "Low latency unsupported";
        expect (engine.retryAudioDevice().isEmpty()); dispatchPendingMessages();
        expect (engine.isRunning()); expect (engine.getDeviceError().isEmpty());
        expectEquals (engine.getDeviceManager().getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::sharedTypeName));
        expectEquals (shared->attempts.size(), 1);
        expectEquals (shared->attempts[0].input, juce::String ("Microphone B"));
        expectEquals (shared->attempts[0].output, juce::String ("CABLE Input"));
        expectEquals (shared->attempts[0].sampleRate, 48000.0);
        expectEquals (shared->attempts[0].bufferSize, 64);

        beginTest ("mono dry bypass reaches both outputs and master mute produces silence");
        auto* device = dynamic_cast<testAudio::Device*> (engine.getDeviceManager().getCurrentAudioDevice());
        expect (device != nullptr);
        if (device != nullptr)
        {
            engine.setMasterBypass (false); dispatchPendingMessages();
            auto processed = device->render();
            for (int channel = 0; channel < 2; ++channel)
                expectWithinAbsoluteError (processed.getSample (channel, 20), 0.2f, 0.00001f);
            engine.setMasterBypass (true);
            auto buffer = device->render();
            expectEquals (buffer.getNumChannels(), 2);
            for (int channel = 0; channel < 2; ++channel)
                expectWithinAbsoluteError (buffer.getSample (channel, 20), 0.2f, 0.00001f);
            engine.setMuted (true); buffer = device->render();
            expect (buffer.getSample (0, 0) > 0.0f && buffer.getSample (0, 0) < 0.2f);
            for (int i = 0; i < 4; ++i) buffer = device->render();
            expectWithinAbsoluteError (buffer.getMagnitude (0, buffer.getNumSamples()), 0.0f, 0.00001f);
            engine.setMuted (false);
        }

        beginTest ("selecting no output stays input-only instead of opening default speakers");
        expect (engine.setDeviceConfig ("Microphone B", {}, 48000.0, 64).isEmpty());
        expect (engine.isRunning());
        expect (engine.captureState().outputDevice.isEmpty());
        expect (shared->attempts.getLast().output.isEmpty());
        expect (engine.getDeviceManager().getCurrentAudioDevice()->getActiveOutputChannels().isZero());

        beginTest ("failure of both WASAPI modes restores the original mode and saved route");
        lowLatency->openError.clear();
        AudioEngine rollback (testAudio::devices (lowLatency, shared));
        expect (rollback.initialise ("Microphone A", "CABLE Input").isEmpty()); dispatchPendingMessages();
        lowLatency->openError = "Low latency open failed"; lowLatency->failingInput = "Microphone B";
        shared->openError = "Shared open failed"; shared->failingInput = "Microphone B";
        auto error = rollback.setDeviceConfig ("Microphone B", "CABLE Input", 0, 0);
        expect (error.contains ("Low latency open failed") && error.contains ("Shared open failed"));
        expect (rollback.isRunning());
        expectEquals (rollback.getDeviceManager().getCurrentAudioDeviceType(), juce::String (MicVSTDeviceManager::lowLatencyTypeName));
        expectEquals (rollback.captureState().inputDevice, juce::String ("Microphone A"));
        expectEquals (rollback.getDeviceManager().getAudioDeviceSetup().inputDeviceName, juce::String ("Microphone A"));

        beginTest ("rollback failure is reported and leaves the saved route available for a later retry");
        lowLatency->failingInput.clear(); shared->failingInput.clear();
        error = rollback.setDeviceConfig ("Microphone B", "CABLE Input", 0, 0);
        expect (error.contains ("Previous route could not be restored"));
        expect (! rollback.isRunning());
        expectEquals (rollback.captureState().inputDevice, juce::String ("Microphone A"));
        expectEquals (rollback.getDeviceError(), error);

        beginTest ("a missing shared-mode endpoint never redirects audio to a default device");
        auto unavailableShared = std::make_shared<testAudio::Backend>();
        unavailableShared->inputs.removeString ("Microphone B");
        AudioEngine missing (testAudio::devices (lowLatency, unavailableShared));
        expect (missing.initialise ("Microphone B", "CABLE Input").isNotEmpty());
        expect (! missing.isRunning()); expect (unavailableShared->attempts.isEmpty());
        expectEquals (missing.captureState().inputDevice, juce::String ("Microphone B"));
        expectEquals (missing.captureState().outputDevice, juce::String ("CABLE Input"));

        beginTest ("a first-launch low-latency failure opens the chosen route directly in shared mode");
        shared->openError.clear();
        AudioEngine firstLaunch (testAudio::devices (lowLatency, shared));
        const int beforeFirstLaunch = shared->attempts.size();
        expect (firstLaunch.initialise ("Microphone A", "CABLE Input").isEmpty());
        expect (firstLaunch.isRunning());
        expectEquals (shared->attempts.size(), beforeFirstLaunch + 1);
        expectEquals (shared->attempts.getLast().input, juce::String ("Microphone A"));
        expectEquals (shared->attempts.getLast().output, juce::String ("CABLE Input"));
    }
};
static AudioDeviceRegression audioDeviceRegression;
