#include <juce_core/juce_core.h>
#include "state/Persistence.h"
#include "audio/AudioEngine.h"
#include "audio/PluginChain.h"
#include "audio/ScanCoordinator.h"
#include "audio/DeviceSelection.h"
#include <thread>

struct StabilityTests : juce::UnitTest
{
    StabilityTests() : UnitTest ("State and routing regressions") {}
    void runTest() override
    {
        beginTest ("a default virtual microphone never creates a first-run cable feedback loop");
        const juce::StringArray inputs { "CABLE Output (VB-Audio Virtual Cable)", "Loopback Main 7/8 (Minifuse 2)",
            "Mix 3/4 (Minifuse 2)", "Input 1/2 (Minifuse 2)" };
        expectEquals (preferredMicrophone (inputs, 0), inputs[3]);
        expectEquals (preferredMicrophone (inputs, 3), inputs[3]);
        expect (preferredMicrophone ({ inputs[0], inputs[1] }, 0).isEmpty());
        beginTest ("invalid persisted settings fall back to safe defaults");
        juce::ValueTree invalid ("MicVST");
        invalid.setProperty ("sampleRate", -1, nullptr);
        invalid.setProperty ("userBufferSize", -300, nullptr);
        invalid.setProperty ("inputChannel", 99, nullptr);
        auto validated = fromValueTree (invalid);
        expectEquals (validated.sampleRate, 48000.0);
        expectEquals (validated.bufferSize, 0);
        expectEquals (validated.inputChannel, 0);

        beginTest ("channel, mute, bypass and VST3 class identity survive a restart");
        MicVSTState state;
        state.inputChannel = 1; state.sampleRate = 44100; state.muted = true; state.bypassed = true;
        PluginEntryState plugin;
        plugin.fileOrId = "C:/missing/Shell.vst3"; plugin.identifier = "specific-VST3-class";
        plugin.displayName = "My EQ"; plugin.state.append ("saved-preset", 12);
        state.plugins.add (plugin);
        auto restored = fromValueTree (toValueTree (state));
        expectEquals (restored.inputChannel, 1); expectEquals (restored.sampleRate, 44100.0);
        expect (restored.muted && restored.bypassed);
        expectEquals (restored.plugins[0].identifier, plugin.identifier);
        expectEquals (restored.plugins[0].displayName, plugin.displayName);
        expect (restored.plugins[0].state == plugin.state);

        beginTest ("a truncated config recovers the last valid atomic-save backup");
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("crystalvoice-regression", ".xml");
        state.inputDevice = "First microphone"; expect (saveStateToFile (state, file));
        state.inputDevice = "Second microphone"; expect (saveStateToFile (state, file));
        expectEquals (loadStateFromFile (file).inputDevice, state.inputDevice);
        file.replaceWithText ("<MicVST broken");
        expectEquals (loadStateFromFile (file).inputDevice, juce::String ("First microphone"));
        file.deleteFile(); file.getSiblingFile (file.getFileName() + ".bak").deleteFile();

        beginTest ("missing plugins keep order and preset blobs instead of disappearing");
        AudioEngine engine;
        state.inputDevice = "Disconnected microphone"; state.outputDevice = "Disconnected cable";
        PluginEntryState second = plugin; second.displayName = "Second effect";
        second.fileOrId = "C:/missing/Second.vst3"; second.identifier = "second-class";
        state.plugins.add (second);
        engine.applyState (state);
        expectEquals ((int) engine.getChain().entries().size(), 2);
        expect (engine.getChain().entries()[0].isUnavailable());
        auto captured = engine.captureState();
        expectEquals (captured.inputDevice, state.inputDevice);
        expectEquals (captured.outputDevice, state.outputDevice);
        expectEquals (captured.plugins.size(), 2);
        expect (captured.plugins[0].state == plugin.state);
        expectEquals (captured.plugins[1].displayName, second.displayName);

        beginTest ("read-only cable discovery preserves the requested device route");
        engine.detectCableOutput();
        expectEquals (engine.captureState().inputDevice, state.inputDevice);
        expectEquals (engine.captureState().outputDevice, state.outputDevice);

        beginTest ("failed device changes do not overwrite a saved route");
        expect (engine.setDeviceConfig ("Another nonexistent microphone", "Another nonexistent cable", 0, 0).isNotEmpty());
        expectEquals (engine.captureState().inputDevice, state.inputDevice);
        expectEquals (engine.captureState().outputDevice, state.outputDevice);

        beginTest ("retrying an unavailable audio device retains the requested route");
        expect (engine.retryAudioDevice().isNotEmpty());
        expectEquals (engine.captureState().inputDevice, state.inputDevice);
        expectEquals (engine.captureState().outputDevice, state.outputDevice);

        beginTest ("delayed row actions track the same effect after reorder");
        auto& chain = engine.getChain();
        const auto firstId = chain.entries()[0].id, secondId = chain.entries()[1].id;
        chain.movePlugin (0, 1);
        expectEquals (chain.indexOf (firstId), 1); expectEquals (chain.indexOf (secondId), 0);
        chain.removePlugin (chain.indexOf (secondId));
        expectEquals (chain.indexOf (firstId), 0); expectEquals (chain.indexOf (secondId), -1);
        expect (engine.captureState().plugins[0].state == plugin.state);
    }
};
static StabilityTests stabilityTests;

struct BundleAliasTest : juce::UnitTest
{
    BundleAliasTest() : UnitTest ("Bundle cache aliases") {}
    void runTest() override
    {
        beginTest ("a cached inner binary prevents rescanning its unchanged bundle");
        const auto bundle = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getNonexistentChildFile ("crystalvoice-bundle", ".vst3");
        const auto binary = bundle.getChildFile ("Contents/x86_64-win/effect.vst3");
        binary.getParentDirectory().createDirectory(); binary.replaceWithText ("test binary");
        juce::KnownPluginList list; juce::PluginDescription description;
        description.name = "Effect"; description.pluginFormatName = "VST3";
        description.fileOrIdentifier = binary.getFullPathName(); description.lastFileModTime = binary.getLastModificationTime();
        list.addType (description); MicVST3Format format; juce::Array<SkippedPlugin> skipped;
        auto pending = filterFilesNeedingScan ({ bundle.getFullPathName() }, list, format, skipped);
        expect (pending.isEmpty());
        binary.setLastModificationTime (description.lastFileModTime + juce::RelativeTime::seconds (5));
        pending = filterFilesNeedingScan ({ bundle.getFullPathName() }, list, format, skipped);
        expectEquals (pending.size(), 1);
        // Only the private test-created directory is removed.
        const auto tempRoot = juce::File::getSpecialLocation (juce::File::tempDirectory);
        expect (bundle.isAChildOf (tempRoot) && bundle.getFileName().startsWith ("crystalvoice-bundle"));
        if (bundle.isAChildOf (tempRoot) && bundle.getFileName().startsWith ("crystalvoice-bundle")) bundle.deleteRecursively();
    }
};
static BundleAliasTest bundleAliasTest;

struct GraphSignalTests : juce::UnitTest
{
    GraphSignalTests() : UnitTest ("Processed graph signal") {}
    void runTest() override
    {
        using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
        juce::AudioProcessorGraph graph;
        graph.setPlayConfigDetails (1, 2, 48000.0, 64);
        const auto input = graph.addNode (std::make_unique<IO> (IO::audioInputNode));
        const auto output = graph.addNode (std::make_unique<IO> (IO::audioOutputNode));
        PluginChain chain (graph, input->nodeID, output->nodeID);
        chain.rebuildConnections();
        graph.prepareToPlay (48000.0, 64);
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        auto checkMono = [&]
        {
            buffer.clear();
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                buffer.setSample (0, s, (s % 9 - 4) * 0.1f);
            graph.processBlock (buffer, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int s = 0; s < buffer.getNumSamples(); ++s)
                    expectWithinAbsoluteError (buffer.getSample (channel, s), (s % 9 - 4) * 0.1f, 0.00001f);
        };

        beginTest ("an empty chain carries the physical mono input to both output channels");
        expectEquals (input->getProcessor()->getMainBusNumOutputChannels(), 1);
        expectEquals (output->getProcessor()->getMainBusNumInputChannels(), 2);
        expectEquals ((int) graph.getConnections().size(), 2);
        checkMono();

        beginTest ("missing effects and an enabled converter preserve the same samples");
        PluginEntryState missing; missing.fileOrId = "C:/missing/EQ.vst3";
        chain.addUnavailable (missing, "Not installed"); chain.addMonoToStereo();
        chain.rebuildConnections(); checkMono();

        beginTest ("bypassing a converter still delivers mono to both virtual microphone channels");
        chain.setBypass (1, true); chain.rebuildConnections(); checkMono();
        graph.releaseResources();

        beginTest ("stereo to mono averages both inputs and fans the result out to both outputs");
        graph.setPlayConfigDetails (2, 2, 48000.0, 64);
        chain.addStereoToMono(); graph.prepareToPlay (48000.0, 64); chain.rebuildConnections();
        buffer.clear();
        for (int s = 0; s < 64; ++s) { buffer.setSample (0, s, 0.2f); buffer.setSample (1, s, 0.6f); }
        graph.processBlock (buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            for (int s = 0; s < 64; ++s) expectWithinAbsoluteError (buffer.getSample (channel, s), 0.4f, 0.00001f);
        graph.releaseResources();
    }
};
static GraphSignalTests graphSignalTests;

struct DeviceErrorTests : juce::UnitTest
{
    DeviceErrorTests() : UnitTest ("Runtime device errors") {}
    void runTest() override
    {
        beginTest ("driver errors are delivered to the UI on the message thread");
        AudioEngine engine;
        juce::StringArray errors;
        bool callbackOnMainThread = true;
        engine.onStatusChanged = [&]
        {
            callbackOnMainThread = callbackOnMainThread && juce::MessageManager::getInstance()->isThisTheMessageThread();
            errors.add (engine.getDeviceError());
        };
        std::thread driver ([&] { engine.audioDeviceError ("The driver stopped streaming"); });
        driver.join();
        expect (errors.isEmpty());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        expect (errors.contains ("The driver stopped streaming"));
        expect (callbackOnMainThread);

        beginTest ("queued driver errors are discarded after shutdown");
        bool calledAfterDestruction = false;
        {
            auto exiting = std::make_unique<AudioEngine>();
            exiting->onStatusChanged = [&] { calledAfterDestruction = true; };
            exiting->audioDeviceError ("A late driver error");
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        expect (! calledAfterDestruction);
    }
};
static DeviceErrorTests deviceErrorTests;
