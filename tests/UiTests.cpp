#include "ui/PluginPicker.h"

struct PluginPickerRegression : juce::UnitTest
{
    PluginPickerRegression() : UnitTest ("Plugin picker interactions") {}
    void runTest() override
    {
        juce::Array<juce::PluginDescription> choices;
        juce::PluginDescription eq; eq.name = "Voice EQ"; eq.manufacturerName = "Acme"; eq.fileOrIdentifier = "C:/Voice.vst3";
        juce::PluginDescription mono; mono.name = "Mono to stereo"; mono.manufacturerName = "Built-in"; mono.fileOrIdentifier = "builtin:mono2stereo";
        choices.add (eq); choices.add (mono);
        int chosen = 0, cancelled = 0; juce::String chosenId;
        PluginPickerComponent picker (choices, [&] (const juce::PluginDescription& type) { ++chosen; chosenId = type.fileOrIdentifier; }, [&] { ++cancelled; });
        auto* search = dynamic_cast<juce::TextEditor*> (picker.findChildWithID ("plugin-search"));
        auto* results = dynamic_cast<juce::ListBox*> (picker.findChildWithID ("plugin-results"));
        auto* add = dynamic_cast<juce::TextButton*> (picker.findChildWithID ("plugin-add"));
        beginTest ("search has a keyboard target and Add selects an effect, not a manufacturer header");
        expect (search != nullptr && results != nullptr && add != nullptr);
        if (search == nullptr || results == nullptr || add == nullptr) return;
        expect (results->getSelectedRow() > 0); expect (add->isEnabled());
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        expectEquals (chosen, 1); expectEquals (chosenId, eq.fileOrIdentifier);

        beginTest ("an empty search result cannot accidentally add the old selection");
        search->setText ("no matching effect", false); search->onTextChange();
        expectEquals (results->getListBoxModel()->getNumRows(), 0); expect (! add->isEnabled());
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosen, 1);

        beginTest ("manufacturer filtering and Enter select the right VST3 class");
        search->setText ("acme", false); search->onTextChange();
        expectEquals (results->getListBoxModel()->getNumRows(), 1); expect (add->isEnabled());
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosenId, eq.fileOrIdentifier);

        beginTest ("arrow navigation skips nonselectable manufacturer headers");
        search->setText ({}, false); search->onTextChange();
        picker.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosenId, mono.fileOrIdentifier);
        picker.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)); expectEquals (cancelled, 1);
    }
};
static PluginPickerRegression pluginPickerRegression;
