#pragma once
#include <juce_core/juce_core.h>

namespace pluginLocations
{
    // Keep drive letters intact: Windows path lists use semicolons, not colons.
    juce::StringArray windowsDefaults (const juce::String& programFiles,
                                      const juce::String& commonFiles,
                                      const juce::String& localAppData,
                                      const juce::String& applicationDirectory,
                                      const juce::String& environmentPaths);
    juce::StringArray normaliseFolders (const juce::StringArray&);
}
