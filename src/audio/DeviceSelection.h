#pragma once
#include <juce_core/juce_core.h>

inline juce::String preferredMicrophone (const juce::StringArray& inputs, int defaultIndex)
{
    auto physical = [] (const juce::String& name)
    {
        return ! (name.containsIgnoreCase ("CABLE Output") || name.containsIgnoreCase ("VB-Audio")
            || name.containsIgnoreCase ("VoiceMeeter") || name.containsIgnoreCase ("Virtual Audio")
            || name.containsIgnoreCase ("Loopback") || name.startsWithIgnoreCase ("Mix "));
    };
    if (juce::isPositiveAndBelow (defaultIndex, inputs.size()) && physical (inputs[defaultIndex]))
        return inputs[defaultIndex];
    for (const auto& name : inputs)
        if (physical (name) && (name.containsIgnoreCase ("mic") || name.containsIgnoreCase ("input"))) return name;
    for (const auto& name : inputs) if (physical (name)) return name;
    return {};
}
