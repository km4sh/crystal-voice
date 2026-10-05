#include "audio/AudioEngine.h"
#include "audio/PluginIdentity.h"
#include "state/Presets.h"
#include "state/Recovery.h"
#include "ui/PluginListView.h"
#include "FakeAudioDevices.h"
#include <limits>
#include <thread>

namespace
{
struct TestProfile
{
    juce::File previous = settingsDirectory();
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("CrystalVoiceCoreTests", {}, true);
    TestProfile() { folder.createDirectory(); setSettingsDirectory (folder); }
    ~TestProfile() { setSettingsDirectory (previous); folder.deleteRecursively(); }
};
float valueOf (const juce::MemoryBlock& block)
{
    float value = -1.0f;
    if (block.getSize() == sizeof (value)) std::memcpy (&value, block.getData(), sizeof (value));
    return value;
}
juce::PluginDescription fixtureDescription()
{
    juce::PluginDescription desc;
    desc.name = "Fixture Gain"; desc.manufacturerName = "Crystal Voice Tests";
    desc.pluginFormatName = "Fixture"; desc.fileOrIdentifier = "fixture:gain"; desc.uniqueId = 123456;
    desc.numInputChannels = desc.numOutputChannels = 1;
    return desc;
}
struct FixtureCounters
{
    std::atomic<int> captures { 0 }, renders { 0 }, captureDelay { 0 };
    std::atomic<bool> processing { false }, overlap { false };
};
class FixturePlugin : public juce::AudioPluginInstance
{
public:
    struct GainParameter : juce::HostedAudioProcessorParameter
    {
        float getValue() const override { return value.load(); }
        float get() const { return getValue(); }
        void setValue (float next) override { value = juce::jlimit (0.0f, 1.0f, next); }
        float getDefaultValue() const override { return 0.5f; }
        juce::String getName (int) const override { return "Gain"; }
        juce::String getLabel() const override { return {}; }
        juce::String getText (float next, int) const override { return juce::String (next); }
        float getValueForText (const juce::String& text) const override { return text.getFloatValue(); }
        juce::String getParameterID() const override { return "gain"; }
        std::atomic<float> value { 0.5f };
    };
    explicit FixturePlugin (std::shared_ptr<FixtureCounters> state)
        : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::mono(), true)
                                               .withOutput ("Out", juce::AudioChannelSet::mono(), true)), counters (state)
    { auto parameter = std::make_unique<GainParameter>(); gain = parameter.get(); addHostedParameter (std::move (parameter)); }
    void fillInPluginDescription (juce::PluginDescription& desc) const override { desc = fixtureDescription(); }
    const juce::String getName() const override { return "Fixture Gain"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    { counters->processing = true; buffer.applyGain (gain->get()); ++counters->renders; counters->processing = false; }
    void getStateInformation (juce::MemoryBlock& state) override
    {
        if (counters->processing.load()) counters->overlap = true;
        ++counters->captures;
        if (counters->captureDelay.load() > 0) juce::Thread::sleep (counters->captureDelay.load());
        const float value = gain->get(); state.replaceAll (&value, sizeof (value));
    }
    void setStateInformation (const void* data, int size) override
    { if (size == sizeof (float)) { float value; std::memcpy (&value, data, sizeof (value)); gain->setValueNotifyingHost (value); } }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    GainParameter* gain;
private:
    std::shared_ptr<FixtureCounters> counters;
};
class FixtureFormat : public juce::AudioPluginFormat
{
public:
    explicit FixtureFormat (std::shared_ptr<FixtureCounters> state) : counters (state) {}
    juce::String getName() const override { return "Fixture"; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
    bool fileMightContainThisPluginType (const juce::String&) override { return true; }
    juce::String getNameOfPluginFromIdentifier (const juce::String&) override { return "Fixture Gain"; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
protected:
    void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback callback) override
    { callback (std::make_unique<FixturePlugin> (counters), {}); }
private:
    std::shared_ptr<FixtureCounters> counters;
};
}

struct CoreFeatureTests : juce::UnitTest
{
    CoreFeatureTests() : UnitTest ("Core presets and realtime safety") {}
    void runTest() override
    {
        TestProfile profile;
        beginTest ("legacy path identifiers relocate by class identity without dropping the state blob");
        juce::PluginDescription original = fixtureDescription(); original.pluginFormatName = "VST3";
        original.fileOrIdentifier = "C:/Old/Gain.vst3";
        PluginEntryState saved; saved.fileOrId = original.fileOrIdentifier;
        saved.identifier = original.createIdentifierString(); saved.displayName = original.name;
        saved.state.append ("original parameters", 19);
        auto relocated = original; relocated.fileOrIdentifier = "C:/Program Files/Common Files/VST3/Gain.vst3";
        juce::KnownPluginList known; known.addType (relocated);
        auto resolved = resolvePlugin (saved, known);
        expect (resolved != nullptr);
        if (resolved != nullptr) expectEquals (resolved->fileOrIdentifier, relocated.fileOrIdentifier);
        expectEquals ((int) saved.state.getSize(), 19);

        beginTest ("ambiguous relocated installations never silently select the first copy");
        auto duplicate = relocated; duplicate.fileOrIdentifier = "D:/Another/Gain.vst3"; known.addType (duplicate);
        expect (resolvePlugin (saved, known) == nullptr);
        saved.identifier = relocated.createIdentifierString(); expect (resolvePlugin (saved, known) != nullptr);

        beginTest ("new stable identity tolerates display-name changes and rejects the wrong vendor");
        known.clear(); auto renamed = relocated; renamed.name = "Renamed Gain"; known.addType (renamed);
        saved.identifier = original.createIdentifierString(); saved.format = "VST3";
        saved.classUid = juce::String::toHexString (original.uniqueId); saved.manufacturer = original.manufacturerName;
        expect (resolvePlugin (saved, known) != nullptr);
        saved.manufacturer = "Another vendor"; expect (resolvePlugin (saved, known) == nullptr);
        saved.manufacturer = original.manufacturerName;

        beginTest ("named presets round-trip order, bypass and binary plugin parameters");
        PresetStore store;
        ChainPreset preset; preset.name = "  Daily voice  "; preset.plugins.add (saved);
        PluginEntryState converter; converter.fileOrId = PluginChain::monoToStereoId; converter.bypassed = true;
        preset.plugins.add (converter); juce::String error;
        expect (store.save (preset, error)); expectEquals (preset.name, juce::String ("Daily voice"));
        ChainPreset loaded; expect (store.load (preset.id, loaded, error)); expectEquals (loaded.plugins.size(), 2);
        if (loaded.plugins.size() == 2)
        { expect (loaded.plugins[0].state == saved.state); expect (loaded.plugins[1].bypassed); expectEquals (loaded.plugins[0].classUid, saved.classUid); }
        expectEquals (store.list().size(), 1);

        const auto malformed = profile.folder.getChildFile ("bad.cvpreset");
        const auto previousId = loaded.id;
        beginTest ("corrupted, truncated and oversized state lengths are rejected before allocation");
        for (const auto& blob : { "-1.", "99999999.", "4.A", "1.!!", "1.AAAA" })
        {
            juce::MemoryBlock destination; destination.append ("keep", 4);
            expect (! decodePluginState (blob, destination)); expectEquals ((int) destination.getSize(), 4);
            malformed.replaceWithText ("<CrystalVoicePreset version=\"1\" id=\"" + preset.id + "\" name=\"Bad\"><plugins><plugin fileOrId=\"fixture\" state=\"" + blob + "\"/></plugins></CrystalVoicePreset>");
            expect (! store.importFile (malformed, loaded, error)); expectEquals (loaded.id, previousId);
        }

        beginTest ("invalid import and path traversal leave existing presets and results intact");
        malformed.replaceWithText ("<CrystalVoicePreset version=\"99\"/> ");
        expect (! store.importFile (malformed, loaded, error)); expectEquals (loaded.id, previousId);
        expect (! store.load ("../config", loaded, error)); expectEquals (loaded.id, previousId);
        expectEquals (store.list().size(), 1);

        beginTest ("startup pins a saved chain without replacing routing or the edited session");
        MicVSTState session; session.inputDevice = "Microphone B"; session.outputDevice = "CABLE Input";
        session.sampleRate = 44100; session.muted = true; session.startupPreset = preset.id; session.presetModified = true;
        auto edited = converter; edited.bypassed = false; session.plugins.add (edited);
        expect (restoreStartupPreset (session, store, error)); expectEquals (session.plugins.size(), 2);
        expectEquals (session.inputDevice, juce::String ("Microphone B")); expectEquals (session.sampleRate, 44100.0); expect (session.muted);
        expect (! session.presetModified);
        session.startupPreset = juce::Uuid().toString().removeCharacters ("-");
        expect (! restoreStartupPreset (session, store, error)); expectEquals (session.plugins.size(), 2);

        beginTest ("a failed staged load preserves the live chain, processor IDs and route");
        AudioEngine engine (testAudio::devices()); MicVSTState state;
        state.inputDevice = "Microphone A"; state.outputDevice = "CABLE Input"; engine.applyState (state);
        engine.getChain().addMonoToStereo(); engine.rebuildGraph();
        const auto oldNode = engine.getChain().entries()[0].node; const int oldNodes = engine.getGraph().getNumNodes();
        ChainPreset invalid; invalid.id = preset.id; invalid.name = "Missing";
        invalid.plugins.add (converter); invalid.plugins.add (saved);
        int replacements = 0; engine.onChainReplacing = [&] { ++replacements; };
        expect (! engine.loadPreset (invalid, error)); expectEquals (replacements, 0);
        expectEquals ((int) engine.getChain().entries().size(), 1); expect (engine.getChain().entries()[0].node == oldNode);
        expectEquals (engine.getGraph().getNumNodes(), oldNodes);
        auto* device = dynamic_cast<testAudio::Device*> (engine.getDeviceManager().getCurrentAudioDevice());
        expect (device != nullptr); if (device != nullptr) expectWithinAbsoluteError (device->render().getSample (0, 20), 0.2f, 0.00001f);

        beginTest ("plugin parameters survive save/load and quick host settings use cached state");
        auto counters = std::make_shared<FixtureCounters>(); engine.getFormatManager().addFormat (std::make_unique<FixtureFormat> (counters));
        const auto description = fixtureDescription(); engine.getKnownPlugins().addType (description);
        ChainPreset gainPreset; gainPreset.id = juce::Uuid().toString().removeCharacters ("-"); gainPreset.name = "Gain";
        PluginEntryState gainState; gainState.fileOrId = description.fileOrIdentifier; gainState.identifier = description.createIdentifierString();
        gainState.displayName = description.name; float gainValue = 0.25f; gainState.state.append (&gainValue, sizeof (gainValue)); gainPreset.plugins.add (gainState);
        expect (engine.loadPreset (gainPreset, error)); expectEquals (replacements, 1);
        auto* processor = dynamic_cast<FixturePlugin*> (engine.getGraph().getNodeForId (engine.getChain().entries()[0].node)->getProcessor());
        expect (processor != nullptr); if (processor == nullptr) return;
        processor->gain->setValueNotifyingHost (0.8f);
        engine.setMuted (true); engine.setMasterBypass (true);
        auto cached = engine.captureState (false);
        expectEquals (counters->captures.load(), 0); expectWithinAbsoluteError (valueOf (cached.plugins[0].state), 0.25f, 0.00001f);
        expect (engine.snapshotPluginStates()); expectEquals (counters->captures.load(), 1);
        expectWithinAbsoluteError (valueOf (engine.captureState (false).plugins[0].state), 0.8f, 0.00001f);
        expect (engine.savePreset ("Everyday", true, error)); const auto pinned = engine.getCurrentPreset();
        expect (engine.setStartupPreset (pinned, error));
        processor->gain->setValueNotifyingHost (0.3f); expect (engine.snapshotPluginStates());
        expect (store.load (pinned, loaded, error)); expectWithinAbsoluteError (valueOf (loaded.plugins[0].state), 0.8f, 0.00001f);
        expect (engine.loadPreset (loaded, error));
        processor = dynamic_cast<FixturePlugin*> (engine.getGraph().getNodeForId (engine.getChain().entries()[0].node)->getProcessor());
        expect (processor != nullptr); if (processor == nullptr) return;
        expectWithinAbsoluteError (processor->gain->get(), 0.8f, 0.00001f);

        beginTest ("modified session status survives serialization without overwriting its named preset");
        engine.markPresetModified(); const auto editedSession = fromValueTree (toValueTree (engine.captureState (false)));
        expect (editedSession.presetModified); expectEquals (editedSession.currentPreset, pinned);
        expect (store.load (pinned, loaded, error)); expectWithinAbsoluteError (valueOf (loaded.plugins[0].state), 0.8f, 0.00001f);

        beginTest ("audio callbacks continue while slow parameter serialization quiesces plugin DSP");
        engine.setMuted (false); engine.setMasterBypass (false);
        counters->captureDelay = 30; std::atomic<bool> running { true }; std::atomic<int> callbacks { 0 };
        std::thread audio ([&] { while (running.load()) { device->render(); ++callbacks; juce::Thread::sleep (1); } });
        while (callbacks.load() < 3) juce::Thread::sleep (1);
        const int before = callbacks.load(); expect (engine.snapshotPluginStates (true));
        running = false; audio.join(); expect (callbacks.load() > before + 5); expect (! counters->overlap.load()); counters->captureDelay = 0;

        beginTest ("coalesced session writes flush the newest state and report disk failures");
        const auto sessionFile = profile.folder.getChildFile ("session.xml");
        { SessionWriter writer (sessionFile); for (int i = 0; i < 12; ++i) { state.inputChannel = i % 2; state.currentPreset = juce::String (i); writer.enqueue (state); } writer.flush(); expect (writer.takeError().isEmpty()); }
        expectEquals (loadStateFromFile (sessionFile).currentPreset, juce::String ("11"));
        const auto obstacle = profile.folder.getChildFile ("not-a-folder"); obstacle.replaceWithText ("occupied");
        { SessionWriter writer (obstacle.getChildFile ("config.xml")); writer.enqueue (state); writer.flush(); expect (writer.takeError().isNotEmpty()); }

        beginTest ("interrupted sessions skip third-party effects without losing their parameters");
        RecoverySession recovery (profile.folder); expect (! recovery.begin()); expect (recovery.begin()); expect (recovery.markClean()); expect (! recovery.begin()); recovery.markClean();
        AudioEngine safeEngine (testAudio::devices()); safeEngine.setRecoveryMode (true);
        safeEngine.getKnownPlugins().addType (description); auto safeCounters = std::make_shared<FixtureCounters>();
        safeEngine.getFormatManager().addFormat (std::make_unique<FixtureFormat> (safeCounters));
        state.plugins = gainPreset.plugins; state.muted = false; safeEngine.applyState (state);
        expect (safeEngine.isMuted()); expect (safeEngine.getChain().entries()[0].isUnavailable());
        safeEngine.retryMissingPlugins(); expect (safeEngine.getChain().entries()[0].isUnavailable());
        expect (safeEngine.captureState (false).plugins[0].state == gainState.state);
        safeEngine.retryMissingPlugins (true); expect (! safeEngine.getChain().entries()[0].isUnavailable()); expect (safeEngine.isMuted());

        beginTest ("retrying one effect in safe start never automatically restores other paused effects");
        safeEngine.setRecoveryMode (true); state.plugins.add (gainState); safeEngine.applyState (state);
        safeEngine.retryMissingPlugins (true, safeEngine.getChain().entries()[0].id);
        expect (! safeEngine.getChain().entries()[0].isUnavailable()); expect (safeEngine.getChain().entries()[1].isUnavailable());
        expect (safeEngine.isRecoveryMode()); safeEngine.retryMissingPlugins(); expect (safeEngine.getChain().entries()[1].isUnavailable());
        safeEngine.retryMissingPlugins (true); expect (! safeEngine.getChain().entries()[1].isUnavailable());
        expect (! safeEngine.isRecoveryMode()); expect (safeEngine.isMuted());

        beginTest ("non-finite meter samples cannot poison later valid audio");
        LevelMeter meter; meter.prepare (48000); juce::AudioBuffer<float> samples (1, 512); samples.clear();
        samples.setSample (0, 0, std::numeric_limits<float>::quiet_NaN()); samples.setSample (0, 1, std::numeric_limits<float>::infinity()); meter.process (samples);
        expect (std::isfinite (meter.read().vu)); expect (std::isfinite (meter.read().rms));
        juce::FloatVectorOperations::fill (samples.getWritePointer (0), 0.2f, 512); for (int i = 0; i < 40; ++i) meter.process (samples);
        expect (meter.read().vu > 0.1f);

        beginTest ("output guard replaces invalid values, bounds full scale and fades over five milliseconds");
        OutputSafety safety; safety.prepare (48000, 1, false, false); juce::AudioBuffer<float> out (1, 480), dry (1, 480);
        dry.clear(); juce::FloatVectorOperations::fill (out.getWritePointer (0), 0.5f, 480);
        out.setSample (0, 0, std::numeric_limits<float>::quiet_NaN()); out.setSample (0, 1, std::numeric_limits<float>::infinity()); out.setSample (0, 2, 2.0f);
        const auto counts = safety.process (out.getArrayOfWritePointers(), 1, dry.getArrayOfReadPointers(), 1, 480, false, false, false, 0);
        expectEquals ((int) counts.invalid, 2); expectEquals ((int) counts.clipped, 1); expectEquals (out.getSample (0, 0), 0.0f); expectEquals (out.getSample (0, 2), 1.0f);
        juce::FloatVectorOperations::fill (out.getWritePointer (0), 0.5f, 480);
        safety.process (out.getArrayOfWritePointers(), 1, dry.getArrayOfReadPointers(), 1, 480, true, false, false, 0);
        expect (out.getSample (0, 0) > 0.49f); expectWithinAbsoluteError (out.getSample (0, 119), 0.25f, 0.01f); expectEquals (out.getSample (0, 239), 0.0f); expectEquals (out.getSample (0, 479), 0.0f);

        beginTest ("dry comparison compensates plugin latency and invalid inputs remain finite");
        safety.prepare (48000, 1, false, true); out.clear(); dry.clear(); dry.setSample (0, 0, 0.4f); dry.setSample (0, 1, std::numeric_limits<float>::quiet_NaN());
        auto dryCounts = safety.process (out.getArrayOfWritePointers(), 1, dry.getArrayOfReadPointers(), 1, 480, false, true, false, 10);
        expectEquals ((int) dryCounts.invalid, 1); expectEquals (out.getSample (0, 0), 0.0f); expectWithinAbsoluteError (out.getSample (0, 10), 0.4f, 0.00001f);

        beginTest ("preset controls remain accessible alongside add/library/bypass at minimum width");
        PluginListView view (engine); view.setSize (568, 400);
        juce::Rectangle<int> presetBounds, bypassBounds;
        for (auto* child : view.getChildren()) if (auto* button = dynamic_cast<juce::TextButton*> (child))
        { if (button->getButtonText() == "[ PRESETS ]") presetBounds = button->getBounds(); if (button->getButtonText() == "[ BYPASS ]") bypassBounds = button->getBounds(); }
        expect (! presetBounds.isEmpty()); expect (! presetBounds.intersects (bypassBounds)); expect (view.getLocalBounds().contains (presetBounds));
    }
};
static CoreFeatureTests coreFeatureTests;
