#include "state/Persistence.h"
#include <cmath>

namespace { juce::File settingsOverride; }

namespace ids
{
    const juce::Identifier root ("MicVST"), inDev ("inputDevice"), outDev ("outputDevice"),
        sr ("sampleRate"), userBuf ("userBufferSize"), folders ("pluginFolders"), window ("windowState"),
        updEnabled ("updateCheckEnabled"), updAsked ("updateCheckAsked"), updLast ("lastNotifiedVersion"),
        plugins ("plugins"), plugin ("plugin"), fileId ("fileOrId"), byp ("bypassed"), blob ("state"),
        identifier ("identifier"), name ("displayName"), inputChannel ("inputChannel"),
        mute ("muted"), masterBypass ("masterBypass");
}

juce::ValueTree toValueTree (const MicVSTState& s)
{
    juce::ValueTree t (ids::root);
    t.setProperty (ids::inDev, s.inputDevice, nullptr);
    t.setProperty (ids::outDev, s.outputDevice, nullptr);
    t.setProperty (ids::sr, s.sampleRate, nullptr);
    t.setProperty (ids::userBuf, s.bufferSize, nullptr);
    t.setProperty (ids::inputChannel, s.inputChannel, nullptr);
    t.setProperty (ids::mute, s.muted, nullptr);
    t.setProperty (ids::masterBypass, s.bypassed, nullptr);
    t.setProperty (ids::folders, s.pluginFolders.joinIntoString ("\n"), nullptr);
    t.setProperty (ids::window, s.windowState, nullptr);
    t.setProperty (ids::updEnabled, s.updateCheckEnabled, nullptr);
    t.setProperty (ids::updAsked, s.updateCheckAsked, nullptr);
    t.setProperty (ids::updLast, s.lastNotifiedVersion, nullptr);

    juce::ValueTree list (ids::plugins);
    for (auto& p : s.plugins)
    {
        juce::ValueTree pt (ids::plugin);
        pt.setProperty (ids::fileId, p.fileOrId, nullptr);
        pt.setProperty (ids::identifier, p.identifier, nullptr);
        pt.setProperty (ids::name, p.displayName, nullptr);
        pt.setProperty (ids::byp, p.bypassed, nullptr);
        pt.setProperty (ids::blob, p.state.toBase64Encoding(), nullptr);
        list.appendChild (pt, nullptr);
    }
    t.appendChild (list, nullptr);
    return t;
}

MicVSTState fromValueTree (const juce::ValueTree& t)
{
    MicVSTState s;
    if (! t.hasType (ids::root)) return s;
    s.inputDevice  = t.getProperty (ids::inDev);
    s.outputDevice = t.getProperty (ids::outDev);
    s.sampleRate   = t.getProperty (ids::sr, 48000.0);
    if (! std::isfinite (s.sampleRate) || s.sampleRate < 8000.0 || s.sampleRate > 384000.0)
        s.sampleRate = 48000.0;
    // Migration v1.0.x: der alte Key "bufferSize" (immer 128) wird bewusst ignoriert --
    // im Shared-Modus war er nie wirksam. Bestandsnutzer starten mit Auto.
    s.bufferSize   = t.getProperty (ids::userBuf, 0);
    if (s.bufferSize < 0 || s.bufferSize > 8192) s.bufferSize = 0;
    s.inputChannel = t.getProperty (ids::inputChannel, 0);
    if (s.inputChannel < -1 || s.inputChannel > 1) s.inputChannel = 0;
    s.muted = t.getProperty (ids::mute, false);
    s.bypassed = t.getProperty (ids::masterBypass, false);
    {
        const auto f = t.getProperty (ids::folders).toString();
        if (f.isNotEmpty()) { s.pluginFolders.addLines (f); s.pluginFolders.removeEmptyStrings(); }
    }
    s.windowState = t.getProperty (ids::window).toString();
    s.updateCheckEnabled  = t.getProperty (ids::updEnabled, false);
    s.updateCheckAsked    = t.getProperty (ids::updAsked, false);
    s.lastNotifiedVersion = t.getProperty (ids::updLast).toString();

    auto list = t.getChildWithName (ids::plugins);
    for (auto pt : list)
    {
        PluginEntryState p;
        p.fileOrId = pt.getProperty (ids::fileId);
        if (p.fileOrId.isEmpty()) continue;
        p.identifier = pt.getProperty (ids::identifier);
        p.displayName = pt.getProperty (ids::name);
        p.bypassed = pt.getProperty (ids::byp, false);
        p.state.fromBase64Encoding (pt.getProperty (ids::blob).toString());
        s.plugins.add (p);
    }
    return s;
}

juce::File configFile()
{
    return settingsDirectory().getChildFile ("config.xml");
}

juce::File settingsDirectory()
{
    return settingsOverride != juce::File() ? settingsOverride
        : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("CrystalVoice");
}

void setSettingsDirectory (const juce::File& directory) { settingsOverride = directory; }

bool saveStateToFile (const MicVSTState& s, const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    if (auto xml = toValueTree (s).createXml())
    {
        juce::TemporaryFile temporary (f);
        if (! xml->writeTo (temporary.getFile())) return false;
        if (auto previous = juce::parseXML (f))
            if (previous->hasTagName ("MicVST"))
                f.copyFileTo (f.getSiblingFile (f.getFileName() + ".bak"));
        return temporary.overwriteTargetFileWithTemporary();
    }
    return false;
}

MicVSTState loadStateFromFile (const juce::File& f)
{
    for (const auto& candidate : { f, f.getSiblingFile (f.getFileName() + ".bak") })
        if (auto xml = juce::XmlDocument::parse (candidate))
            if (xml->hasTagName ("MicVST"))
                return fromValueTree (juce::ValueTree::fromXml (*xml));
    return {};
}

bool saveState (const MicVSTState& s) { return saveStateToFile (s, configFile()); }
MicVSTState loadState() { return loadStateFromFile (configFile()); }
