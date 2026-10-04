#include "ui/PluginPicker.h"
#include "ui/DevicePanel.h"
#include "ui/WorkspaceLayout.h"
#include "ui/Theme.h"
#include "ui/PluginListView.h"
#include "ui/LevelMeterComponent.h"
#include "FakeAudioDevices.h"

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
        expect (results->getRowPosition (results->getSelectedRow(), true).getY() >= 0);
        expect (results->getRowPosition (results->getSelectedRow(), true).getBottom() <= results->getHeight());
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

struct DisconnectedDevicePanelRegression : juce::UnitTest
{
    DisconnectedDevicePanelRegression() : UnitTest ("Disconnected device controls") {}
    void runTest() override
    {
        AudioEngine engine (testAudio::devices());
        beginTest ("an intentionally disconnected profile keeps its selected sample rate visible");
        expect (engine.initialise ({}, {}, 44100.0).isEmpty()); expect (! engine.isRunning());
        expectEquals (engine.captureState().sampleRate, 44100.0);
        DevicePanel panel (engine);
        auto* rate = dynamic_cast<juce::ComboBox*> (panel.findChildWithID ("device-sample-rate"));
        expect (rate != nullptr);
        if (rate == nullptr) return;
        expectEquals (rate->getText(), juce::String ("44.1 kHz"));

        beginTest ("changing sample rate without endpoints still updates the saved preference and control");
        expect (engine.setDeviceConfig ({}, {}, 48000.0, 0).isEmpty());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        expectEquals (engine.captureState().sampleRate, 48000.0);
        expectEquals (rate->getText(), juce::String ("48.0 kHz"));
    }
};
static DisconnectedDevicePanelRegression disconnectedDevicePanelRegression;

struct WorkspaceRegression : juce::UnitTest
{
    WorkspaceRegression() : UnitTest ("Console workspace layout") {}
    void runTest() override
    {
        beginTest ("routing, meters, effects and footer remain separate at minimum size and on larger displays");
        for (const auto size : { juce::Point<int> (WorkspaceLayout::minWidth, WorkspaceLayout::minHeight),
                                juce::Point<int> (1000, 700), juce::Point<int> (1600, 1000) })
        {
            const juce::Rectangle<int> bounds (0, 0, size.x, size.y);
            const WorkspaceLayout layout (bounds);
            const std::vector<juce::Rectangle<int>> panels { layout.header, layout.routing, layout.inputMeter,
                layout.outputMeter, layout.effects, layout.footer };
            for (size_t i = 0; i < panels.size(); ++i)
            {
                expect (bounds.contains (panels[i])); expect (! panels[i].isEmpty());
                for (size_t j = i + 1; j < panels.size(); ++j) expect (! panels[i].intersects (panels[j]));
            }
            expect (layout.routing.getHeight() >= 462);
            expect (layout.effects.getWidth() >= 540);
            expect (layout.effects.getHeight() >= 360);
        }
        beginTest ("all device selectors stay inside the narrow routing column without overlap");
        AudioEngine engine (testAudio::devices()); engine.initialise ({}, {}, 48000);
        DevicePanel panel (engine); panel.setSize (296, 462);
        std::vector<juce::Rectangle<int>> controls;
        for (const auto* id : { "device-input", "device-output", "device-channel", "device-sample-rate", "device-buffer" })
        {
            auto* control = panel.findChildWithID (id); expect (control != nullptr);
            if (control == nullptr) continue;
            expect (panel.getLocalBounds().contains (control->getBounds()));
            expect (control->getWidth() >= 120); expect (control->getHeight() >= 30);
            for (const auto previous : controls) expect (! previous.intersects (control->getBounds()));
            controls.push_back (control->getBounds());
        }
    }
};
static WorkspaceRegression workspaceRegression;

struct EmbeddedConsoleFontRegression : juce::UnitTest
{
    EmbeddedConsoleFontRegression() : UnitTest ("Embedded console fonts") {}
    void runTest() override
    {
        beginTest ("both bundled weights load as JetBrains Mono without a system installation");
        auto regular = theme::typeface(), medium = theme::typeface (true);
        expect (regular != nullptr && medium != nullptr);
        if (regular == nullptr || medium == nullptr) return;
        expectEquals (regular->getName(), juce::String ("JetBrains Mono"));
        expectEquals (medium->getName(), juce::String ("JetBrains Mono"));
        expect (regular->getStyle() != medium->getStyle());
        expect (regular == theme::typeface()); expect (medium == theme::typeface (true));
        auto font = theme::font (14);
        expect (font.getTypefacePtr() == regular);
        juce::GlyphArrangement digits, letters;
        digits.addLineOfText (font, "0123456789", 0, 0);
        letters.addLineOfText (font, "MWil[]{}_.", 0, 0);
        expectWithinAbsoluteError (digits.getBoundingBox (0, -1, false).getWidth(),
                                   letters.getBoundingBox (0, -1, false).getWidth(), 0.01f);
    }
};
static EmbeddedConsoleFontRegression embeddedConsoleFontRegression;

namespace
{
    juce::MouseEvent rackMouse (juce::Component* row, float y, bool dragged = false)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), { 12.0f, y },
                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0,
                 row, row, now, { 12.0f, 30.0f }, now, 1, dragged };
    }
}

struct EffectReorderRegression : juce::UnitTest
{
    EffectReorderRegression() : UnitTest ("Effect rack drag preview") {}
    void runTest() override
    {
        beginTest ("other rows move through intermediate positions while the audio chain stays unchanged");
        AudioEngine engine (testAudio::devices()); engine.initialise ({}, {}, 48000);
        engine.getChain().addMonoToStereo(); engine.getChain().addStereoToMono(); engine.getChain().addMonoToStereo();
        const auto firstId = engine.getChain().entries()[0].id;
        const auto secondId = engine.getChain().entries()[1].id;
        int persists = 0; engine.onStateChanged = [&] { ++persists; };
        PluginListView rack (engine); rack.setBounds (-30000, -30000, 620, 470);
        // An off-screen, non-focusing peer exercises the visible-window timer lifecycle.
        rack.addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        rack.setVisible (true);
        expect (rack.isShowing(), "The off-screen peer must allow the same drag timer lifecycle as the visible application");
        auto* viewport = dynamic_cast<juce::Viewport*> (rack.findChildWithID ("effect-viewport"));
        expect (viewport != nullptr); if (viewport == nullptr) return;
        auto* holder = viewport->getViewedComponent();
        auto* first = holder->getChildComponent (0); auto* second = holder->getChildComponent (1);
        auto* third = holder->getChildComponent (2);

        first->mouseDown (rackMouse (first, 30)); first->mouseDrag (rackMouse (first, 130, true));
        expectEquals (engine.getChain().indexOf (firstId), 0); expectEquals (persists, 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (75);
        expect (second->getY() > 0 && second->getY() < 78); expectEquals (third->getY(), 156);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        expectEquals (second->getY(), 0); expectEquals (engine.getChain().indexOf (firstId), 0);

        beginTest ("reversing the drag restores the gap, and Escape leaves order and settings untouched");
        first->mouseDrag (rackMouse (first, -70, true));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        expectEquals (second->getY(), 78);
        expect (first->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        first->mouseUp (rackMouse (first, 30));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        expectEquals (first->getY(), 0); expectEquals (persists, 0);

        beginTest ("dropping commits once and snaps into place without recreating effect controls");
        first->mouseDown (rackMouse (first, 30)); first->mouseDrag (rackMouse (first, 186, true));
        first->mouseUp (rackMouse (first, 30));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        expectEquals (engine.getChain().indexOf (firstId), 2); expectEquals (persists, 1);
        expectEquals (second->getY(), 0); expectEquals (third->getY(), 78); expectEquals (first->getY(), 156);
        expect (first->getParentComponent() == holder); expectEquals (holder->getNumChildComponents(), 3);

        beginTest ("an unchanged drop does not rebuild or persist the audio chain");
        first->mouseDown (rackMouse (first, 30)); first->mouseUp (rackMouse (first, 30));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        expectEquals (engine.getChain().indexOf (firstId), 2); expectEquals (persists, 1);

        beginTest ("scrolling racks derive drag positions from the viewport instead of visible-row indexes");
        rack.setSize (620, 245); viewport->setViewPosition (0, 80);
        expect (viewport->getViewPositionY() > 0);
        first->mouseDown (rackMouse (first, 30)); first->mouseDrag (rackMouse (first, -126, true));
        first->mouseUp (rackMouse (first, 30));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (250);
        expectEquals (engine.getChain().indexOf (firstId), 0); expectEquals (persists, 2);
        expectEquals (first->getY(), 0); expectEquals (second->getY(), 78);
        expectEquals (engine.getChain().indexOf (secondId), 1);

        beginTest ("hovering near the viewport edge scrolls long chains while preview stays uncommitted");
        for (int i = 0; i < 8; ++i) engine.getChain().addMonoToStereo();
        engine.onScanFinished(); viewport->setViewPosition (0, 0);
        first = holder->getChildComponent (0);
        first->mouseDown (rackMouse (first, 30));
        first->mouseDrag (rackMouse (first, (float) viewport->getHeight() - 5, true));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);
        expect (viewport->getViewPositionY() > 0); expectEquals (engine.getChain().indexOf (firstId), 0);
        expectEquals (persists, 2);
        first->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        beginTest ("a scan refresh safely cancels a drag before destroying its controls");
        first->mouseDown (rackMouse (first, 30)); first->mouseDrag (rackMouse (first, 108, true));
        engine.onScanFinished();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (220);
        expectEquals (engine.getChain().indexOf (firstId), 0); expectEquals (persists, 2);
        expectEquals (holder->getNumChildComponents(), 11);
        rack.setVisible (false); rack.removeFromDesktop();
    }
};
static EffectReorderRegression effectReorderRegression;

struct PeakLineRenderingRegression : juce::UnitTest
{
    PeakLineRenderingRegression() : UnitTest ("Peak line rendering") {}
    void runTest() override
    {
        LevelMeterComponent meter; meter.setSize (240, 18);
        beginTest ("0 dBFS and over-range peaks remain visible inside the right border");
        for (const float peak : { 1.0f, 1.2f })
        {
            meter.setLevel ({ 0.0f, peak, 0.0f, peak });
            juce::Image image (juce::Image::ARGB, 240, 18, true, juce::SoftwareImageType());
            juce::Graphics graphics (image); meter.paint (graphics);
            bool redPeak = false;
            for (int x = 236; x < 240; ++x) redPeak = redPeak || image.getPixelAt (x, 8) == theme::danger;
            expect (redPeak, "Edge pixels: " + image.getPixelAt (237, 8).toString() + ", "
                            + image.getPixelAt (238, 8).toString() + ", " + image.getPixelAt (239, 8).toString());
        }
        beginTest ("negative-dB sample peaks align with the existing scale and retain the normal line colour");
        meter.setLevel ({ 0.0f, 0.5f, 0.0f, 0.5f });
        juce::Image image (juce::Image::ARGB, 240, 18, true, juce::SoftwareImageType());
        juce::Graphics graphics (image); meter.paint (graphics);
        const int expectedX = (int) (meterScale::dbToNorm (juce::Decibels::gainToDecibels (0.5f)) * 240);
        bool yellowPeak = false;
        for (int x = expectedX - 1; x <= expectedX + 1; ++x)
            yellowPeak = yellowPeak || image.getPixelAt (x, 8) == theme::warning;
        expect (yellowPeak);
    }
};
static PeakLineRenderingRegression peakLineRenderingRegression;
