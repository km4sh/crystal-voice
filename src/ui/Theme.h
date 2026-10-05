#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

// A quiet, static console inspired by threshold-34. No animated backdrop or GPU context.
namespace theme
{
    inline const juce::Colour background { 0xff080c0e }, surface { 0xff0e1416 },
        raised { 0xff182224 }, border { 0xff2b3d39 }, text { 0xffd2ddd6 },
        muted { 0xff85958e }, accent { 0xff39e67a }, warning { 0xffe8c467 },
        danger { 0xffff7070 }, cyan { 0xff69cddd };
    inline juce::Typeface::Ptr typeface (bool medium = false)
    {
        static const auto regular = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf,
                                                                           BinaryData::JetBrainsMonoRegular_ttfSize);
        static const auto emphasis = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf,
                                                                            BinaryData::JetBrainsMonoMedium_ttfSize);
        return medium ? emphasis : regular;
    }
    inline juce::Font font (float size, bool medium = false)
    {
        // Its generous vertical metrics need slightly more JUCE height to keep
        // console text legible at the same control sizes as system UI fonts.
        return juce::Font (juce::FontOptions (typeface (medium)).withHeight (size * 1.2f).withFeatureDisabled ("calt"));
    }
    inline void card (juce::Graphics& g, juce::Rectangle<float> bounds, float = 0.0f)
    {
        g.setColour (surface); g.fillRect (bounds);
        g.setColour (border); g.drawRect (bounds.reduced (0.5f), 1.0f);
    }
    inline void caption (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> bounds,
                         juce::Colour colour = accent)
    {
        g.setColour (colour); g.setFont (font (12, true));
        g.drawText (title, bounds, juce::Justification::centredLeft);
    }
    inline void primary (juce::TextButton& button)
    {
        button.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.12f));
        button.setColour (juce::TextButton::textColourOffId, accent);
    }
    inline void window (juce::DocumentWindow& window)
    { window.setUsingNativeTitleBar (false); window.setTitleBarHeight (30); }
}

class CrystalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CrystalLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("JetBrains Mono");
        setColour (juce::ResizableWindow::backgroundColourId, theme::background);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::TextButton::buttonColourId, theme::surface);
        setColour (juce::TextButton::buttonOnColourId, theme::accent.withAlpha (0.18f));
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::TextButton::textColourOnId, theme::accent);
        setColour (juce::ComboBox::backgroundColourId, theme::background);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::outlineColourId, theme::border);
        setColour (juce::ComboBox::arrowColourId, theme::accent);
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
        setColour (juce::AlertWindow::backgroundColourId, theme::surface);
        setColour (juce::AlertWindow::textColourId, theme::text);
        setColour (juce::AlertWindow::outlineColourId, theme::border);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return theme::font (13, true); }
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (f.getTypefaceName() == "JetBrains Mono" || f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return theme::typeface (f.isBold());
        return juce::LookAndFeel_V4::getTypefaceForFont (f);
    }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return theme::font (13); }
    juce::Font getPopupMenuFont() override { return theme::font (13); }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour,
                               bool hover, bool down) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
        const auto ink = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                        : juce::TextButton::textColourOffId);
        g.setColour ((down ? ink.withAlpha (0.22f) : hover ? ink.withAlpha (0.10f) : colour)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.fillRect (bounds);
        g.setColour ((b.hasKeyboardFocus (true) || hover || b.getToggleState() ? ink.withAlpha (0.7f)
                                                                                        : theme::border)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.drawRect (bounds, 1.0f);
    }
    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId)); g.fillRect (bounds);
        g.setColour (box.hasKeyboardFocus (true) ? theme::accent : theme::border); g.drawRect (bounds, 1.0f);
        g.setColour (theme::accent.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.35f));
        g.setFont (theme::font (12));
        g.drawText ("v", width - 27, 0, 20, height, juce::Justification::centred);
    }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    { label.setBounds (10, 1, box.getWidth() - 38, box.getHeight() - 2); label.setFont (getComboBoxFont (box)); }
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        const float alpha = b.isEnabled() ? 1.0f : 0.35f;
        g.setColour ((b.getToggleState() || hover ? theme::accent : theme::muted).withMultipliedAlpha (alpha));
        g.setFont (theme::font (12));
        g.drawText (b.getToggleState() ? "[x]" : "[ ]", 0, 0, 26, b.getHeight(), juce::Justification::centredLeft);
        g.drawText (b.getButtonText(), 30, 0, b.getWidth() - 30, b.getHeight(), juce::Justification::centredLeft);
        if (b.hasKeyboardFocus (true)) g.drawRect (b.getLocalBounds(), 1);
    }
    void drawDocumentWindowTitleBar (juce::DocumentWindow& w, juce::Graphics& g, int width, int height,
                                    int titleX, int titleW, const juce::Image*, bool) override
    {
        g.fillAll (theme::surface);
        g.setColour (theme::border); g.drawHorizontalLine (height - 1, 0, (float) width);
        g.setColour (theme::accent); g.setFont (theme::font (12, true));
        g.drawText ("[ " + w.getName().toUpperCase() + " ]", titleX + 8, 0, titleW - 16, height,
                    juce::Justification::centredLeft);
    }
    juce::Button* createDocumentWindowButton (int type) override
    {
        class WindowButton : public juce::Button
        {
        public:
            explicit WindowButton (int t) : Button (t == juce::DocumentWindow::closeButton ? "Close"
                : t == juce::DocumentWindow::minimiseButton ? "Minimise" : "Maximise"), type (t) {}
            void paintButton (juce::Graphics& g, bool hover, bool down) override
            {
                const auto colour = type == juce::DocumentWindow::closeButton ? theme::danger : theme::accent;
                if (hover || down) { g.setColour (colour.withAlpha (down ? 0.25f : 0.12f)); g.fillAll(); }
                g.setColour (hover ? colour : theme::muted);
                const float x = getWidth() / 2.0f, y = getHeight() / 2.0f;
                if (type == juce::DocumentWindow::closeButton)
                { g.drawLine (x - 4, y - 4, x + 4, y + 4, 1); g.drawLine (x + 4, y - 4, x - 4, y + 4, 1); }
                else if (type == juce::DocumentWindow::minimiseButton) g.drawLine (x - 5, y + 3, x + 5, y + 3, 1);
                else g.drawRect (juce::Rectangle<float> (x - 4, y - 4, 8, 8), 1);
            }
        private:
            int type;
        };
        return new WindowButton (type);
    }
    void positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int width, int height,
                                       juce::Button* minimise, juce::Button* maximise, juce::Button* close, bool) override
    {
        int right = x + width - 4;
        for (auto* button : { close, maximise, minimise })
            if (button != nullptr) { right -= 34; button->setBounds (right, y, 34, height); }
    }
};
