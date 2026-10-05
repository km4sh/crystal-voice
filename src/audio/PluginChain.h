#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/GraphConnections.h"
#include "state/Persistence.h"
#include <vector>

// Verwaltet die geordnete VST3-Kette innerhalb eines AudioProcessorGraph.
// Hält pro Plugin einen Node + Bypass-Flag. Baut bei jeder Änderung die
// Verbindungen input->...->output neu auf (via computeChainConnections).
class PluginChain
{
public:
    struct StateObserver : private juce::AudioProcessorListener
    {
        explicit StateObserver (juce::AudioProcessorGraph::Node::Ptr);
        ~StateObserver() override;
        bool dirty() const { return revision.load() != captured; }
        uint64_t generation() const { return revision.load(); }
        void capture(); // Caller must quiesce graph processing first.
        void acceptState (const juce::MemoryBlock& state) { cached = state; captured = revision.load(); }
        juce::MemoryBlock cached;
    private:
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++revision; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
        { if (details.programChanged || details.nonParameterStateChanged || details.parameterInfoChanged) ++revision; }
        juce::AudioProcessorGraph::Node::Ptr node;
        std::atomic<uint64_t> revision { 1 };
        uint64_t captured = 0;
    };
    // Marker im fileOrId-Feld für die internen Glieder (kein VST).
    static constexpr const char* monoToStereoId = "builtin:mono2stereo";
    static constexpr const char* stereoToMonoId = "builtin:stereo2mono";

    struct Entry
    {
        NodeID node;
        juce::String fileOrId;
        bool bypassed = false;
        juce::String identifier, displayName, manufacturer, error;
        juce::MemoryBlock savedState;
        juce::uint32 id = 0;
        juce::String format, classUid;
        std::shared_ptr<StateObserver> observer;
        bool isBuiltIn() const { return fileOrId.startsWith ("builtin:"); }
        bool isUnavailable() const { return node == NodeID{}; }
    };

    PluginChain (juce::AudioProcessorGraph& graph,
                 NodeID inputNode, NodeID outputNode);
    ~PluginChain();
    NodeID input() const { return inputNode; }
    NodeID output() const { return outputNode; }
    bool hasDirtyStates() const;
    uint64_t stateRevision() const;
    void captureStates (bool force);

    // Lädt ein VST3 (synchron) und hängt es ans Ende der Kette. Gibt false bei Fehler.
    bool addPlugin (juce::AudioPluginFormatManager& fm,
                    const juce::PluginDescription& desc,
                    double sampleRate, int blockSize,
                    juce::String& errorOut);

    // Hängt ein internes Glied ans Ende der Kette: Mono->Stereo (1 in -> 2 out, dupliziert)
    // bzw. Stereo->Mono (2 in -> 1 out, gemittelt).
    void addMonoToStereo();
    void addStereoToMono();
    void addUnavailable (const PluginEntryState&, const juce::String& error);
    int indexOf (juce::uint32 id) const;

    void removePlugin (int index);
    void movePlugin (int from, int to);
    void setBypass (int index, bool shouldBypass);

    const std::vector<Entry>& entries() const { return chain; }

    // Connect main buses in chain order. Mono fans out to destination channels;
    // other layouts connect matching channels without auxiliary sidechain inputs.
    void rebuildConnections();

private:
    juce::AudioProcessorGraph& graph;
    NodeID inputNode, outputNode;
    std::vector<Entry> chain;
    juce::uint32 nextId = 1;
};
