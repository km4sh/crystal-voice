#include "audio/PluginIdentity.h"

std::unique_ptr<juce::PluginDescription> resolvePlugin (const PluginEntryState& saved,
                                                       const juce::KnownPluginList& known)
{
    if (saved.identifier.isNotEmpty())
        if (auto exact = known.getTypeForIdentifierString (saved.identifier))
            if ((saved.format.isEmpty() || saved.format == exact->pluginFormatName)
                && (saved.manufacturer.isEmpty() || saved.manufacturer == exact->manufacturerName)
                && (saved.classUid.isEmpty() || saved.classUid.equalsIgnoreCase (juce::String::toHexString (exact->uniqueId))
                    || saved.classUid.equalsIgnoreCase (juce::String::toHexString (exact->deprecatedUid)))) return exact;
    auto uid = saved.classUid;
    auto format = saved.format;
    if (uid.isEmpty() && saved.identifier.isNotEmpty())
    {
        uid = saved.identifier.fromLastOccurrenceOf ("-", false, false);
        if (saved.identifier.startsWith ("VST3-")) format = "VST3";
    }
    std::unique_ptr<juce::PluginDescription> match;
    for (const auto& type : known.getTypes())
    {
        const bool identity = uid.isNotEmpty()
            && (uid.equalsIgnoreCase (juce::String::toHexString (type.uniqueId))
                || uid.equalsIgnoreCase (juce::String::toHexString (type.deprecatedUid)))
            && (format.isEmpty() || format == type.pluginFormatName)
            && (saved.manufacturer.isEmpty() || saved.manufacturer == type.manufacturerName)
            && (saved.classUid.isNotEmpty() && saved.manufacturer.isNotEmpty()
                ? true : (saved.displayName.isEmpty() || saved.displayName == type.name));
        const bool legacyFile = uid.isEmpty() && saved.identifier.isEmpty()
            && juce::File (saved.fileOrId) == juce::File (type.fileOrIdentifier);
        if (! identity && ! legacyFile) continue;
        if (match != nullptr) return {}; // Multiple installations/classes need an explicit choice.
        match = std::make_unique<juce::PluginDescription> (type);
    }
    return match;
}
