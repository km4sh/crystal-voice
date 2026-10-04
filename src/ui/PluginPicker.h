#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/PluginSearch.h"
#include <optional>

class PluginPickerComponent : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
{
public:
    PluginPickerComponent (juce::Array<juce::PluginDescription>,
                          std::function<void (const juce::PluginDescription&)>, std::function<void()> onCancel);
    void paint (juce::Graphics&) override;
    void resized() override;
    void focusSearch();
    void setChoices (juce::Array<juce::PluginDescription>);
    bool keyPressed (const juce::KeyPress& key) override { return keyPressed (key, this); }
private:
    struct Item { bool header; juce::String text; int pluginIndex; };
    void rebuildItems (bool preserveSelection = false);
    void chooseRow (int);
    void moveSelection (int);
    int firstSelectableRow() const;
    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { chooseRow (row); }
    void returnKeyPressed (int row) override { chooseRow (row); }
    void selectedRowsChanged (int row) override;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    juce::Array<juce::PluginDescription> all, filtered;
    std::optional<juce::PluginDescription> selectionToRestore;
    std::vector<Item> items;
    std::function<void (const juce::PluginDescription&)> onChosen;
    std::function<void()> onCancel;
    juce::TextEditor search;
    juce::ListBox list { {}, this };
    juce::TextButton addButton { "Add effect" }, cancelButton { "Cancel" };
    juce::Label results, empty { {}, "No effects found. Try a different name or manufacturer." };
};
