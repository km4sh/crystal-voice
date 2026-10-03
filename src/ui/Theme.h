#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour background { 0xff10151c }, surface { 0xff19212b },
        raised { 0xff212c38 }, border { 0xff2c3948 }, text { 0xffedf3fa },
        muted { 0xff94a6b9 }, accent { 0xff69dfc6 }, warning { 0xfff3bc73 },
        danger { 0xffff8294 };
    inline juce::Font font (float size, bool bold = false)
    { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
    inline void card (juce::Graphics& g, juce::Rectangle<float> bounds, float radius = 14.0f)
    {
        g.setColour (surface); g.fillRoundedRectangle (bounds, radius);
        g.setColour (border); g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }
    inline void primary (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, accent);
        button.setColour (juce::TextButton::textColourOffId, background);
    }
}

class CrystalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CrystalLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Segoe UI");
        setColour (juce::ResizableWindow::backgroundColourId, theme::background);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::TextButton::buttonColourId, theme::raised);
        setColour (juce::TextButton::buttonOnColourId, theme::accent);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::background);
        setColour (juce::ComboBox::backgroundColourId, theme::background);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::outlineColourId, theme::border);
        setColour (juce::ComboBox::arrowColourId, theme::muted);
        setColour (juce::PopupMenu::backgroundColourId, theme::surface);
        setColour (juce::PopupMenu::textColourId, theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::raised);
        setColour (juce::PopupMenu::highlightedTextColourId, theme::accent);
        setColour (juce::TextEditor::backgroundColourId, theme::background);
        setColour (juce::TextEditor::textColourId, theme::text);
        setColour (juce::TextEditor::outlineColourId, theme::border);
        setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.25f));
        setColour (juce::ListBox::backgroundColourId, theme::surface);
        setColour (juce::ScrollBar::thumbColourId, theme::border.brighter (0.2f));
        setColour (juce::ToggleButton::textColourId, theme::muted);
        setColour (juce::ToggleButton::tickColourId, theme::accent);
        setColour (juce::TooltipWindow::backgroundColourId, theme::raised);
        setColour (juce::TooltipWindow::textColourId, theme::text);
        setColour (juce::TooltipWindow::outlineColourId, theme::border);
        setColour (juce::ProgressBar::backgroundColourId, theme::raised);
        setColour (juce::ProgressBar::foregroundColourId, theme::accent);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (14.0f, true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (14.0f); }
    juce::Font getPopupMenuFont() override { return theme::font (14.0f); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                               bool hover, bool down) override
    {
        auto c = colour.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f);
        if (hover) c = c.brighter (0.08f);
        if (down) c = c.darker (0.10f);
        auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (c); g.fillRoundedRectangle (bounds, 8.0f);
        if (colour != theme::accent)
        { g.setColour (theme::border); g.drawRoundedRectangle (bounds, 8.0f, 1.0f); }
    }
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (box.hasKeyboardFocus (true) ? theme::accent : theme::border);
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
        juce::Path arrow;
        arrow.startNewSubPath ((float) width - 23.0f, (float) height / 2 - 2);
        arrow.lineTo ((float) width - 18.0f, (float) height / 2 + 3);
        arrow.lineTo ((float) width - 13.0f, (float) height / 2 - 2);
        g.setColour (theme::muted.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (arrow, juce::PathStrokeType (1.5f));
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    { label.setBounds (12, 1, box.getWidth() - 42, box.getHeight() - 2); label.setFont (getComboBoxFont (box)); }
};
