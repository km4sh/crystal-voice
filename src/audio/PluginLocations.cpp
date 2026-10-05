#include "audio/PluginLocations.h"

juce::StringArray pluginLocations::normaliseFolders (const juce::StringArray& paths)
{
    juce::StringArray result;
    for (const auto& path : paths)
    {
        const auto trimmed = path.trim().unquoted();
        if (! juce::File::isAbsolutePath (trimmed)) continue;
       #if JUCE_WINDOWS
        // Drive-relative and single-slash paths depend on the process working directory.
        if (! trimmed.startsWith ("\\\\") && ! trimmed.startsWith ("//")
            && ! (trimmed.length() >= 3 && trimmed[1] == ':' && (trimmed[2] == '\\' || trimmed[2] == '/'))) continue;
       #endif
        // A trailing dot component needs its separator for JUCE to remove it.
        const auto normalised = juce::File (trimmed + juce::File::getSeparatorString()).getFullPathName();
        if (! result.contains (normalised, (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) != 0))
            result.add (normalised);
    }
    return result;
}

juce::StringArray pluginLocations::windowsDefaults (const juce::String& programFiles,
                                                   const juce::String& commonFiles,
                                                   const juce::String& localAppData,
                                                   const juce::String& applicationDirectory,
                                                   const juce::String& environmentPaths)
{
    auto paths = juce::StringArray::fromTokens (environmentPaths, ";", "\"");
    const auto append = [&paths] (const juce::String& base, const juce::String& suffix)
    {
        if (juce::File::isAbsolutePath (base)) paths.add (juce::File (base).getChildFile (suffix).getFullPathName());
    };
    append (localAppData, "Programs/Common/VST3");
    append (commonFiles, "VST3");
    append (applicationDirectory, "VST3");
    // Some installers also put VST3 effects beside their older VST plugins.
    // Enumerate only VST3 bundles in these bounded, conventional directories.
    append (programFiles, "VST3");
    append (programFiles, "VSTPlugins");
    append (programFiles, "Steinberg/VstPlugins");
    append (commonFiles, "VST2");
    append (commonFiles, "Steinberg/VST2");
    return normaliseFolders (paths);
}
