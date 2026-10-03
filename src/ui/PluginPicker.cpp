#include "ui/PluginPicker.h"
#include "ui/Theme.h"

PluginPickerComponent::PluginPickerComponent (juce::Array<juce::PluginDescription> choices,
    std::function<void (const juce::PluginDescription&)> chosen, std::function<void()> cancel)
    : all (std::move (choices)), onChosen (std::move (chosen)), onCancel (std::move (cancel))
{
    search.setTextToShowWhenEmpty ("Search effects or manufacturers...", theme::muted);
    search.setFont (theme::font (15)); search.setIndents (12, 11);
    search.setComponentID ("plugin-search"); list.setComponentID ("plugin-results"); addButton.setComponentID ("plugin-add");
    search.onTextChange = [this] { rebuildItems(); }; search.addKeyListener (this);
    list.setRowHeight (46); list.setOutlineThickness (0);
    list.addKeyListener (this);
    list.setColour (juce::ListBox::backgroundColourId, theme::background);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme::border);
    theme::primary (addButton); addButton.onClick = [this] { chooseRow (list.getSelectedRow()); };
    cancelButton.onClick = [this] { const auto callback = onCancel; if (callback) callback(); };
    results.setFont (theme::font (12)); results.setColour (juce::Label::textColourId, theme::muted);
    empty.setFont (theme::font (14)); empty.setJustificationType (juce::Justification::centred);
    empty.setColour (juce::Label::textColourId, theme::muted);
    for (auto* child : std::initializer_list<juce::Component*> { &search, &list, &addButton, &cancelButton, &results, &empty }) addAndMakeVisible (child);
    rebuildItems(); setSize (600, 550);
}

void PluginPickerComponent::focusSearch() { search.grabKeyboardFocus(); }
void PluginPickerComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background); g.setColour (theme::text); g.setFont (theme::font (23, true));
    g.drawText ("Find your next effect", 24, 16, getWidth() - 48, 32, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::font (13));
    g.drawText ("VST3 effects and built-in channel routing", 24, 51, getWidth() - 48, 20, juce::Justification::centredLeft);
    g.setColour (theme::border); g.drawHorizontalLine (getHeight() - 64, 24, (float) getWidth() - 24);
}

void PluginPickerComponent::resized()
{
    auto r = getLocalBounds().reduced (24); r.removeFromTop (62);
    auto footer = r.removeFromBottom (34); addButton.setBounds (footer.removeFromRight (120));
    footer.removeFromRight (10); cancelButton.setBounds (footer.removeFromRight (90));
    r.removeFromBottom (20); search.setBounds (r.removeFromTop (42)); r.removeFromTop (8);
    results.setBounds (r.removeFromTop (22)); r.removeFromTop (4); list.setBounds (r);
    empty.setBounds (r.reduced (20));
}

void PluginPickerComponent::rebuildItems()
{
    filtered = filterPlugins (all, search.getText()); items.clear();
    const bool grouped = search.getText().trim().isEmpty(); juce::String manufacturer ("\x01");
    for (int i = 0; i < filtered.size(); ++i)
    {
        const auto& description = filtered.getReference (i);
        if (grouped && description.manufacturerName != manufacturer)
        { manufacturer = description.manufacturerName; items.push_back ({ true, manufacturer.isEmpty() ? "Other" : manufacturer, -1 }); }
        items.push_back ({ false, description.name, i });
    }
    list.updateContent(); list.selectRow (firstSelectableRow());
    list.scrollToEnsureRowIsOnscreen (list.getSelectedRow()); list.repaint();
    selectedRowsChanged (list.getSelectedRow()); empty.setVisible (filtered.isEmpty());
    results.setText (juce::String (filtered.size()) + (filtered.size() == 1 ? " effect available" : " effects available"), juce::dontSendNotification);
}

int PluginPickerComponent::firstSelectableRow() const
{ for (int i = 0; i < (int) items.size(); ++i) if (! items[(size_t) i].header) return i; return -1; }

void PluginPickerComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
    const auto& item = items[(size_t) row];
    if (item.header)
    { g.setColour (theme::muted); g.setFont (theme::font (11, true));
      g.drawText (item.text.toUpperCase(), 10, 12, width - 20, height - 12, juce::Justification::centredLeft); return; }
    if (selected) { g.setColour (theme::raised); g.fillRoundedRectangle (2, 1, (float) width - 4, (float) height - 2, 8); }
    const auto& description = filtered[item.pluginIndex];
    g.setColour (selected ? theme::accent : theme::text); g.setFont (theme::font (14, true));
    g.drawText (item.text, 14, 5, width - 108, 22, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::font (11));
    g.drawText (description.manufacturerName, 14, 26, width - 108, 16, juce::Justification::centredLeft);
    g.drawText (description.fileOrIdentifier.startsWith ("builtin:") ? "ROUTING" : "VST3", width - 88, 0, 72, height, juce::Justification::centredRight);
}

void PluginPickerComponent::selectedRowsChanged (int row)
{ addButton.setEnabled (juce::isPositiveAndBelow (row, (int) items.size()) && ! items[(size_t) row].header); }

void PluginPickerComponent::chooseRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size())) return;
    const auto item = items[(size_t) row]; if (item.header || ! juce::isPositiveAndBelow (item.pluginIndex, filtered.size())) return;
    const auto description = filtered[item.pluginIndex]; const auto callback = onChosen;
    if (callback) callback (description);
}
void PluginPickerComponent::moveSelection (int direction)
{
    for (int row = list.getSelectedRow() + direction; juce::isPositiveAndBelow (row, (int) items.size()); row += direction)
        if (! items[(size_t) row].header) { list.selectRow (row); list.scrollToEnsureRowIsOnscreen (row); return; }
}
bool PluginPickerComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::downKey) { moveSelection (1); return true; }
    if (key == juce::KeyPress::upKey) { moveSelection (-1); return true; }
    if (key == juce::KeyPress::returnKey) { chooseRow (list.getSelectedRow()); return true; }
    if (key == juce::KeyPress::escapeKey) { const auto callback = onCancel; if (callback) callback(); return true; }
    return false;
}
