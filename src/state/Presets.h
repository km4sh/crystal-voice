#pragma once
#include "state/Persistence.h"

struct ChainPreset
{
    juce::String id, name;
    juce::Array<PluginEntryState> plugins;
};

class PresetStore
{
public:
    explicit PresetStore (juce::File root = settingsDirectory().getChildFile ("Presets")) : folder (root) {}
    juce::Array<ChainPreset> list() const;
    bool save (ChainPreset&, juce::String& error) const;
    bool load (const juce::String& id, ChainPreset&, juce::String& error) const;
    bool exportFile (const ChainPreset&, const juce::File&, juce::String& error) const;
    bool importFile (const juce::File&, ChainPreset&, juce::String& error) const;
    static bool validId (const juce::String&);
private:
    juce::File folder;
};
bool restoreStartupPreset (MicVSTState&, const PresetStore&, juce::String& error);

// Disk I/O never runs in an audio callback. Pending session saves are coalesced.
class SessionWriter : private juce::Thread
{
public:
    explicit SessionWriter (juce::File destination = configFile());
    ~SessionWriter() override;
    void enqueue (MicVSTState);
    void flush();
    juce::String takeError();
private:
    void run() override;
    juce::File file;
    juce::CriticalSection mutex;
    juce::WaitableEvent wake, idle;
    std::unique_ptr<MicVSTState> pending;
    juce::String failure;
    bool writing = false;
};
