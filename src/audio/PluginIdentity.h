#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "state/Persistence.h"

// Never choose between ambiguous classes by name or installation order.
std::unique_ptr<juce::PluginDescription> resolvePlugin (const PluginEntryState&,
                                                       const juce::KnownPluginList&);
