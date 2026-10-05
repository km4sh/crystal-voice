#include "audio/isolation/IsolatedPlugin.h"
#include "audio/isolation/Protocol.h"
#include <stdexcept>
#include <cmath>

using isolation::Command;
struct IsolatedPlugin::Bridge final : juce::ChildProcessCoordinator
{
    enum Failure { healthy, crashed, hung, controlTimeout, invalidAudio, controlError };
    isolation::AudioTransport transport;
    std::atomic<int> fault { healthy };
    std::atomic<uint32_t> pid { 0 };
    std::atomic<bool> closing { false };
    juce::WaitableEvent hello, reply;
    juce::CriticalSection responseLock, commandLock;
    isolation::Packet response {};
    int64_t nextId = 0;
    int inputs = 0, outputs = 0, block = 480;
    double rate = 48000;
    juce::MemoryBlock cached;

    ~Bridge() override
    {
        closing = true; transport.terminateWorker(); killWorkerProcess();
    }
    void fail (Failure reason) noexcept { int expected = healthy; fault.compare_exchange_strong (expected, reason); }
    void handleConnectionLost() override { if (! closing.load()) fail (crashed); hello.signal(); reply.signal(); }
    void handleMessageFromWorker (const juce::MemoryBlock& bytes) override
    {
        isolation::Packet packet {};
        if (! isolation::unpack (bytes, packet)) { fail (controlError); reply.signal(); return; }
        if (packet.command == Command::hello && packet.id == 0 && packet.payload.getSize() == 4)
        { juce::MemoryInputStream input (packet.payload, false); pid = (uint32_t) input.readInt(); hello.signal(); return; }
        { const juce::ScopedLock lock (responseLock); response = std::move (packet); }
        reply.signal();
    }
    bool request (Command command, const juce::MemoryBlock& payload, juce::MemoryBlock& result, juce::String& error, int timeout = 2000)
    {
        const juce::ScopedLock serial (commandLock);
        if (fault.load() != healthy) { error = reason(); return false; }
        const int64_t id = ++nextId;
        reply.reset();
        if (! sendMessageToWorker (isolation::pack (command, id, payload)))
        { fail (crashed); error = reason(); return false; }
        const auto end = juce::Time::getMillisecondCounterHiRes() + timeout;
        for (;;)
        {
            {
                const juce::ScopedLock lock (responseLock);
                if (response.id == id && response.command == command)
                {
                    if (! response.success)
                    {
                        error = isolation::payloadText (response.payload);
                        if (command != Command::showEditor && command != Command::hideEditor) fail (controlError);
                        return false;
                    }
                    result = response.payload; error.clear(); return true;
                }
            }
            if (fault.load() != healthy) { error = reason(); return false; }
            const int remaining = (int) (end - juce::Time::getMillisecondCounterHiRes());
            if (remaining <= 0) { fail (controlTimeout); error = reason(); return false; }
            reply.wait (remaining);
        }
    }
    bool initialise (const juce::PluginDescription& description, double sampleRate, int samples, juce::String& error)
    {
        if (! transport.create (error)) return false;
        if (! launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), isolation::workerId, 5000, 0))
        { error = "Could not start the isolated plugin process."; return false; }
        if (! hello.wait (5000) || pid.load() == 0 || fault.load() != healthy)
        { error = "The isolated plugin process did not connect."; return false; }
        if (! transport.attachWorker (pid.load(), error)) return false;
        juce::XmlElement load ("IsolatedPlugin"); load.setAttribute ("version", (int) isolation::protocolVersion);
        load.setAttribute ("transport", transport.name()); load.setAttribute ("rate", sampleRate); load.setAttribute ("block", samples);
        load.addChildElement (description.createXml().release());
        juce::MemoryBlock result;
        if (! request (Command::load, isolation::textPayload (load.toString()), result, error, 15000)) return false;
        if (result.getSize() < 16) { error = "Invalid isolated plugin description."; return false; }
        juce::MemoryInputStream input (result, false);
        inputs = input.readInt(); outputs = input.readInt(); const int latency = input.readInt(), stateSize = input.readInt();
        if (inputs < 0 || inputs > isolation::maxChannels || outputs <= 0 || outputs > isolation::maxChannels
            || stateSize < 0 || (size_t) stateSize > isolation::maxStateBytes || result.getSize() != (size_t) stateSize + 16)
        { error = "Unsupported isolated plugin channels or state size."; return false; }
        cached.replaceAll (static_cast<const char*> (result.getData()) + 16, (size_t) stateSize);
        rate = sampleRate; block = samples; transport.data()->latency.store (juce::jmax (0, latency));
        error.clear(); return true;
    }
    juce::String reason() const
    {
        switch (fault.load())
        {
            case crashed: return "Plugin process exited. Other effects remain isolated. Reload to restore its saved settings.";
            case hung: return "Plugin audio stopped responding. Reload to restore its saved settings.";
            case controlTimeout: return "Plugin control request timed out. Reload to restore its saved settings.";
            case invalidAudio: return "Unsupported or invalid isolated audio block.";
            case controlError: return "Invalid plugin process response.";
            default: return {};
        }
    }
};

std::unique_ptr<IsolatedPlugin> IsolatedPlugin::create (const juce::PluginDescription& description, double rate, int block, juce::String& error)
{
    auto bridge = std::make_unique<Bridge>();
    if (! bridge->initialise (description, rate, block, error)) return {};
    return std::unique_ptr<IsolatedPlugin> (new IsolatedPlugin (description, std::move (bridge)));
}
IsolatedPlugin::IsolatedPlugin (const juce::PluginDescription& desc, std::unique_ptr<Bridge> connection)
    : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::canonicalChannelSet (connection->inputs), connection->inputs > 0)
                                           .withOutput ("Out", juce::AudioChannelSet::canonicalChannelSet (connection->outputs), true)),
      bridge (std::move (connection)), description (desc)
{
    preparedRate = bridge->rate; preparedBlock = bridge->block;
    setLatencySamples (bridge->transport.data()->latency.load()); remoteRevision = bridge->transport.data()->revision.load();
    activityGain.reset (preparedRate, 0.005); activityGain.setCurrentAndTargetValue (1.0f); startTimer (100);
}
IsolatedPlugin::~IsolatedPlugin() { stopTimer(); bridge.reset(); }
bool IsolatedPlugin::failed() const { return bridge->fault.load() != Bridge::healthy; }
juce::String IsolatedPlugin::failureReason() const { return bridge->reason(); }
uint32_t IsolatedPlugin::workerProcessId() const { return bridge->pid.load(); }
int IsolatedPlugin::renderedBlocks() const { return bridge->transport.data()->rendered.load(); }
const juce::MemoryBlock& IsolatedPlugin::lastGoodState() const { return bridge->cached; }
void IsolatedPlugin::fillInPluginDescription (juce::PluginDescription& desc) const { desc = description; }
const juce::String IsolatedPlugin::getName() const { return description.name; }
void IsolatedPlugin::prepareToPlay (double rate, int block)
{
    activityGain.reset (rate, 0.005); activityGain.setCurrentAndTargetValue (failed() ? 0.0f : 1.0f); lastSample.fill (0.0f);
    if (rate == preparedRate && block == preparedBlock) return;
    juce::MemoryOutputStream payload; payload.writeDouble (rate); payload.writeInt (block);
    juce::MemoryBlock result; juce::String error;
    if (! bridge->request (Command::prepare, payload.getMemoryBlock(), result, error)) { bridge->fail (Bridge::controlError); return; }
    preparedRate = rate; preparedBlock = block;
}
void IsolatedPlugin::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const juce::ScopedNoDenormals noDenormals;
    auto outcome = isolation::AudioTransport::AudioResult::dead;
    if (! failed())
    {
        int64_t position = 0;
        if (auto* head = getPlayHead()) if (auto info = head->getPosition()) position = info->getTimeInSamples().orFallback (0);
        outcome = bridge->transport.exchange (buffer, getMainBusNumInputChannels(), position,
            isolation::CallbackBudget::deadline (buffer.getNumSamples() * 1000.0 / preparedRate));
        if (outcome == isolation::AudioTransport::AudioResult::dead) bridge->fail (Bridge::crashed);
        if (outcome == isolation::AudioTransport::AudioResult::invalid) bridge->fail (Bridge::invalidAudio);
        if (outcome == isolation::AudioTransport::AudioResult::pending)
        { ++deadlineMisses; if (bridge->transport.pendingAge() > 250.0) bridge->fail (Bridge::hung); }
    }
    const bool ready = outcome == isolation::AudioTransport::AudioResult::complete;
    activityGain.setTargetValue (ready ? 1.0f : 0.0f);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float gain = activityGain.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);
            float value = ready ? samples[sample] : (channel < isolation::maxChannels ? lastSample[(size_t) channel] : 0.0f);
            if (! std::isfinite (value)) value = 0.0f;
            if (ready && channel < isolation::maxChannels) lastSample[(size_t) channel] = juce::jlimit (-1.0f, 1.0f, value);
            samples[sample] = value * gain;
        }
    }
}
void IsolatedPlugin::getStateInformation (juce::MemoryBlock& state)
{
    juce::MemoryBlock result; juce::String error;
    const auto before = bridge->transport.data()->revision.load();
    if (! failed() && bridge->request (Command::getState, {}, result, error))
    { bridge->cached = std::move (result); remoteRevision = before; }
    state = bridge->cached; // Crashed/hung effects retain the last successful snapshot.
}
void IsolatedPlugin::setStateInformation (const void* data, int bytes)
{
    if (bytes < 0 || (size_t) bytes > isolation::maxStateBytes) throw std::runtime_error ("Plugin state exceeds the isolation limit.");
    juce::MemoryBlock result; juce::String error;
    juce::MemoryBlock state (data, (size_t) bytes);
    if (! bridge->request (Command::setState, state, result, error)) throw std::runtime_error (error.toStdString());
    bridge->cached = std::move (state); remoteRevision = bridge->transport.data()->revision.load();
}
bool IsolatedPlugin::showRemoteEditor (juce::String& error)
{ juce::MemoryBlock result; return bridge->request (Command::showEditor, {}, result, error); }
void IsolatedPlugin::hideRemoteEditor()
{ juce::MemoryBlock result; juce::String error; if (! failed()) bridge->request (Command::hideEditor, {}, result, error); }
void IsolatedPlugin::timerCallback()
{
    if (bridge->transport.workerExited()) bridge->fail (Bridge::crashed);
    if (bridge->transport.pendingAge() > 250.0) bridge->fail (Bridge::hung);
    if (failed()) { bridge->transport.terminateWorker(); return; }
    const int revision = bridge->transport.data()->revision.load(), latency = bridge->transport.data()->latency.load();
    if (latency != getLatencySamples()) setLatencySamples (juce::jmax (0, latency));
    if (revision != remoteRevision)
    { remoteRevision = revision; juce::AudioProcessorListener::ChangeDetails details; details.nonParameterStateChanged = true; updateHostDisplay (details); }
}
