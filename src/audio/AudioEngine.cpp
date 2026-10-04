#include "audio/AudioEngine.h"
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
}

AudioEngine::~AudioEngine()
{
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
    // JUCE-Default-Orte statt hartkodiertem Pfad: deckt neben Program Files auch
    // %LOCALAPPDATA%\Programs\Common\VST3 und die VST3_PATH-Umgebungsvariable ab.
    MicVST3Format vst3;
    juce::StringArray roots;
    const auto defaults = vst3.getDefaultLocationsToSearch();
    for (int i = 0; i < defaults.getNumPaths(); ++i)
        roots.add (defaults[i].getFullPathName());
    for (auto& f : pluginFolders)
        if (f.isNotEmpty()) roots.add (f);
    return roots;
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

MicVSTState AudioEngine::captureState()
{
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

    // Unavailable entries retain their original blobs and position in the chain.
    if (pluginChain == nullptr) return s;

    for (auto& e : pluginChain->entries())
    {
        PluginEntryState p;
        p.fileOrId = e.fileOrId;
        p.identifier = e.identifier;
        p.displayName = e.displayName;
        p.bypassed = e.bypassed;
        if (e.isUnavailable()) p.state = e.savedState;
        if (auto* node = graph.getNodeForId (e.node))
        {
            auto* proc = node->getProcessor();
            const juce::ScopedLock sl (proc->getCallbackLock());   // gegen Race mit processBlock
            proc->getStateInformation (p.state);
        }
        s.plugins.add (p);
    }
    return s;
}

void AudioEngine::applyState (const MicVSTState& s)
{
    setPreferredBufferSize (s.bufferSize);
    inputChannel = s.inputChannel;
    muted.store (s.muted); masterBypass.store (s.bypassed);
    initialise (s.inputDevice, s.outputDevice, s.sampleRate);

    // Restore available effects immediately; keep placeholders for missing ones.
    restoreChain (s.plugins);
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

        auto type = p.identifier.isNotEmpty() ? knownPlugins.getTypeForIdentifierString (p.identifier)
                                             : knownPlugins.getTypeForFile (p.fileOrId);
        if (type == nullptr) { pluginChain->addUnavailable (p, "Not found. Rescan your plugin folders."); continue; }
        juce::String err;
        auto* device = deviceManager.getCurrentAudioDevice();
        const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 480;
        if (pluginChain->addPlugin (formatManager, *type, sr, block, err))
        {
            const int idx = (int) pluginChain->entries().size() - 1;
            if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
            {
                const juce::ScopedLock lock (node->getProcessor()->getCallbackLock());
                node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
            }
            pluginChain->setBypass (idx, p.bypassed);
        }
        else { pluginChain->addUnavailable (p, err); juce::Logger::writeToLog ("Plugin-Load: " + err); }
    }
    rebuildGraph();
}

void AudioEngine::retryMissingPlugins()
{
    if (pluginChain == nullptr) return;
    bool restored = false;
    for (int i = 0; i < (int) pluginChain->entries().size(); ++i)
    {
        const auto old = pluginChain->entries()[(size_t) i];
        if (! old.isUnavailable()) continue;
        auto type = old.identifier.isNotEmpty() ? knownPlugins.getTypeForIdentifierString (old.identifier)
                                               : knownPlugins.getTypeForFile (old.fileOrId);
        if (type == nullptr) continue;
        auto* device = deviceManager.getCurrentAudioDevice();
        juce::String error;
        if (! pluginChain->addPlugin (formatManager, *type,
            device != nullptr ? device->getCurrentSampleRate() : requestedSetup.sampleRate,
            device != nullptr ? device->getCurrentBufferSizeSamples() : 480, error)) continue;
        int last = (int) pluginChain->entries().size() - 1;
        auto node = graph.getNodeForId (pluginChain->entries()[(size_t) last].node);
        if (node != nullptr && old.savedState.getSize() > 0)
        { const juce::ScopedLock lock (node->getProcessor()->getCallbackLock());
          node->getProcessor()->setStateInformation (old.savedState.getData(), (int) old.savedState.getSize()); }
        pluginChain->setBypass (last, old.bypassed);
        pluginChain->removePlugin (i);
        pluginChain->movePlugin (last - 1, i);
        restored = true;
    }
    if (restored) { rebuildGraph(); requestPersist(); }
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
    playHead.samples.fetch_add (numSamples, std::memory_order_relaxed);   // Transport voranschieben

    if (numInputChannels > 0)
    {
        juce::AudioBuffer<float> inView (const_cast<float* const*> (inputChannelData),
                                         numInputChannels, numSamples);
        inputMeter.process (inView);
    }

    // Graph verarbeiten (Input -> Kette -> Output).
    if (masterBypass.load (std::memory_order_relaxed))
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            const auto* source = numInputChannels > 0 ? inputChannelData[channel % numInputChannels] : nullptr;
            if (outputChannelData[channel] == nullptr) continue;
            if (source != nullptr) juce::FloatVectorOperations::copy (outputChannelData[channel], source, numSamples);
            else juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
        }
    }
    else player.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                             outputChannelData, numOutputChannels,
                                             numSamples, context);
    if (muted.load (std::memory_order_relaxed))
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numOutputChannels > 0)
    {
        juce::AudioBuffer<float> outView (outputChannelData, numOutputChannels, numSamples);
        outputMeter.process (outView);
    }
}
