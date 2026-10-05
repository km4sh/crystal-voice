#include "audio/AudioEngine.h"
#include "audio/isolation/IsolatedPlugin.h"
#include "audio/PluginLocations.h"
#include "audio/PluginIdentity.h"
#include <stdexcept>
using IOProc = juce::AudioProcessorGraph::AudioGraphIOProcessor;

namespace
{
    juce::String validateActiveChannels (const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                         const juce::AudioIODevice* device)
    {
        if (device == nullptr) return {};
        if (setup.inputDeviceName.isNotEmpty()
            && device->getActiveInputChannels().countNumberOfSetBits() < setup.inputChannels.countNumberOfSetBits())
            return "The selected input channel is unavailable. Choose channel 1 or another microphone.";
        if (setup.outputDeviceName.isNotEmpty() && device->getActiveOutputChannels().isZero())
            return "The selected destination has no active output channels.";
        return {};
    }
}

AudioEngine::AudioEngine (MicVSTDeviceManager::DeviceTypesFactory factory)
    : deviceManager (std::move (factory))
{
    // WICHTIG: erzwingt das Erstellen + Scannen der Geräte-Typen. Ohne diesen Aufruf
    // ist availableDeviceTypes leer, getCurrentDeviceTypeObject() liefert nullptr und
    // setCurrentAudioDeviceType()/setAudioDeviceSetup() sowie die Device-Suche tun nichts.
    deviceManager.getAvailableDeviceTypes();
    deviceManager.addChangeListener (this);
    startTimer (250);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    *alive = false;
    scanner.reset();
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    player.setProcessor (nullptr);
}

bool AudioEngine::isRunning() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    return dev != nullptr && dev->isPlaying();
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! applyingDevice)
    {
        const auto actual = deviceManager.getAudioDeviceSetup();
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        const bool wantsAudio = requestedSetup.inputDeviceName.isNotEmpty() || requestedSetup.outputDeviceName.isNotEmpty();
        const bool available = type != nullptr
            && (requestedSetup.inputDeviceName.isEmpty() || type->getDeviceNames (true).contains (requestedSetup.inputDeviceName))
            && (requestedSetup.outputDeviceName.isEmpty() || type->getDeviceNames (false).contains (requestedSetup.outputDeviceName));
        // JUCE can fall back to default devices after hot-unplug. Preserve the user's route.
        if (wantsAudio && (actual.inputDeviceName != requestedSetup.inputDeviceName
            || actual.outputDeviceName != requestedSetup.outputDeviceName)) deviceManager.closeAudioDevice();
        if (! available) reconnectAttempted = false;
        if (wantsAudio && available && ! isRunning() && ! reconnectAttempted)
        {
            reconnectAttempted = true;
            setDeviceConfig (requestedSetup.inputDeviceName, requestedSetup.outputDeviceName,
                             requestedSetup.sampleRate, requestedSetup.bufferSize);
        }
        else if (wantsAudio && ! available)
        {
            awaitingReconnect = true;
            deviceError = "Waiting for the selected microphone or destination to reconnect.";
        }
        const auto restored = deviceManager.getAudioDeviceSetup();
        if (isRunning() && restored.inputDeviceName == requestedSetup.inputDeviceName
            && restored.outputDeviceName == requestedSetup.outputDeviceName)
        {
            reconnectAttempted = false;
            if (awaitingReconnect) { awaitingReconnect = false; deviceError.clear(); }
        }
    }
    // Device kam/ging: AudioDeviceManager stellt das gespeicherte Setup selbst
    // wieder her (namensbasiert). Wir spiegeln nur den Status nach außen.
    juce::Logger::writeToLog (isRunning() ? "Audio: läuft"
                                          : "Audio: idle (Device getrennt?)");
    if (onStatusChanged) onStatusChanged();
    // A hot-unplug must never overwrite the user's requested route with a fallback.
    if (! applyingDevice && onDeviceChanged) onDeviceChanged();
}

juce::String AudioEngine::detectCableOutput()
{
    // Render-Endpunkte bekannter virtueller Kabel, nach Priorität (VB-Cable zuerst).
    static const char* const cablePatterns[] = {
        "CABLE Input",          // VB-Audio Virtual Cable (empfohlen)
        "VB-Audio",             // weitere VB-Audio-Kabel (Hi-Fi Cable etc.)
        "VoiceMeeter Input",    // VoiceMeeter VAIO/Aux
        "Virtual Audio Cable"   // VAC ("Line 1 (Virtual Audio Cable)")
    };

    // Discovery is read-only: switching types here previously reopened the audio route.
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
        for (auto* candidate : deviceManager.getAvailableDeviceTypes())
            if (candidate->getTypeName() == deviceManager.preferredTypeName()) { type = candidate; break; }
    if (type != nullptr)
    {
        type->scanForDevices();
        auto outs = type->getDeviceNames (false /* output */);
        for (auto* pat : cablePatterns)
            for (auto& name : outs)
                if (name.containsIgnoreCase (pat))
                    return name;
    }
    return {};
}

juce::String AudioEngine::initialise (const juce::String& inputDeviceName,
                                      const juce::String& outputDeviceName, double sampleRate)
{
    const juce::ScopedValueSetter<bool> applying (applyingDevice, true);
    ++deviceGeneration;
    deviceManager.removeAudioCallback (this);   // idempotent: doppelte Registrierung vermeiden
    deviceManager.closeAudioDevice();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    // Namen VERBATIM übernehmen — auch leer. Ein leerer Output-Name bedeutet bewusst
    // "kein Host-Output" ("none"); der Input-WASAPI-Callback treibt die Engine dann allein.
    // Würden wir leer überspringen, bliebe das alte Default-Gerät stehen und "none"
    // ließe sich nicht speichern.
    setup.inputDeviceName  = inputDeviceName;
    setup.outputDeviceName = outputDeviceName;
    if (preferredBufferSize > 0)
        setup.bufferSize = preferredBufferSize;   // 0 = Auto: Geräte-Default nicht anfassen
    // Kanäle EXPLIZIT aktivieren. Auf useDefault* darf man sich nicht verlassen:
    // ohne deviceManager.initialise(numIn,numOut,...) ist numInputChansNeeded=0,
    // wodurch die "Default"-Input-Kanäle auf [0,0) = KEINE gesetzt würden.
    setup.useDefaultInputChannels  = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (inputChannel < 0 ? 0 : inputChannel, inputChannel < 0 ? 2 : 1, true);
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);
    setup.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    requestedSetup = setup;

    // Fehler NICHT früh zurückgeben: Graph/Chain müssen immer existieren,
    // auch wenn (noch) kein Device offen ist (z. B. Gerät noch nicht da / Reconnect).
    juce::String err = openDeviceSetup (setup, deviceManager.preferredTypeName());
    if (err.isNotEmpty()) deviceManager.closeAudioDevice();
    else requestedSetup = deviceManager.getCurrentAudioDevice() != nullptr ? deviceManager.getAudioDeviceSetup() : setup;
    ++deviceGeneration;
    deviceError = err;
    awaitingReconnect = false;

    if (auto* d = deviceManager.getCurrentAudioDevice())
        juce::Logger::writeToLog ("Setup: in=" + setup.inputDeviceName
            + " out=" + setup.outputDeviceName
            + " sr=" + juce::String (d->getCurrentSampleRate(), 0)
            + " buf=" + juce::String (d->getCurrentBufferSizeSamples()));
    else
        juce::Logger::writeToLog ("Setup: kein Device offen (" + err + ")");

    pluginChain.reset();
    graph.clear();
    auto inNode  = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    auto outNode = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));

    pluginChain = std::make_unique<PluginChain> (graph, inNode->nodeID, outNode->nodeID);
    rebuildGraph();   // leere Kette: in -> out (inkl. Mono→Stereo-Fanout)

    // Eigenen "spielenden" Playhead setzen, BEVOR der Player seinen (ohne isPlaying)
    // installiert. Der Player nutzt seinen nur, wenn der Graph keinen hat.
    if (auto* d = deviceManager.getCurrentAudioDevice())
        playHead.sampleRate.store (d->getCurrentSampleRate());
    graph.setPlayHead (&playHead);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (this);
    return err;
}

void AudioEngine::configureChannels (juce::AudioDeviceManager::AudioDeviceSetup& setup)
{
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (inputChannel < 0 ? 0 : inputChannel, inputChannel < 0 ? 2 : 1, true);
    setup.outputChannels.clear(); setup.outputChannels.setRange (0, 2, true);
}

juce::String AudioEngine::setInputChannel (int channel)
{
    const int previous = inputChannel;
    inputChannel = juce::jlimit (-1, 1, channel);
    const auto error = setDeviceConfig (requestedSetup.inputDeviceName, requestedSetup.outputDeviceName, 0, 0);
    if (error.isNotEmpty()) inputChannel = previous;
    return error;
}

juce::String AudioEngine::retryAudioDevice()
{
    reconnectAttempted = true;
    deviceManager.closeAudioDevice();
    return setDeviceConfig (requestedSetup.inputDeviceName, requestedSetup.outputDeviceName,
                            requestedSetup.sampleRate, requestedSetup.bufferSize);
}

void AudioEngine::audioDeviceError (const juce::String& error)
{
    // The driver may report errors off the message thread. Queued notifications
    // must not access a destroyed engine, and never touch UI from the audio thread.
    const auto generation = deviceGeneration.load();
    juce::MessageManager::callAsync ([this, guard = alive, generation, error]
    {
        if (! *guard || generation != deviceGeneration.load()) return;
        awaitingReconnect = false;
        deviceError = error.isNotEmpty() ? error : "The audio device stopped unexpectedly.";
        juce::Logger::writeToLog ("Audio device error: " + deviceError);
        if (onStatusChanged) onStatusChanged();
    });
}

juce::String AudioEngine::openDeviceSetup (const juce::AudioDeviceManager::AudioDeviceSetup& setup,
                                          const juce::String& typeName)
{
    auto error = deviceManager.openDeviceSetup (typeName, setup);
    if (error.isEmpty()) return validateActiveChannels (setup, deviceManager.getCurrentAudioDevice());
    if (typeName != MicVSTDeviceManager::lowLatencyTypeName) return error;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != MicVSTDeviceManager::sharedTypeName) continue;
        // Only change WASAPI mode; never substitute another microphone or output.
        if ((setup.inputDeviceName.isNotEmpty() && ! type->getDeviceNames (true).contains (setup.inputDeviceName))
            || (setup.outputDeviceName.isNotEmpty() && ! type->getDeviceNames (false).contains (setup.outputDeviceName)))
            return error;
        auto sharedError = deviceManager.openDeviceSetup (MicVSTDeviceManager::sharedTypeName, setup);
        if (sharedError.isEmpty()) sharedError = validateActiveChannels (setup, deviceManager.getCurrentAudioDevice());
        if (sharedError.isEmpty()) return {};
        return "Low-latency mode: " + error + "\nShared mode: " + sharedError;
    }
    return error;
}

juce::String AudioEngine::setDeviceConfig (const juce::String& input, const juce::String& output,
                                  double sampleRate, int bufferSize)
{
    const auto previous = deviceManager.getAudioDeviceSetup();
    const auto previousType = deviceManager.getCurrentAudioDeviceType();
    const juce::ScopedValueSetter<bool> applying (applyingDevice, true);
    ++deviceGeneration;
    auto setup = requestedSetup;
    setup.inputDeviceName  = input;
    setup.outputDeviceName = output;
    if (sampleRate > 0.0) setup.sampleRate = sampleRate;
    if (bufferSize > 0)   setup.bufferSize = bufferSize;
    // Kanäle EXPLIZIT (wie in initialise) — sonst droht 0 aktive Input-Kanäle.
    configureChannels (setup);
    auto error = openDeviceSetup (setup, previousType.isNotEmpty() ? previousType : deviceManager.preferredTypeName());
    if (error.isNotEmpty())
    {
        auto rollbackError = deviceManager.openDeviceSetup (previousType, previous);
        if (rollbackError.isEmpty()) rollbackError = validateActiveChannels (previous, deviceManager.getCurrentAudioDevice());
        if (rollbackError.isNotEmpty())
        {
            deviceManager.closeAudioDevice();
            error += "\nPrevious route could not be restored: " + rollbackError;
        }
        ++deviceGeneration;
        deviceError = error;
        if (onStatusChanged) onStatusChanged();
        return error;
    }
    requestedSetup = deviceManager.getCurrentAudioDevice() != nullptr ? deviceManager.getAudioDeviceSetup() : setup;
    ++deviceGeneration;
    deviceError.clear();
    awaitingReconnect = false;
    rebuildGraph();   // IO-Knoten-Kanalzahl kann sich geändert haben -> neu verdrahten
    if (onStatusChanged) onStatusChanged();
    if (onDeviceChanged) onDeviceChanged();
    return {};
}

void AudioEngine::rebuildGraph()
{
    if (pluginChain != nullptr)
        pluginChain->rebuildConnections();
}

juce::File AudioEngine::pluginCacheFile()
{
    return settingsDirectory().getChildFile ("plugin_cache.xml");
}

juce::StringArray AudioEngine::scanRoots() const
{
    auto roots = getDefaultPluginFolders();
    roots.addArray (pluginFolders);
    return pluginLocations::normaliseFolders (roots);
}

juce::StringArray AudioEngine::getDefaultPluginFolders() const
{
   #if JUCE_WINDOWS
    const auto programFiles = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    return pluginLocations::windowsDefaults (programFiles.getFullPathName(),
        juce::SystemStats::getEnvironmentVariable ("CommonProgramFiles", programFiles.getChildFile ("Common Files").getFullPathName()),
        juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getFullPathName(),
        juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getFullPathName(),
        juce::SystemStats::getEnvironmentVariable ("VST3_PATH", {}));
   #else
    MicVST3Format format;
    juce::StringArray paths;
    const auto defaults = format.getDefaultLocationsToSearch();
    for (int i = 0; i < defaults.getNumPaths(); ++i) paths.add (defaults[i].getFullPathName());
    return pluginLocations::normaliseFolders (paths);
   #endif
}

juce::StringArray AudioEngine::listVst3Files() const
{
    MicVST3Format vst3;
    juce::FileSearchPath paths;
    for (auto& r : scanRoots())
        paths.add (juce::File (r));
    paths.removeRedundantPaths();
    return vst3.searchPathsForPlugins (paths, true, true);
}

void AudioEngine::loadPluginCache()
{
    if (! PluginScanCache::load (pluginCacheFile(), knownPlugins, skippedPlugins))
        juce::Logger::writeToLog ("Plugin-Cache fehlt/korrupt -> voller Scan");
}

void AudioEngine::startBackgroundScan (int timeoutMs, const juce::StringArray& forceRescan)
{
    if (isScanning()) { rescanQueued = true; return; }

    MicVST3Format vst3;
    auto files = filterFilesNeedingScan (listVst3Files(), knownPlugins, vst3, skippedPlugins, forceRescan);
    juce::Logger::writeToLog ("Scan: " + juce::String (files.size()) + " Datei(en) zu scannen");
    if (files.isEmpty())
    {
        PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
        retryMissingPlugins();
        if (onScanFinished) onScanFinished();
        return;
    }

    scanner = std::make_unique<ScanCoordinator> (files,
        [this] (int cur, int total, juce::String name)
        {
            if (onScanProgress) onScanProgress (cur, total, name);
        },
        [this] (ScanOutcome outcome) { handleScanFinished (outcome); },
        timeoutMs);
}

void AudioEngine::handleScanFinished (const ScanOutcome& outcome)
{
    scanner = nullptr;   // Callback kommt via callAsync -> wir sind auf dem Message-Thread

    mergeScanResults (knownPlugins, outcome);
    for (auto& s : outcome.skipped)
    {
        skippedPlugins.add (s);
        juce::Logger::writeToLog ("Scan übersprungen (" + s.reason + "): " + s.file);
    }

    // Ordner können während des Scans entfernt worden sein -> NACH dem Übernehmen wegputzen,
    // damit auch frisch gescannte Fremd-Ergebnisse rausfliegen.
    pruneOutsideFolders();

    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);

    retryMissingPlugins();
    if (onScanFinished) onScanFinished();

    if (rescanQueued) { rescanQueued = false; startBackgroundScan(); }
}

void AudioEngine::rescanAllPlugins()
{
    if (isScanning()) return;
    knownPlugins.clear();
    skippedPlugins.clear();
    pluginCacheFile().deleteFile();
    startBackgroundScan();
}

void AudioEngine::retrySkippedPlugins()
{
    if (isScanning() || skippedPlugins.isEmpty()) return;

    // Crash-Rescue: Gerettete Typen sind im Cache schon mit AKTUELLER effectiveModTime
    // gestempelt (mergeScanResults) -> ohne forceRescan hielte filterFilesNeedingScan die
    // Datei für up-to-date und der Retry würde für sie stillschweigend nichts tun. Vor dem
    // Leeren einsammeln (skippedPlugins ist danach weg), nur noch existierende Dateien.
    juce::StringArray forceRescan;
    for (auto& s : skippedPlugins)
        if (juce::File (s.file).exists())
            forceRescan.add (s.file);

    skippedPlugins.clear();   // Cache/Fundliste bleiben -> nur die Geskippten werden gescannt
    startBackgroundScan (ScanCoordinator::retryTimeoutMs, forceRescan);
}

void AudioEngine::skipCurrentScanFile()
{
    if (scanner != nullptr) scanner->skipCurrentFile();
}

void AudioEngine::pruneOutsideFolders()
{
    const auto roots = scanRoots();
    for (auto& t : knownPlugins.getTypes())
        if (! pathIsInsideAnyFolder (t.fileOrIdentifier, roots)) knownPlugins.removeType (t);
    for (int i = skippedPlugins.size(); --i >= 0;)
        if (! pathIsInsideAnyFolder (skippedPlugins[i].file, roots)) skippedPlugins.remove (i);
}

void AudioEngine::addPluginFolder (const juce::String& folder)
{
    if (folder.isNotEmpty() && ! pluginFolders.contains (folder))
        pluginFolders.add (folder);
    startBackgroundScan();
}

void AudioEngine::removePluginFolder (const juce::String& folder)
{
    pluginFolders.removeString (folder);
    pruneOutsideFolders();
    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
    startBackgroundScan();
}

bool AudioEngine::snapshotPluginStates (bool force)
{
    if (pluginChain == nullptr || (! force && ! pluginChain->hasDirtyStates())) return true;
    if (std::none_of (pluginChain->entries().begin(), pluginChain->entries().end(),
                      [] (const PluginChain::Entry& entry) { return entry.observer != nullptr; })) return true;
    const bool hasLocalProcessor = std::any_of (pluginChain->entries().begin(), pluginChain->entries().end(),
        [this] (const PluginChain::Entry& entry)
        {
            auto node = graph.getNodeForId (entry.node);
            return entry.observer != nullptr && node != nullptr && dynamic_cast<IsolatedPlugin*> (node->getProcessor()) == nullptr;
        });
    if (! hasLocalProcessor)
    {
        try { pluginChain->captureStates (force); return true; }
        catch (...) { return false; }
    }
    // Sequentially consistent gate for local processors only: readers that saw false are counted before
    // the message thread starts serialization. New callbacks never wait on a lock.
    takingSnapshot.store (true);
    struct Release { std::atomic<bool>& flag; ~Release() { flag.store (false); } } release { takingSnapshot };
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 100.0;
    while (audioReaders.load() != 0)
    {
        if (juce::Time::getMillisecondCounterHiRes() > deadline) return false;
        juce::Thread::sleep (1);
    }
    try { pluginChain->captureStates (force); }
    catch (...) { return false; }
    return true;
}

void AudioEngine::timerCallback()
{
    if (pluginChain == nullptr) return;
    juce::String health;
    for (const auto& entry : pluginChain->entries())
        if (auto node = graph.getNodeForId (entry.node))
            if (auto* isolated = dynamic_cast<IsolatedPlugin*> (node->getProcessor()))
                health += juce::String (entry.id) + ":" + juce::String (isolated->workerProcessId()) + ":" + juce::String ((int) isolated->failed()) + ";";
    if (health != observedPluginHealth)
    {
        observedPluginHealth = health;
        if (onPluginStatusChanged) onPluginStatusChanged();
        if (onStatusChanged) onStatusChanged();
    }
    const auto revision = pluginChain->stateRevision();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (revision != observedRevision)
    { observedRevision = revision; lastParameterChange = now; presetModified = true; }
    if (pluginChain->hasDirtyStates() && now - lastParameterChange >= 1000.0 && snapshotPluginStates()) requestPersist();
}

int AudioEngine::getFailedPluginCount() const
{
    int count = 0;
    if (pluginChain != nullptr)
        for (const auto& entry : pluginChain->entries())
            if (auto node = graph.getNodeForId (entry.node))
                if (auto* isolated = dynamic_cast<IsolatedPlugin*> (node->getProcessor())) count += isolated->failed() ? 1 : 0;
    return count;
}

uint64_t AudioEngine::getPluginDeadlineMisses() const
{
    uint64_t count = 0;
    if (pluginChain != nullptr)
        for (const auto& entry : pluginChain->entries())
            if (auto node = graph.getNodeForId (entry.node))
                if (auto* isolated = dynamic_cast<IsolatedPlugin*> (node->getProcessor())) count += isolated->missedBlocks();
    return count;
}

MicVSTState AudioEngine::captureState (bool refreshParameters)
{
    if (refreshParameters) snapshotPluginStates (true);
    MicVSTState s;
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup = requestedSetup;
    s.inputDevice  = setup.inputDeviceName;
    s.outputDevice = setup.outputDeviceName;
    s.sampleRate   = setup.sampleRate;
    s.bufferSize   = preferredBufferSize;
    s.inputChannel = inputChannel;
    s.muted = muted.load(); s.bypassed = masterBypass.load();
    s.pluginFolders = pluginFolders;
    s.currentPreset = currentPreset; s.startupPreset = startupPreset;
    s.presetModified = presetModified;

    // Unavailable entries retain their original blobs and position in the chain.
    if (pluginChain == nullptr) return s;

    for (auto& e : pluginChain->entries())
    {
        PluginEntryState p;
        p.fileOrId = e.fileOrId;
        p.identifier = e.identifier;
        p.displayName = e.displayName;
        p.bypassed = e.bypassed;
        p.format = e.format; p.manufacturer = e.manufacturer; p.classUid = e.classUid;
        if (e.isUnavailable()) p.state = e.savedState;
        else if (e.observer != nullptr) p.state = e.observer->cached;
        s.plugins.add (p);
    }
    return s;
}

bool AudioEngine::savePreset (const juce::String& name, bool asNew, juce::String& error)
{
    if (! snapshotPluginStates (true)) { error = "Audio is busy. Try saving again."; return false; }
    ChainPreset preset;
    preset.name = name; preset.id = asNew ? juce::String() : currentPreset;
    preset.plugins = captureState (false).plugins;
    if (! PresetStore().save (preset, error)) return false;
    currentPreset = preset.id; currentPresetName = preset.name;
    presetModified = false; observedRevision = pluginChain->stateRevision();
    if (startupPreset == currentPreset) startupPresetName = preset.name;
    requestPersist(); return true;
}

bool AudioEngine::setStartupPreset (const juce::String& id, juce::String& error)
{
    ChainPreset preset;
    if (id.isNotEmpty() && ! PresetStore().load (id, preset, error)) return false;
    startupPreset = id; startupPresetName = id.isNotEmpty() ? preset.name : juce::String();
    error.clear(); requestPersist(); return true;
}

bool AudioEngine::loadPreset (const ChainPreset& preset, juce::String& error)
{
    if (pluginChain == nullptr) { error = "The audio engine is not ready."; return false; }
    auto staged = std::make_unique<PluginChain> (graph, pluginChain->input(), pluginChain->output());
    auto* device = deviceManager.getCurrentAudioDevice();
    const double rate = device != nullptr ? device->getCurrentSampleRate() : requestedSetup.sampleRate;
    const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 480;
    try
    {
        for (const auto& saved : preset.plugins)
        {
            if (saved.fileOrId == PluginChain::monoToStereoId) staged->addMonoToStereo();
            else if (saved.fileOrId == PluginChain::stereoToMonoId) staged->addStereoToMono();
            else
            {
                auto type = resolvePlugin (saved, knownPlugins);
                if (type == nullptr || ! staged->addPlugin (formatManager, *type, rate, block, error))
                { error = "Could not load " + saved.displayName + ". " + (type == nullptr ? "Plugin missing or ambiguous." : error); return false; }
                const auto& entry = staged->entries().back();
                auto* processor = graph.getNodeForId (entry.node)->getProcessor();
                if (saved.state.getSize() > 0) processor->setStateInformation (saved.state.getData(), (int) saved.state.getSize());
                if (saved.state.getSize() > 0) entry.observer->acceptState (saved.state);
            }
            staged->setBypass ((int) staged->entries().size() - 1, saved.bypassed);
        }
    }
    catch (const std::exception& exception) { error = "Plugin load failed: " + juce::String (exception.what()); return false; }
    catch (...) { error = "Plugin load failed."; return false; }
    if (onChainReplacing) onChainReplacing();
    pluginChain = std::move (staged);
    rebuildGraph();
    currentPreset = preset.id; currentPresetName = preset.name; presetModified = false;
    observedRevision = pluginChain->stateRevision(); recoveryMode = false;
    error.clear(); requestPersist(); if (onStatusChanged) onStatusChanged(); return true;
}

void AudioEngine::applyState (const MicVSTState& s)
{
    restoringChain.store (true);
    struct Ready { std::atomic<bool>& flag; ~Ready() { flag.store (false); } } ready { restoringChain };
    setPreferredBufferSize (s.bufferSize);
    inputChannel = s.inputChannel;
    muted.store (s.muted || recoveryMode); masterBypass.store (s.bypassed);
    currentPreset = s.currentPreset; startupPreset = s.startupPreset;
    presetModified = s.presetModified;
    ChainPreset preset; juce::String error; PresetStore store;
    if (store.load (currentPreset, preset, error)) currentPresetName = preset.name;
    if (store.load (startupPreset, preset, error)) startupPresetName = preset.name;
    initialise (s.inputDevice, s.outputDevice, s.sampleRate);

    // Restore available effects immediately; keep placeholders for missing ones.
    restoreChain (s.plugins);
    if (std::any_of (pluginChain->entries().begin(), pluginChain->entries().end(),
                     [] (const PluginChain::Entry& entry) { return entry.isUnavailable() && ! entry.bypassed; }))
        muted.store (true);
    observedRevision = pluginChain->stateRevision();
    rebuildGraph();
}

void AudioEngine::restoreChain (const juce::Array<PluginEntryState>& plugins)
{
    double sr = deviceManager.getCurrentAudioDevice() != nullptr
              ? deviceManager.getCurrentAudioDevice()->getCurrentSampleRate() : 48000.0;

    for (auto& p : plugins)
    {
        if (p.fileOrId == PluginChain::monoToStereoId || p.fileOrId == PluginChain::stereoToMonoId)
        {
            if (p.fileOrId == PluginChain::monoToStereoId) pluginChain->addMonoToStereo();
            else                                           pluginChain->addStereoToMono();
            pluginChain->setBypass ((int) pluginChain->entries().size() - 1, p.bypassed);
            continue;
        }

        if (recoveryMode)
        { pluginChain->addUnavailable (p, "Safe start after an interrupted session. Review this effect, then use Retry.", true); continue; }
        auto type = resolvePlugin (p, knownPlugins);
        if (type == nullptr) { pluginChain->addUnavailable (p, "Not found. Rescan your plugin folders."); continue; }
        juce::String err;
        auto* device = deviceManager.getCurrentAudioDevice();
        const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 480;
        const int previousSize = (int) pluginChain->entries().size();
        try
        {
            if (! pluginChain->addPlugin (formatManager, *type, sr, block, err)) throw std::runtime_error (err.toStdString());
            const int idx = (int) pluginChain->entries().size() - 1;
            if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
            {
                if (p.state.getSize() > 0) node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
                if (p.state.getSize() > 0 && pluginChain->entries()[(size_t) idx].observer != nullptr)
                    pluginChain->entries()[(size_t) idx].observer->acceptState (p.state);
            }
            pluginChain->setBypass (idx, p.bypassed);
        }
        catch (const std::exception& exception) { err = exception.what(); }
        catch (...) { err = "Plugin state restore failed."; }
        if (err.isNotEmpty())
        {
            if ((int) pluginChain->entries().size() > previousSize) pluginChain->removePlugin (previousSize);
            pluginChain->addUnavailable (p, err, true); juce::Logger::writeToLog ("Plugin-Load: " + err);
        }
    }
    rebuildGraph();
}

void AudioEngine::retryMissingPlugins (bool userInitiated, juce::uint32 onlyEntry)
{
    if (recoveryMode && ! userInitiated) return;
    if (pluginChain == nullptr) return;
    bool restored = false;
    for (int i = 0; i < (int) pluginChain->entries().size(); ++i)
    {
        const auto old = pluginChain->entries()[(size_t) i];
        if (onlyEntry != 0 && old.id != onlyEntry) continue;
        if (old.requiresManualRetry && ! userInitiated) continue;
        auto oldNode = graph.getNodeForId (old.node);
        auto* isolated = oldNode != nullptr ? dynamic_cast<IsolatedPlugin*> (oldNode->getProcessor()) : nullptr;
        if (! old.isUnavailable() && (! userInitiated || isolated == nullptr || ! isolated->failed())) continue;
        PluginEntryState saved;
        saved.fileOrId = old.fileOrId; saved.identifier = old.identifier; saved.displayName = old.displayName;
        saved.format = old.format; saved.manufacturer = old.manufacturer; saved.classUid = old.classUid;
        saved.state = old.isUnavailable() ? old.savedState : isolated->lastGoodState();
        auto type = resolvePlugin (saved, knownPlugins);
        if (type == nullptr) continue;
        auto* device = deviceManager.getCurrentAudioDevice();
        juce::String error;
        const int previousSize = (int) pluginChain->entries().size();
        try
        {
            if (! pluginChain->addPlugin (formatManager, *type,
                device != nullptr ? device->getCurrentSampleRate() : requestedSetup.sampleRate,
                device != nullptr ? device->getCurrentBufferSizeSamples() : 480, error)) continue;
            auto& replacement = pluginChain->entries().back();
            if (saved.state.getSize() > 0)
            {
                graph.getNodeForId (replacement.node)->getProcessor()->setStateInformation (saved.state.getData(), (int) saved.state.getSize());
                replacement.observer->acceptState (saved.state);
            }
        }
        catch (...)
        {
            if ((int) pluginChain->entries().size() > previousSize) pluginChain->removePlugin (previousSize);
            continue;
        }
        pluginChain->replaceWithLast (i);
        restored = true;
    }
    if (recoveryMode && userInitiated)
        recoveryMode = std::any_of (pluginChain->entries().begin(), pluginChain->entries().end(),
                                   [] (const PluginChain::Entry& entry) { return entry.isUnavailable(); });
    if (restored) { rebuildGraph(); requestPersist(); }
    if (userInitiated && onStatusChanged) onStatusChanged();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    // A restarted stream must not inherit queued errors from its predecessor.
    ++deviceGeneration;
    // Geöffnete Geräte-/Kanalkonfiguration ins Log (hilft beim Diagnostizieren von Audio-Problemen).
    juce::Logger::writeToLog ("Device start: '" + device->getName() + "'"
        + " | inCh aktiv=" + juce::String (device->getActiveInputChannels().countNumberOfSetBits())
        + " von [" + device->getInputChannelNames().joinIntoString (", ") + "]"
        + " | outCh aktiv=" + juce::String (device->getActiveOutputChannels().countNumberOfSetBits())
        + " | sr=" + juce::String (device->getCurrentSampleRate(), 0)
        + " | buf=" + juce::String (device->getCurrentBufferSizeSamples()));
    playHead.sampleRate.store (device->getCurrentSampleRate());
    playHead.samples.store (0);
    inputMeter.prepare (device->getCurrentSampleRate());
    outputMeter.prepare (device->getCurrentSampleRate());
    outputSafety.prepare (device->getCurrentSampleRate(), device->getActiveOutputChannels().countNumberOfSetBits(), muted.load(), masterBypass.load());
    cleanInput.setSize (juce::jmax (1, device->getActiveInputChannels().countNumberOfSetBits()),
                        juce::jmax (4096, device->getCurrentBufferSizeSamples() * 2));
    cleanInput.clear();
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
    inputMeter.reset(); outputMeter.reset();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    audioReaders.fetch_add (1);
    const juce::ScopedNoDenormals noDenormals;
    struct ReaderRelease { std::atomic<int>& count; ~ReaderRelease() { count.fetch_sub (1); } } reader { audioReaders };
    const auto started = juce::Time::getHighResolutionTicks();
    const bool snapshot = takingSnapshot.load() || restoringChain.load();
    playHead.samples.fetch_add (numSamples, std::memory_order_relaxed);   // Transport voranschieben

    if (numSamples > cleanInput.getNumSamples() || numInputChannels > cleanInput.getNumChannels())
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr) juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
        ++overruns; return; // Unexpected driver block: do not allocate on the audio thread.
    }
    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        auto* target = cleanInput.getWritePointer (ch);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float value = inputChannelData[ch] != nullptr ? inputChannelData[ch][sample] : 0.0f;
            target[sample] = std::isfinite (value) ? value : 0.0f;
            if (! std::isfinite (value)) ++invalidSamples;
        }
    }
    const auto* const* inputs = cleanInput.getArrayOfReadPointers();

    if (numInputChannels > 0)
    {
        juce::AudioBuffer<float> inView (const_cast<float* const*> (inputs),
                                         numInputChannels, numSamples);
        inputMeter.process (inView);
    }

    // Graph verarbeiten (Input -> Kette -> Output).
    if (snapshot)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] == nullptr) continue;
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
        }
    }
    else
    {
        // One deadline for the entire chain; reserve time for the output guard and metering.
        const double elapsedMs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started) * 1000.0;
        isolation::CallbackBudget budget (juce::jmax (0.0, numSamples * 1000.0 / playHead.sampleRate.load() - elapsedMs - 0.25));
        player.audioDeviceIOCallbackWithContext (inputs, numInputChannels,
                                             outputChannelData, numOutputChannels,
                                             numSamples, context);
    }
    const auto counts = outputSafety.process (outputChannelData, numOutputChannels, inputs, numInputChannels, numSamples,
        muted.load (std::memory_order_relaxed), masterBypass.load (std::memory_order_relaxed), snapshot, graph.getLatencySamples());
    invalidSamples.fetch_add (counts.invalid, std::memory_order_relaxed);
    clippedSamples.fetch_add (counts.clipped, std::memory_order_relaxed);

    if (numOutputChannels > 0)
    {
        juce::AudioBuffer<float> outView (outputChannelData, numOutputChannels, numSamples);
        outputMeter.process (outView);
    }
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - started);
    if (elapsed > numSamples / playHead.sampleRate.load()) ++overruns;
}
