#include "ui/PluginPicker.h"
#include "ui/DevicePanel.h"
#include "ui/WorkspaceLayout.h"
#include "ui/Theme.h"
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
