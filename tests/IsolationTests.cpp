#include "audio/isolation/IsolatedPlugin.h"
#include "audio/isolation/Protocol.h"
#include "IsolationFixtures.h"
#include "audio/AudioEngine.h"
#include "FakeAudioDevices.h"
#include "ui/PluginListView.h"
#include <thread>
#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#endif

namespace
{
float stateValue (const juce::MemoryBlock& block)
{ float result = -1; if (block.getSize() == 4) std::memcpy (&result, block.getData(), 4); return result; }
juce::AudioBuffer<float> render (IsolatedPlugin& plugin, float value = 0.2f, double budget = 100.0)
{
    juce::AudioBuffer<float> buffer (1, 480); juce::FloatVectorOperations::fill (buffer.getWritePointer (0), value, 480);
    juce::MidiBuffer midi; isolation::CallbackBudget deadline (budget); plugin.processBlock (buffer, midi); return buffer;
}
}
struct IsolationTests : juce::UnitTest
{
    IsolationTests() : UnitTest ("Per-instance plugin process isolation") {}
    void runTest() override
    {
        beginTest ("callback budgets nest and the same deadline spans all plugin instances");
        { isolation::CallbackBudget outer (100); const auto end = isolation::CallbackBudget::deadline (1000);
          { isolation::CallbackBudget inner (10); expect (isolation::CallbackBudget::deadline (1000) < end); }
          expectEquals (isolation::CallbackBudget::deadline (1000), end); }

        beginTest ("duplicate plugin classes own separate workers and private parameters");
        juce::String error;
        auto first = IsolatedPlugin::create (isolationTest::description ("gain"), 48000, 480, error);
        expect (first != nullptr, error); if (first == nullptr) return;
        auto second = IsolatedPlugin::create (isolationTest::description ("gain"), 48000, 480, error);
        expect (second != nullptr, error); if (second == nullptr) return;
        expect (first->workerProcessId() != isolation::AudioTransport::currentProcessId()); expect (first->workerProcessId() != second->workerProcessId());
        expectWithinAbsoluteError (render (*first).getSample (0, 300), 0.1f, 0.00001f);
        float gain = 0.8f; first->setStateInformation (&gain, 4);
        expectWithinAbsoluteError (render (*first).getSample (0, 300), 0.16f, 0.00001f);
        expectWithinAbsoluteError (render (*second).getSample (0, 300), 0.1f, 0.00001f);
        juce::MemoryBlock state; first->getStateInformation (state); expectWithinAbsoluteError (stateValue (state), 0.8f, 0.00001f);

        beginTest ("crashing during DLL creation cannot take down the host or another worker");
        auto loadCrash = IsolatedPlugin::create (isolationTest::description ("load-crash"), 48000, 480, error);
        expect (loadCrash == nullptr); expect (error.isNotEmpty()); expect (! second->failed());
        expectWithinAbsoluteError (render (*second).getSample (0, 300), 0.1f, 0.00001f);

        beginTest ("DSP process exit fades only its output and preserves the last good state");
        auto crash = IsolatedPlugin::create (isolationTest::description ("process-crash"), 48000, 480, error);
        expect (crash != nullptr, error); if (crash == nullptr) return;
        gain = 0.75f; crash->setStateInformation (&gain, 4); render (*crash); render (*crash);
        auto crashed = render (*crash); expect (crash->failed()); expectEquals (crashed.getSample (0, 400), 0.0f);
        crash->getStateInformation (state); expectWithinAbsoluteError (stateValue (state), 0.75f, 0.00001f);
        expect (! second->failed()); expectWithinAbsoluteError (render (*second).getSample (0, 300), 0.1f, 0.00001f);

        beginTest ("a stuck DSP has bounded waits while the other instance continues rendering");
        auto hung = IsolatedPlugin::create (isolationTest::description ("process-hang"), 48000, 480, error);
        expect (hung != nullptr, error); if (hung == nullptr) return;
        const auto before = second->renderedBlocks(); const auto started = juce::Time::getMillisecondCounterHiRes();
        while (! hung->failed() && juce::Time::getMillisecondCounterHiRes() - started < 1200)
        {
            auto silence = render (*hung, 0.2f, 2); expectEquals (silence.getSample (0, 400), 0.0f);
            render (*second); juce::Thread::sleep (10);
        }
        expect (hung->failed()); expect (second->renderedBlocks() > before + 5); expect (! second->failed());
        expect (juce::Time::getMillisecondCounterHiRes() - started < 1200);

        beginTest ("late completed packets are discarded instead of replaying an older input");
        auto delayed = IsolatedPlugin::create (isolationTest::description ("delayed"), 48000, 480, error);
        expect (delayed != nullptr, error); if (delayed == nullptr) return;
        expectEquals (render (*delayed, 0.7f, 2).getSample (0, 400), 0.0f);
        juce::Thread::sleep (50);
        expectWithinAbsoluteError (render (*delayed, -0.4f).getSample (0, 400), -0.2f, 0.00001f);
        expect (! delayed->failed()); expect (delayed->missedBlocks() >= 1);

        beginTest ("slow serialization pauses only that worker and cannot block other audio");
        auto slow = IsolatedPlugin::create (isolationTest::description ("state-slow"), 48000, 480, error);
        expect (slow != nullptr, error); if (slow == nullptr) return;
        std::atomic<bool> running { true }; std::atomic<int> renders { 0 };
        std::thread audio ([&] { while (running.load()) { render (*second); ++renders; juce::Thread::sleep (1); } });
        while (renders.load() < 3) juce::Thread::sleep (1);
        const auto previous = renders.load(); slow->getStateInformation (state); running = false; audio.join();
        expect (renders.load() > previous + 5); expect (! slow->failed()); expect (! second->failed());

        beginTest ("state crashes and hangs return cached parameters without trapping the host");
        for (const auto& mode : { "state-crash", "state-hang" })
        {
            auto bad = IsolatedPlugin::create (isolationTest::description (mode), 48000, 480, error);
            expect (bad != nullptr, error); if (bad == nullptr) continue;
            gain = 0.6f; bad->setStateInformation (&gain, 4);
            const auto start = juce::Time::getMillisecondCounterHiRes(); bad->getStateInformation (state);
            expect (juce::Time::getMillisecondCounterHiRes() - start < 3000); expect (bad->failed());
            expectWithinAbsoluteError (stateValue (state), 0.6f, 0.00001f); expect (! second->failed());
        }
        beginTest ("oversized audio blocks fail safely without overwriting shared buffers");
        juce::AudioBuffer<float> oversized (1, isolation::maxSamples + 1); oversized.clear(); juce::MidiBuffer midi;
        first->processBlock (oversized, midi); expect (first->failed()); expectEquals (oversized.getSample (0, 8192), 0.0f); expect (! second->failed());

        beginTest ("reloading a failed chain slot preserves identity, settings and other workers");
        AudioEngine engine (testAudio::devices());
        engine.setPreferredBufferSize (480);
        expect (engine.initialise ("Microphone A", "CABLE Input").isEmpty());
        auto crashDesc = isolationTest::description ("process-crash"), gainDesc = isolationTest::description ("gain");
        engine.getKnownPlugins().addType (crashDesc); engine.getKnownPlugins().addType (gainDesc);
        expect (engine.getChain().addPlugin (engine.getFormatManager(), crashDesc, 48000, 480, error), error);
        expect (engine.getChain().addPlugin (engine.getFormatManager(), gainDesc, 48000, 480, error), error);
        const auto slot = engine.getChain().entries()[0].id, otherSlot = engine.getChain().entries()[1].id;
        auto proxyAt = [&] (int index) { return dynamic_cast<IsolatedPlugin*> (engine.getGraph().getNodeForId (engine.getChain().entries()[(size_t) index].node)->getProcessor()); };
        auto* failing = proxyAt (0); auto* other = proxyAt (1);
        const auto originalPid = failing->workerProcessId(), otherPid = other->workerProcessId();
        gain = 0.75f; failing->setStateInformation (&gain, 4);
        render (*failing); render (*failing); render (*failing);
        expect (failing->failed()); expectEquals (engine.getFailedPluginCount(), 1);
        engine.retryMissingPlugins(); expectEquals (proxyAt (0)->workerProcessId(), originalPid);
        PluginListView view (engine); view.setSize (660, 450);
        auto* viewport = dynamic_cast<juce::Viewport*> (view.findChildWithID ("effect-viewport"));
        auto* rows = viewport != nullptr ? viewport->getViewedComponent() : nullptr;
        expect (rows != nullptr);
        bool reloadFound = false;
        if (rows != nullptr) for (auto* child : rows->getChildComponent (0)->getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*> (child)) reloadFound = reloadFound || button->getButtonText() == "Reload";
        expect (reloadFound);
        engine.retryMissingPlugins (true, slot);
        expectEquals (engine.getFailedPluginCount(), 0); expectEquals (engine.getChain().entries()[0].id, slot);
        expectEquals (engine.getChain().entries()[1].id, otherSlot); expect (proxyAt (0)->workerProcessId() != originalPid);
        expectEquals (proxyAt (1)->workerProcessId(), otherPid);
        expectWithinAbsoluteError (stateValue (proxyAt (0)->lastGoodState()), 0.75f, 0.00001f);
        expectWithinAbsoluteError (render (*proxyAt (0)).getSample (0, 300), 0.15f, 0.00001f);
        expectWithinAbsoluteError (render (*proxyAt (1)).getSample (0, 300), 0.1f, 0.00001f);

        beginTest ("a worker dying while loading preset parameters leaves the live chain unchanged");
        auto setCrash = isolationTest::description ("set-crash"); engine.getKnownPlugins().addType (setCrash);
        PluginEntryState saved; saved.fileOrId = setCrash.fileOrIdentifier; saved.identifier = setCrash.createIdentifierString();
        saved.displayName = setCrash.name; saved.state.replaceAll (&gain, 4);
        ChainPreset preset; preset.name = "Broken fixture"; preset.plugins.add (saved);
        expect (! engine.loadPreset (preset, error)); expect (error.isNotEmpty());
        expectEquals ((int) engine.getChain().entries().size(), 2); expectEquals (engine.getChain().entries()[0].id, slot);
        expectEquals (proxyAt (1)->workerProcessId(), otherPid); expect (! proxyAt (1)->failed());

        beginTest ("a startup state failure preserves the slot and keeps the microphone muted");
        AudioEngine startup (testAudio::devices()); startup.setPreferredBufferSize (480);
        startup.getKnownPlugins().addType (setCrash); startup.getKnownPlugins().addType (gainDesc);
        MicVSTState startupState; startupState.inputDevice = "Microphone A"; startupState.outputDevice = "CABLE Input";
        startupState.bufferSize = 480; startupState.plugins.add (saved);
        PluginEntryState healthyState; healthyState.fileOrId = gainDesc.fileOrIdentifier; healthyState.identifier = gainDesc.createIdentifierString();
        startupState.plugins.add (healthyState); startup.applyState (startupState);
        expect (startup.isMuted()); expect (startup.isRunning()); expectEquals ((int) startup.getChain().entries().size(), 2);
        expect (startup.getChain().entries()[0].isUnavailable()); expect (! startup.getChain().entries()[1].isUnavailable());
        expect (startup.captureState (false).plugins[0].state == saved.state);
        auto* startupDevice = dynamic_cast<testAudio::Device*> (startup.getDeviceManager().getCurrentAudioDevice());
        expect (startupDevice != nullptr);
        if (startupDevice != nullptr) expectEquals (startupDevice->render().getSample (0, 479), 0.0f);

        beginTest ("automatic scan retry never relaunches a worker that failed startup state restore");
        const auto onceMarker = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("CrystalVoiceStateCrashOnce", ".txt", false);
        auto onceDesc = isolationTest::description ("set-crash-once:" + onceMarker.getFullPathName());
        AudioEngine onceEngine (testAudio::devices()); onceEngine.getKnownPlugins().addType (onceDesc);
        MicVSTState onceState; onceState.inputDevice = "Microphone A"; onceState.outputDevice = "CABLE Input"; onceState.bufferSize = 480;
        PluginEntryState onceSaved; onceSaved.fileOrId = onceDesc.fileOrIdentifier; onceSaved.identifier = onceDesc.createIdentifierString(); onceSaved.state.replaceAll (&gain, 4);
        onceState.plugins.add (onceSaved); onceEngine.applyState (onceState);
        expect (onceMarker.existsAsFile()); expect (onceEngine.getChain().entries()[0].isUnavailable()); expect (onceEngine.isMuted());
        const auto onceId = onceEngine.getChain().entries()[0].id;
        onceEngine.retryMissingPlugins(); expect (onceEngine.getChain().entries()[0].isUnavailable());
        expect (onceEngine.captureState (false).plugins[0].state == onceSaved.state);
        onceEngine.retryMissingPlugins (true, onceId); expect (! onceEngine.getChain().entries()[0].isUnavailable());
        expectEquals (onceEngine.getChain().entries()[0].id, onceId); expect (onceEngine.isMuted());
        expect (onceEngine.captureState (false).plugins[0].state == onceSaved.state); onceMarker.deleteFile();

        beginTest ("a native access violation is contained in its worker");
        auto nativeCrash = IsolatedPlugin::create (isolationTest::description ("native-crash"), 48000, 480, error);
        expect (nativeCrash != nullptr, error);
        if (nativeCrash != nullptr)
        {
            render (*nativeCrash); render (*nativeCrash); render (*nativeCrash);
            expect (nativeCrash->failed()); expect (! second->failed());
            expectWithinAbsoluteError (render (*second).getSample (0, 300), 0.1f, 0.00001f);
        }

        beginTest ("a stuck worker is detected even after the host stops submitting audio");
        auto inactiveHung = IsolatedPlugin::create (isolationTest::description ("process-hang"), 48000, 480, error);
        expect (inactiveHung != nullptr, error);
        if (inactiveHung != nullptr)
        {
            render (*inactiveHung, 0.2f, 2);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            expect (inactiveHung->failed()); expect (! second->failed());
        }

        beginTest ("workers die when their host exits without running destructors");
       #if JUCE_WINDOWS
        auto report = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("CrystalVoiceWorkerOwner", ".txt", false);
        juce::ChildProcess owner;
        expect (owner.start ({ juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--isolation-owner", report.getFullPathName() }, 0));
        const auto ownerDeadline = juce::Time::getMillisecondCounterHiRes() + 7000;
        while (! report.existsAsFile() && owner.isRunning() && juce::Time::getMillisecondCounterHiRes() < ownerDeadline) juce::Thread::sleep (5);
        const auto workerPid = (DWORD) report.loadFileAsString().getLargeIntValue(); expect (workerPid != 0);
        HANDLE workerHandle = workerPid != 0 ? OpenProcess (SYNCHRONIZE, FALSE, workerPid) : nullptr;
        expect (owner.waitForProcessToFinish (5000)); expectEquals ((int) owner.getExitCode(), 0);
        if (workerHandle != nullptr) { expectEquals ((int) WaitForSingleObject (workerHandle, 2000), (int) WAIT_OBJECT_0); CloseHandle (workerHandle); }
        else if (workerPid != 0) expect (GetLastError() == ERROR_INVALID_PARAMETER);
        report.deleteFile();
       #endif
    }
};
static IsolationTests isolationTests;
