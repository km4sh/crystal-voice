#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// One source of truth for the minimum-size workspace and its preview/tests.
struct WorkspaceLayout
{
    static constexpr int minWidth = 920, minHeight = 640;
    juce::Rectangle<int> header, routing, inputMeter, outputMeter, effects, footer;
    explicit WorkspaceLayout (juce::Rectangle<int> bounds)
    {
        auto content = bounds.reduced (20);
        header = content.removeFromTop (78); content.removeFromTop (14);
        footer = content.removeFromBottom (30); content.removeFromBottom (16);
        routing = content.removeFromLeft (296); content.removeFromLeft (16);
        auto meters = content.removeFromTop (86); content.removeFromTop (14);
        inputMeter = meters.removeFromLeft ((meters.getWidth() - 12) / 2);
        meters.removeFromLeft (12); outputMeter = meters;
        effects = content;
    }
};
