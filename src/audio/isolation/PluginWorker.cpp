#include "audio/isolation/PluginWorker.h"
#include "ui/Theme.h"
#include <stdexcept>
#include <array>

namespace isolation
{
struct PluginWorker::Connection final : juce::ChildProcessWorker
{
    explicit Connection (PluginWorker& worker) : owner (worker) {}
    PluginWorker& owner;
    void handleConnectionMade() override { owner.handleConnectionMade(); }
    void handleConnectionLost() override { owner.handleConnectionLost(); }
    void handleMessageFromCoordinator (const juce::MemoryBlock& message) override { owner.handleMessageFromCoordinator (message); }
};
struct PluginWorker::AudioThread final : juce::Thread
{
    explicit AudioThread (PluginWorker& worker) : Thread ("Isolated plugin audio"), owner (worker) {}
    PluginWorker& owner;
    void run() override
    {
        AudioPriority priority;
        auto* shared = owner.transport.data();
        std::array<float*, maxChannels> pointers {};
        for (int channel = 0; channel < maxChannels; ++channel) pointers[(size_t) channel] = shared->input[channel];
        int32_t last = 0; juce::MidiBuffer midi;
        while (! threadShouldExit())
        {
            if (! owner.transport.waitForRequest (50) || threadShouldExit()) continue;
            const auto sequence = shared->request.load();
            if (sequence == 0 || sequence == last) continue;
            const int samples = shared->samples, channels = shared->channels;
            if (samples <= 0 || samples > maxSamples || channels <= 0 || channels > maxChannels) continue;
            juce::AudioBuffer<float> buffer (pointers.data(), channels, samples);
            const juce::ScopedNoDenormals noDenormals;
            const juce::ScopedTryLock lock (owner.processor->getCallbackLock());
            if (lock.isLocked() && shared->activity.load() == 0)
            {
                owner.head.samples = shared->samplePosition;
                midi.clear(); owner.processor->processBlock (buffer, midi); shared->rendered.increment();
            }
            else buffer.clear();
            for (int channel = 0; channel < channels; ++channel)
                std::memcpy (shared->output[channel], buffer.getReadPointer (channel), (size_t) samples * sizeof (float));
            last = sequence; owner.transport.completeRequest (sequence);
        }
    }
};
struct PluginWorker::EditorWindow final : juce::DocumentWindow
{
    EditorWindow (PluginWorker& worker, juce::AudioProcessorEditor* body)
        : DocumentWindow (worker.processor->getName(), theme::background, closeButton), owner (worker)
    {
        theme::window (*this); setContentOwned (body, true);
        centreWithSize (getWidth(), getHeight()); setVisible (true); toFront (true);
    }
    void closeButtonPressed() override { setVisible (false); owner.transport.data()->revision.increment(); }
    PluginWorker& owner;
};
juce::Optional<juce::AudioPlayHead::PositionInfo> PluginWorker::PlayingHead::getPosition() const
{
    PositionInfo info; const auto s = samples.load(); const auto r = rate.load();
    info.setIsPlaying (true); info.setIsRecording (false); info.setIsLooping (false);
    info.setTimeInSamples (s); info.setTimeInSeconds ((double) s / r); info.setBpm (120.0);
    info.setTimeSignature (TimeSignature{}); info.setPpqPosition ((double) s / r * 2.0); return info;
}
PluginWorker::PluginWorker (std::function<void()> quit, Factory create)
    : connection (std::make_unique<Connection> (*this)), onQuit (std::move (quit)), factory (std::move (create)) {}
PluginWorker::~PluginWorker()
{
    // Stop IPC callbacks while their owner and command queue still exist.
    connection.reset(); cancelPendingUpdate(); editor.reset();
    if (audio != nullptr) { audio->signalThreadShouldExit(); transport.wakeWorker(); audio->stopThread (2000); audio.reset(); }
    if (processor != nullptr) { processor->removeListener (this); processor->releaseResources(); processor.reset(); }
}
bool PluginWorker::isWorkerCommandLine (const juce::String& commandLine) { return commandLine.trim().startsWith ("--cvplugin:"); }
bool PluginWorker::connect (const juce::String& commandLine) { return connection->initialiseFromCommandLine (commandLine, workerId, 5000); }
void PluginWorker::sendMessageToCoordinator (const juce::MemoryBlock& message) { connection->sendMessageToCoordinator (message); }
void PluginWorker::handleConnectionMade()
{
    juce::MemoryOutputStream hello; hello.writeInt ((int) AudioTransport::currentProcessId());
    sendMessageToCoordinator (pack (Command::hello, 0, hello.getMemoryBlock()));
}
void PluginWorker::handleConnectionLost() { disconnected = true; triggerAsyncUpdate(); }
void PluginWorker::handleMessageFromCoordinator (const juce::MemoryBlock& bytes)
{
    Packet packet {};
    if (! unpack (bytes, packet)) { disconnected = true; triggerAsyncUpdate(); return; }
    { const juce::ScopedLock lock (queueLock); if (commands.size() >= 16) { disconnected = true; } else commands.push_back (std::move (packet)); }
    triggerAsyncUpdate();
}
void PluginWorker::handleAsyncUpdate()
{
    if (disconnected.load()) { if (onQuit) onQuit(); return; }
    for (;;)
    {
        Packet packet {};
        { const juce::ScopedLock lock (queueLock); if (commands.empty()) return; packet = std::move (commands.front()); commands.pop_front(); }
        try { execute (packet); }
        catch (const std::exception& error) { if (auto* shared = transport.data()) shared->activity.store (0); reply (packet, textPayload (error.what()), false); }
        catch (...) { if (auto* shared = transport.data()) shared->activity.store (0); reply (packet, textPayload ("Plugin control operation failed."), false); }
    }
}
void PluginWorker::reply (const Packet& request, const juce::MemoryBlock& payload, bool success)
{ sendMessageToCoordinator (pack (request.command, request.id, payload, success)); }
void PluginWorker::prepare (double rate, int block)
{
    if (! std::isfinite (rate) || rate < 8000 || rate > 384000 || block <= 0 || block > maxSamples) throw std::runtime_error ("Unsupported audio configuration.");
    auto* shared = transport.data(); shared->activity.store (1);
    const juce::ScopedLock lock (processor->getCallbackLock());
    processor->releaseResources(); processor->setRateAndBufferSizeDetails (rate, block); head.rate = rate;
    processor->prepareToPlay (rate, block); shared->latency.store (processor->getLatencySamples()); shared->activity.store (0);
}
void PluginWorker::execute (const Packet& request)
{
    if (request.command == Command::load)
    {
        if (processor != nullptr) throw std::runtime_error ("This worker already owns a plugin instance.");
        auto xml = juce::parseXML (payloadText (request.payload));
        if (xml == nullptr || ! xml->hasTagName ("IsolatedPlugin") || xml->getIntAttribute ("version") != (int) protocolVersion) throw std::runtime_error ("Invalid plugin load request.");
        juce::PluginDescription description;
        if (xml->getFirstChildElement() == nullptr || ! description.loadFromXml (*xml->getFirstChildElement())) throw std::runtime_error ("Invalid plugin description.");
        juce::String error;
        if (! transport.open (xml->getStringAttribute ("transport"), error)) throw std::runtime_error (error.toStdString());
        const auto rate = xml->getDoubleAttribute ("rate"); const int block = xml->getIntAttribute ("block");
        if (factory) processor = factory (description, rate, block, error);
        else { juce::addDefaultFormatsToManager (formats); processor = formats.createPluginInstance (description, rate, block, error); }
        if (processor == nullptr) throw std::runtime_error (error.toStdString());
        for (const bool input : { true, false })
            for (int bus = 0; bus < processor->getBusCount (input); ++bus)
                if (auto* b = processor->getBus (input, bus))
                { if (bus > 0) b->enable (false); else if (! b->isEnabled()) b->enable (true); }
        if (processor->getTotalNumInputChannels() != processor->getMainBusNumInputChannels()
            || processor->getTotalNumOutputChannels() != processor->getMainBusNumOutputChannels()) throw std::runtime_error ("Mandatory auxiliary buses are unsupported for the microphone route.");
        if (processor->getMainBusNumInputChannels() > maxChannels || processor->getMainBusNumOutputChannels() <= 0
            || processor->getMainBusNumOutputChannels() > maxChannels) throw std::runtime_error ("Unsupported plugin channel count.");
        processor->setPlayHead (&head); processor->addListener (this); prepare (rate, block);
        juce::MemoryBlock state; processor->getStateInformation (state);
        if (state.getSize() > maxStateBytes) throw std::runtime_error ("Plugin state exceeds the isolation limit.");
        juce::MemoryOutputStream response;
        response.writeInt (processor->getMainBusNumInputChannels()); response.writeInt (processor->getMainBusNumOutputChannels());
        response.writeInt (processor->getLatencySamples()); response.writeInt ((int) state.getSize()); response.write (state.getData(), state.getSize());
        audio = std::make_unique<AudioThread> (*this); audio->startThread (juce::Thread::Priority::highest);
        reply (request, response.getMemoryBlock()); return;
    }
    if (processor == nullptr) throw std::runtime_error ("Plugin is not loaded.");
    if (request.command == Command::prepare)
    {
        if (request.payload.getSize() != 12) throw std::runtime_error ("Invalid preparation request.");
        juce::MemoryInputStream input (request.payload, false); const auto rate = input.readDouble(); const auto block = input.readInt(); prepare (rate, block); reply (request, {}); return;
    }
    if (request.command == Command::getState || request.command == Command::setState)
    {
        auto* shared = transport.data(); shared->activity.store (1);
        struct Resume { SharedAudio* shared; ~Resume() { shared->activity.store (0); } } resume { shared };
        const juce::ScopedLock lock (processor->getCallbackLock());
        if (request.command == Command::setState)
        { processor->setStateInformation (request.payload.getData(), (int) request.payload.getSize()); shared->revision.increment(); reply (request, {}); }
        else
        { juce::MemoryBlock state; processor->getStateInformation (state); if (state.getSize() > maxStateBytes) throw std::runtime_error ("Plugin state exceeds the isolation limit."); reply (request, state); }
        return;
    }
    if (request.command == Command::showEditor)
    {
        if (editor == nullptr)
        {
            auto* body = processor->hasEditor() ? processor->createEditorAndMakeActive() : new juce::GenericAudioProcessorEditor (*processor);
            if (body == nullptr) throw std::runtime_error ("Plugin editor could not be created.");
            editor = std::make_unique<EditorWindow> (*this, body);
        }
        else { editor->setVisible (true); editor->toFront (true); }
        reply (request, {}); return;
    }
    if (request.command == Command::hideEditor) { if (editor != nullptr) editor->setVisible (false); reply (request, {}); return; }
    throw std::runtime_error ("Unknown plugin control command.");
}
void PluginWorker::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{ if (auto* shared = transport.data()) shared->revision.increment(); }
void PluginWorker::audioProcessorChanged (juce::AudioProcessor* plugin, const ChangeDetails& details)
{
    if (auto* shared = transport.data())
    {
        shared->latency.store (plugin->getLatencySamples());
        if (details.programChanged || details.nonParameterStateChanged || details.parameterInfoChanged) shared->revision.increment();
    }
}
}
