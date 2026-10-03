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
        bool isBuiltIn() const { return fileOrId.startsWith ("builtin:"); }
        bool isUnavailable() const { return node == NodeID{}; }
    };

    PluginChain (juce::AudioProcessorGraph& graph,
                 NodeID inputNode, NodeID outputNode);

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
