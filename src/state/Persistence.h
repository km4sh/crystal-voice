#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>

struct PluginEntryState
{
    juce::String fileOrId;
    juce::String identifier;   // Distinguishes multiple VST3 classes in the same bundle.
    juce::String displayName;
    bool bypassed = false;
    juce::MemoryBlock state;   // getStateInformation()-Blob
    juce::String format, manufacturer, classUid; // Stable across installation paths.
};

struct MicVSTState
{
    juce::String inputDevice, outputDevice;
    double sampleRate = 48000.0;
    int    bufferSize = 0;   // Buffer-Wunsch in Samples; 0 = Auto (Geräte-Default)
    int inputChannel = 0;    // 0/1 = physical mono channel, -1 = stereo.
    bool muted = false, bypassed = false;
    juce::Array<PluginEntryState> plugins;
    juce::StringArray pluginFolders;   // zusätzliche VST3-Suchordner
    juce::String windowState;          // DocumentWindow::getWindowStateAsString() (Größe/Position)
    juce::String currentPreset, startupPreset;
    bool presetModified = false;

    // Opt-in Auto-Update-Check (siehe UpdateChecker). Default: aus, nie gefragt.
    bool updateCheckEnabled = false;   // Checkbox-Zustand
    bool updateCheckAsked   = false;   // Erststart-Popup schon gezeigt?
    juce::String lastNotifiedVersion;  // letzte per Tray-Bubble gemeldete Version (Dedup)
};

juce::ValueTree  toValueTree (const MicVSTState&);
MicVSTState    fromValueTree (const juce::ValueTree&);
bool          decodePluginState (const juce::String&, juce::MemoryBlock&);

// Datei unter %APPDATA%\MicVST\config.xml
juce::File    configFile();
juce::File    settingsDirectory();
void          setSettingsDirectory (const juce::File&);
bool          saveStateToFile (const MicVSTState&, const juce::File&);
MicVSTState   loadStateFromFile (const juce::File&);
bool          saveState (const MicVSTState&);
MicVSTState loadState();
