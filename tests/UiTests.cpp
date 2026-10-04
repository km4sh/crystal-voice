#include "ui/PluginPicker.h"

struct PluginPickerRegression : juce::UnitTest
{
    PluginPickerRegression() : UnitTest ("Plugin picker interactions") {}
    void runTest() override
    {
        juce::Array<juce::PluginDescription> choices;
        juce::PluginDescription eq; eq.name = "Voice EQ"; eq.manufacturerName = "Acme"; eq.fileOrIdentifier = "C:/Voice.vst3"; eq.uniqueId = 101;
        juce::PluginDescription mono; mono.name = "Mono to stereo"; mono.manufacturerName = "Built-in"; mono.fileOrIdentifier = "builtin:mono2stereo";
        choices.add (eq); choices.add (mono);
        int chosen = 0, cancelled = 0, chosenClass = 0; juce::String chosenId;
        PluginPickerComponent picker (choices, [&] (const juce::PluginDescription& type) { ++chosen; chosenId = type.fileOrIdentifier; chosenClass = type.uniqueId; }, [&] { ++cancelled; });
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

        beginTest ("library updates retain the search and selected class when another class shares the VST3 bundle");
        search->setText ("acme", false); search->onTextChange();
        auto compressor = eq; compressor.uniqueId = 202; compressor.name = "Voice Compressor";
        choices.add (compressor); picker.setChoices (choices);
        expectEquals (search->getText(), juce::String ("acme"));
        expectEquals (results->getListBoxModel()->getNumRows(), 2);
        expectEquals (results->getSelectedRow(), 1);
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosenClass, eq.uniqueId);
        auto renamed = choices; renamed.getReference (0).name = "AAA Equalizer";
        picker.setChoices (renamed); expectEquals (results->getSelectedRow(), 0);
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosenClass, eq.uniqueId);

        beginTest ("removing a selected class disables Add instead of silently selecting another effect");
        renamed.remove (0); const int beforeRemoval = chosen;
        picker.setChoices (renamed);
        expectEquals (results->getSelectedRow(), -1); expect (! add->isEnabled());
        picker.setChoices (renamed); expect (! add->isEnabled());
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosen, beforeRemoval);
        picker.keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        expect (add->isEnabled()); picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
        expectEquals (chosenClass, compressor.uniqueId);

        beginTest ("an empty live search gains newly scanned effects without reopening the picker");
        search->setText ("noise", false); search->onTextChange(); expect (! add->isEnabled());
        auto noise = eq; noise.uniqueId = 303; noise.name = "Noise Suppressor"; renamed.add (noise);
        picker.setChoices (renamed); expectEquals (search->getText(), juce::String ("noise"));
        expectEquals (results->getListBoxModel()->getNumRows(), 1); expect (add->isEnabled());
        picker.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)); expectEquals (chosenClass, noise.uniqueId);
    }
};
static PluginPickerRegression pluginPickerRegression;
