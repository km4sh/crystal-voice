#include "state/Presets.h"

bool PresetStore::validId (const juce::String& id)
{
    return id.length() == 32 && id.containsOnly ("0123456789abcdef");
}

bool PresetStore::exportFile (const ChainPreset& preset, const juce::File& file, juce::String& error) const
{
    if (preset.name.trim().isEmpty() || preset.name.length() > 80 || ! validId (preset.id))
    { error = "Enter a preset name between 1 and 80 characters."; return false; }
    MicVSTState state; state.plugins = preset.plugins;
    juce::XmlElement root ("CrystalVoicePreset");
    root.setAttribute ("version", 1); root.setAttribute ("id", preset.id); root.setAttribute ("name", preset.name);
    root.addChildElement (toValueTree (state).getChildWithName ("plugins").createXml().release());
    if (! file.getParentDirectory().createDirectory()) { error = "Cannot create the preset folder."; return false; }
    juce::TemporaryFile temporary (file);
    if (! root.writeTo (temporary.getFile()))
    { error = "Could not save the preset. Check folder permissions and free disk space."; return false; }
    if (temporary.getFile().getSize() > 64 * 1024 * 1024)
    { error = "The preset exceeds the 64 MiB file limit."; return false; }
    if (! temporary.overwriteTargetFileWithTemporary())
    { error = "Could not save the preset. Check folder permissions and free disk space."; return false; }
    error.clear(); return true;
}

bool PresetStore::importFile (const juce::File& file, ChainPreset& result, juce::String& error) const
{
    if (! file.existsAsFile() || file.getSize() > 64 * 1024 * 1024)
    { error = "The preset is missing or exceeds 64 MiB."; return false; }
    auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("CrystalVoicePreset") || xml->getIntAttribute ("version") != 1
        || ! validId (xml->getStringAttribute ("id")) || xml->getStringAttribute ("name").trim().isEmpty()
        || xml->getStringAttribute ("name").length() > 80 || xml->getChildByName ("plugins") == nullptr)
    { error = "This is not a supported Crystal Voice chain preset."; return false; }
    for (const auto* plugin : xml->getChildByName ("plugins")->getChildIterator())
    {
        juce::MemoryBlock decoded;
        if (! plugin->hasTagName ("plugin") || plugin->getStringAttribute ("fileOrId").isEmpty()
            || plugin->getStringAttribute ("state").isEmpty()
            || ! decodePluginState (plugin->getStringAttribute ("state"), decoded))
        { error = "The preset contains invalid plugin state data."; return false; }
    }
    juce::XmlElement stateXml ("MicVST"); stateXml.addChildElement (new juce::XmlElement (*xml->getChildByName ("plugins")));
    ChainPreset parsed;
    parsed.id = xml->getStringAttribute ("id"); parsed.name = xml->getStringAttribute ("name").trim();
    parsed.plugins = fromValueTree (juce::ValueTree::fromXml (stateXml)).plugins;
    result = std::move (parsed); error.clear(); return true;
}

bool PresetStore::save (ChainPreset& preset, juce::String& error) const
{
    preset.name = preset.name.trim();
    if (preset.id.isEmpty()) preset.id = juce::Uuid().toString().removeCharacters ("-");
    if (! validId (preset.id)) { error = "Invalid preset identifier."; return false; }
    return exportFile (preset, folder.getChildFile (preset.id + ".cvpreset"), error);
}

bool PresetStore::load (const juce::String& id, ChainPreset& preset, juce::String& error) const
{
    if (! validId (id)) { error = "Invalid preset identifier."; return false; }
    ChainPreset parsed;
    if (! importFile (folder.getChildFile (id + ".cvpreset"), parsed, error)) return false;
    if (parsed.id != id) { error = "The preset identifier does not match its file."; return false; }
    preset = std::move (parsed);
    return true;
}

juce::Array<ChainPreset> PresetStore::list() const
{
    juce::Array<ChainPreset> result;
    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, "*.cvpreset"))
    {
        ChainPreset preset; juce::String error;
        if (importFile (file, preset, error) && file.getFileNameWithoutExtension() == preset.id) result.add (preset);
    }
    std::sort (result.begin(), result.end(), [] (const ChainPreset& a, const ChainPreset& b)
    { return a.name.compareNatural (b.name) < 0; });
    return result;
}

SessionWriter::SessionWriter (juce::File destination) : Thread ("SessionSave"), file (destination) { startThread(); }
bool restoreStartupPreset (MicVSTState& state, const PresetStore& store, juce::String& error)
{
    if (state.startupPreset.isEmpty()) { error.clear(); return true; }
    ChainPreset preset;
    if (! store.load (state.startupPreset, preset, error)) return false;
    state.plugins = preset.plugins; state.currentPreset = preset.id; state.presetModified = false;
    error.clear(); return true;
}
SessionWriter::~SessionWriter() { flush(); signalThreadShouldExit(); wake.signal(); stopThread (-1); }
void SessionWriter::enqueue (MicVSTState state)
{
    { const juce::ScopedLock lock (mutex); pending = std::make_unique<MicVSTState> (std::move (state)); }
    wake.signal();
}
void SessionWriter::flush()
{
    for (;;)
    {
        { const juce::ScopedLock lock (mutex); if (pending == nullptr && ! writing) return; }
        idle.wait (20);
    }
}
juce::String SessionWriter::takeError() { const juce::ScopedLock lock (mutex); return std::exchange (failure, {}); }
void SessionWriter::run()
{
    while (! threadShouldExit())
    {
        wake.wait (250);
        std::unique_ptr<MicVSTState> next;
        { const juce::ScopedLock lock (mutex); next = std::move (pending); writing = next != nullptr; }
        if (next != nullptr)
        {
            const bool ok = saveStateToFile (*next, file);
            const juce::ScopedLock lock (mutex);
            if (! ok) failure = "Could not save the session. Check folder permissions and free disk space.";
            writing = false;
        }
        idle.signal();
    }
}
