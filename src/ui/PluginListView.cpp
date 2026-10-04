#include "ui/PluginListView.h"
#include "ui/PluginPicker.h"
#include "ui/Theme.h"

namespace
{
    class PickerWindow : public juce::DocumentWindow
    {
    public:
        PickerWindow (juce::Component* content) : DocumentWindow ("Add an effect", theme::background, closeButton)
        {
            theme::window (*this); setContentOwned (content, false);
            setResizable (true, false); setResizeLimits (500, 420, 1000, 1000);
            centreWithSize (600, 580);
        }
        void closeButtonPressed() override { setVisible (false); }
    };
}

PluginListView::Row::Row (PluginListView& parent, int i)
    : owner (parent), id (parent.engine.getChain().entries()[(size_t) i].id), index (i)
{
    const auto& effect = owner.engine.getChain().entries()[(size_t) i];
    setName (effect.displayName);
    setTooltip (effect.displayName + "\n" + (effect.isUnavailable() ? effect.error : effect.manufacturer));
    enabledButton.setClickingTogglesState (true);
    enabledButton.setToggleState (! effect.bypassed, juce::dontSendNotification);
    enabledButton.setButtonText (effect.bypassed ? "OFF" : "ON");
    enabledButton.setEnabled (! effect.isUnavailable());
    enabledButton.setTooltip ("Enable or bypass this effect");
    enabledButton.onClick = [this] { owner.toggleBypass (id); };
    openButton.onClick = [this] { owner.openEditor (id); };
    openButton.setTooltip (effect.isUnavailable() ? effect.error : "Open the effect editor");
    openButton.setButtonText (effect.isUnavailable() ? "Retry" : "[ EDIT ]");
    moreButton.onClick = [this] { owner.showRowMenu (id, &moreButton); };
    moreButton.setTooltip ("Move or remove this effect");
    addAndMakeVisible (enabledButton); addAndMakeVisible (moreButton);
    if (! effect.isBuiltIn()) addAndMakeVisible (openButton);
}

void PluginListView::Row::resized()
{
    auto r = getLocalBounds().reduced (12, 18);
    moreButton.setBounds (r.removeFromRight (30)); r.removeFromRight (6);
    enabledButton.setBounds (r.removeFromRight (40)); r.removeFromRight (6);
    openButton.setBounds (r.removeFromRight (76));
}

void PluginListView::Row::paint (juce::Graphics& g)
{
    const int current = owner.engine.getChain().indexOf (id);
    if (current < 0) return;
    const auto& effect = owner.engine.getChain().entries()[(size_t) current];
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (dragging ? theme::raised : theme::background); g.fillRect (bounds);
    g.setColour (dragging || isMouseOver (true) ? theme::accent.withAlpha (0.6f) : theme::border); g.drawRect (bounds, 1);
    const auto stateColour = effect.isUnavailable() ? theme::warning : effect.bypassed ? theme::muted : theme::accent;
    g.setColour (stateColour); g.fillRect (0, 0, 2, getHeight());
    g.setColour (theme::muted.withAlpha (0.6f));
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row) g.fillRect (11.0f + column * 5, 26.0f + row * 6, 2.0f, 2.0f);
    g.setColour (stateColour);
    g.setFont (theme::font (13, true));
    g.drawText (juce::String (current + 1).paddedLeft ('0', 2), 27, 16, 28, 36, juce::Justification::centred);
    const int right = openButton.getX() - 10;
    g.setFont (theme::font (15, true));
    g.setColour (effect.bypassed ? theme::muted : theme::text);
    g.drawText (effect.displayName, 64, 10, juce::jmax (1, right - 64), 24, juce::Justification::centredLeft);
    juce::String detail = effect.isUnavailable() ? "Unavailable - settings preserved" : effect.manufacturer;
    if (auto* node = owner.engine.getGraph().getNodeForId (effect.node))
    {
        auto* processor = node->getProcessor();
        auto* device = owner.engine.getDeviceManager().getCurrentAudioDevice();
        const double rate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
        detail << " / " << processor->getMainBusNumInputChannels() << ">" << processor->getMainBusNumOutputChannels()
               << " / " << juce::String (rate > 0 ? processor->getLatencySamples() * 1000.0 / rate : 0, 1) << " ms";
    }
    g.setColour (effect.isUnavailable() ? theme::warning : theme::muted); g.setFont (theme::font (11));
    g.drawText (detail, 64, 36, juce::jmax (1, right - 64), 20, juce::Justification::centredLeft);
}

void PluginListView::Row::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < 28) { dragging = true; grabOffsetY = e.y; toFront (false); repaint(); }
}
void PluginListView::Row::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) setTopLeftPosition (0, juce::jlimit (0, juce::jmax (0, (owner.rows.size() - 1) * rowPitch),
        e.getEventRelativeTo (getParentComponent()).y - grabOffsetY));
}
void PluginListView::Row::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false; owner.requestMove (id, (getY() + rowPitch / 2) / rowPitch);
}
void PluginListView::Row::mouseDoubleClick (const juce::MouseEvent& e) { if (e.x >= 28) owner.openEditor (id); }

PluginListView::EditorWindow::EditorWindow (const juce::String& title, juce::uint32 id,
                                           std::function<void (EditorWindow*)> close)
    : DocumentWindow (title, theme::background, closeButton), entryId (id), onClose (std::move (close)) {}
void PluginListView::EditorWindow::closeButtonPressed() { const auto callback = onClose; if (callback) callback (this); }

PluginListView::PluginListView (AudioEngine& e) : engine (e)
{
    theme::primary (addButton);
    addButton.onClick = [this] { showPluginPicker(); };
    foldersButton.onClick = [this] { showFolderMenu(); };
    foldersButton.setColour (juce::TextButton::textColourOffId, theme::cyan);
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Compare with your dry microphone. Mute still applies.");
    bypassButton.setColour (juce::TextButton::textColourOnId, theme::warning);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warning.withAlpha (0.12f));
    bypassButton.onClick = [this] { engine.setMasterBypass (bypassButton.getToggleState()); refreshProcessingState(); };
    refreshProcessingState();
    foldersButton.setTooltip ("Plugin folders, scanning and recovery");
    for (auto* component : std::initializer_list<juce::Component*> {
        &addButton, &foldersButton, &bypassButton, &viewport, &scanLabel, &scanBar, &skipScanButton, &noticeLabel }) addAndMakeVisible (component);
    viewport.setViewedComponent (&rowsHolder, false); viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    scanLabel.setFont (theme::font (11)); noticeLabel.setFont (theme::font (11));
    noticeLabel.setColour (juce::Label::textColourId, theme::warning);
    skipScanButton.onClick = [this] { engine.skipCurrentScanFile(); };
    engine.onScanProgress = [this] (int current, int total, juce::String name)
    {
        scanProgress = total > 0 ? (double) (current - 1) / total : 0;
        scanLabel.setText ("Scanning " + juce::String (current) + "/" + juce::String (total) + " - " + name,
                           juce::dontSendNotification); updateScanUi();
    };
    engine.onScanFinished = [this] { rebuildRows(); refreshPluginPicker(); updateScanUi(); };
    rebuildRows(); updateScanUi(); startTimer (1000);
}

PluginListView::~PluginListView()
{
    stopTimer(); engine.onScanProgress = nullptr; engine.onScanFinished = nullptr;
    pickerWindow.reset(); editors.clear();
}
void PluginListView::timerCallback() { if (isShowing()) for (auto* row : rows) row->repaint(); }
void PluginListView::refreshProcessingState()
{
    bypassButton.setToggleState (engine.isMasterBypassed(), juce::dontSendNotification);
    bypassButton.setButtonText (engine.isMasterBypassed() ? "[ DRY MIC ]" : "[ BYPASS ]");
}

void PluginListView::paint (juce::Graphics& g)
{
    theme::card (g, getLocalBounds().toFloat());
    g.setColour (theme::text); g.setFont (theme::font (16, true));
    g.drawText ("[ EFFECT CHAIN ]", 14, 8, 240, 26, juce::Justification::centredLeft);
    g.setColour (theme::muted); g.setFont (theme::font (12));
    g.drawText (juce::String (rows.size()).paddedLeft ('0', 2) + " STAGES // TOP TO BOTTOM",
                14, 34, getWidth() - 28, 20, juce::Justification::centredLeft);
    if (rows.isEmpty())
    {
        auto empty = viewport.getBounds().reduced (1);
        theme::card (g, empty.toFloat());
        g.setColour (theme::accent); g.setFont (theme::font (32));
        g.drawText ("+", empty.removeFromTop (empty.getHeight() / 2), juce::Justification::centredBottom);
        g.setColour (theme::text); g.setFont (theme::font (16, true));
        g.drawText ("[ NO EFFECTS LOADED ]", empty.removeFromTop (34), juce::Justification::centred);
        g.setColour (theme::muted); g.setFont (theme::font (13));
        g.drawFittedText ("Add EQ, compression or noise suppression.\nYour dry microphone passes through until then.",
                         empty.removeFromTop (48), juce::Justification::centred, 2);
    }
}

void PluginListView::resized()
{
    auto r = getLocalBounds().reduced (14, 0); r.removeFromTop (58);
    auto toolbar = r.removeFromTop (32);
    addButton.setBounds (toolbar.removeFromLeft (114)); toolbar.removeFromLeft (8);
    foldersButton.setBounds (toolbar.removeFromLeft (116));
    bypassButton.setBounds (toolbar.removeFromRight (126)); r.removeFromTop (12);
    if (scanLabel.isVisible())
    {
        auto scan = r.removeFromTop (30);
        skipScanButton.setBounds (scan.removeFromRight (54).reduced (0, 2)); scan.removeFromRight (10);
        scanBar.setBounds (scan.removeFromRight (110).reduced (0, 11)); scanLabel.setBounds (scan);
        r.removeFromTop (8);
    }
    else if (noticeLabel.isVisible()) { noticeLabel.setBounds (r.removeFromTop (24)); r.removeFromTop (4); }
    r.removeFromBottom (10);
    viewport.setBounds (r);
    const int height = rows.size() * rowPitch;
    const int width = viewport.getWidth() - (height > viewport.getHeight() ? viewport.getScrollBarThickness() + 6 : 0);
    rowsHolder.setSize (juce::jmax (1, width), juce::jmax (viewport.getHeight(), height));
    for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * rowPitch, width, rowH);
}

void PluginListView::rebuildRows()
{
    const int y = viewport.getViewPositionY(); rows.clear();
    for (int i = 0; i < (int) engine.getChain().entries().size(); ++i)
    { auto* row = new Row (*this, i); rowsHolder.addAndMakeVisible (row); rows.add (row); }
    resized(); viewport.setViewPosition (0, y); repaint();
}
void PluginListView::commitChange() { engine.rebuildGraph(); engine.requestPersist(); rebuildRows(); }

void PluginListView::requestRemove (juce::uint32 id)
{
    const int index = engine.getChain().indexOf (id); if (index < 0) return;
    const auto name = engine.getChain().entries()[(size_t) index].displayName;
    juce::Component::SafePointer<PluginListView> safe (this);
    juce::NativeMessageBox::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove effect",
        "Remove \"" + name + "\" from the chain?", nullptr,
        juce::ModalCallbackFunction::create ([safe, id] (int result)
        {
            auto* self = safe.getComponent(); if (self == nullptr || result == 0) return;
            const int current = self->engine.getChain().indexOf (id); if (current < 0) return;
            for (int i = self->editors.size(); --i >= 0;) if (self->editors[i]->entryId == id) self->editors.remove (i);
            self->engine.getChain().removePlugin (current); self->commitChange();
        }));
}

void PluginListView::requestMove (juce::uint32 id, int destination)
{
    juce::Component::SafePointer<PluginListView> safe (this);
    juce::MessageManager::callAsync ([safe, id, destination]
    {
        if (auto* self = safe.getComponent())
        { self->engine.getChain().movePlugin (self->engine.getChain().indexOf (id), destination); self->commitChange(); }
    });
}
void PluginListView::toggleBypass (juce::uint32 id)
{
    juce::Component::SafePointer<PluginListView> safe (this);
    juce::MessageManager::callAsync ([safe, id]
    {
        if (auto* self = safe.getComponent())
        {
            const int index = self->engine.getChain().indexOf (id); if (index < 0) return;
            self->engine.getChain().setBypass (index, ! self->engine.getChain().entries()[(size_t) index].bypassed);
            self->commitChange();
        }
    });
}
void PluginListView::showRowMenu (juce::uint32 id, juce::Component* target)
{
    const int index = engine.getChain().indexOf (id); if (index < 0) return;
    juce::PopupMenu menu;
    menu.addItem (1, "Move up", index > 0); menu.addItem (2, "Move down", index + 1 < (int) engine.getChain().entries().size());
    menu.addSeparator(); menu.addItem (3, "Remove effect...");
    juce::Component::SafePointer<PluginListView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, id] (int result)
    {
        if (auto* self = safe.getComponent())
        { const int current = self->engine.getChain().indexOf (id);
          if (result == 3) self->requestRemove (id);
          else if (result == 1 || result == 2) self->requestMove (id, current + (result == 1 ? -1 : 1)); }
    });
}

juce::Array<juce::PluginDescription> PluginListView::pickerChoices() const
{
    juce::Array<juce::PluginDescription> choices;
    juce::PluginDescription mono, stereo;
    mono.name = "Mono to stereo"; mono.manufacturerName = "Built-in"; mono.fileOrIdentifier = PluginChain::monoToStereoId;
    stereo.name = "Stereo to mono"; stereo.manufacturerName = "Built-in"; stereo.fileOrIdentifier = PluginChain::stereoToMonoId;
    choices.add (mono); choices.add (stereo);
    for (const auto& type : engine.getKnownPlugins().getTypes()) if (! type.isInstrument) choices.add (type);
    return choices;
}

void PluginListView::refreshPluginPicker()
{
    if (pickerWindow != nullptr)
        if (auto* picker = dynamic_cast<PluginPickerComponent*> (pickerWindow->getContentComponent()))
            picker->setChoices (pickerChoices());
}

void PluginListView::showPluginPicker()
{
    if (pickerWindow != nullptr)
    {
        refreshPluginPicker();
        pickerWindow->setVisible (true); pickerWindow->toFront (true);
        if (auto* picker = dynamic_cast<PluginPickerComponent*> (pickerWindow->getContentComponent())) picker->focusSearch();
        return;
    }
    juce::Component::SafePointer<PluginListView> safe (this);
    auto* picker = new PluginPickerComponent (pickerChoices(),
        [safe] (const juce::PluginDescription& chosen)
        { if (auto* self = safe.getComponent()) { self->pickerWindow->setVisible (false); self->addFromPicker (chosen); } },
        [safe] { if (auto* self = safe.getComponent()) self->pickerWindow->setVisible (false); });
    pickerWindow = std::make_unique<PickerWindow> (picker);
    pickerWindow->setVisible (true); pickerWindow->toFront (true); picker->focusSearch();
}

void PluginListView::addFromPicker (const juce::PluginDescription& description)
{
    if (description.fileOrIdentifier == PluginChain::monoToStereoId) engine.getChain().addMonoToStereo();
    else if (description.fileOrIdentifier == PluginChain::stereoToMonoId) engine.getChain().addStereoToMono();
    else
    {
        auto* device = engine.getDeviceManager().getCurrentAudioDevice(); juce::String error;
        if (! engine.getChain().addPlugin (engine.getFormatManager(), description,
            device != nullptr ? device->getCurrentSampleRate() : 48000.0,
            device != nullptr ? device->getCurrentBufferSizeSamples() : 480, error))
        { juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Could not load " + description.name, error.isEmpty() ? "The effect could not be opened." : error); return; }
    }
    commitChange(); viewport.setViewPosition (0, juce::jmax (0, rowsHolder.getHeight() - viewport.getHeight()));
}

void PluginListView::showFolderMenu()
{
    juce::PopupMenu menu; menu.addItem (1, "Add VST3 folder..."); menu.addSeparator();
    const auto folders = engine.getPluginFolders();
    for (int i = 0; i < folders.size(); ++i)
    { juce::PopupMenu sub; sub.addItem (1000 + i, "Remove from library"); menu.addSubMenu (folders[i], sub); }
    menu.addItem (2, "Rescan all plugins", ! engine.isScanning());
    menu.addItem (3, "Retry skipped plugins", ! engine.isScanning() && ! engine.getSkippedPlugins().isEmpty());
    menu.addItem (4, "Retry missing effects");
    juce::Component::SafePointer<PluginListView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&foldersButton), [safe, folders] (int result)
    {
        auto* self = safe.getComponent(); if (self == nullptr) return;
        if (result == 1) self->chooseFolder();
        else if (result == 2) self->engine.rescanAllPlugins();
        else if (result == 3) self->engine.retrySkippedPlugins();
        else if (result == 4) { self->engine.retryMissingPlugins(); self->commitChange(); }
        else if (juce::isPositiveAndBelow (result - 1000, folders.size()))
        { self->engine.removePluginFolder (folders[result - 1000]); self->engine.requestPersist(); }
        self->refreshPluginPicker(); self->updateScanUi();
    });
}

void PluginListView::chooseFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Choose a VST3 folder");
    juce::Component::SafePointer<PluginListView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe, chooser] (const juce::FileChooser& result)
        { if (auto* self = safe.getComponent()) if (result.getResult().isDirectory())
          { self->engine.addPluginFolder (result.getResult().getFullPathName()); self->engine.requestPersist(); } });
}

void PluginListView::openEditor (juce::uint32 id)
{
    const int index = engine.getChain().indexOf (id); if (index < 0) return;
    const auto& effect = engine.getChain().entries()[(size_t) index];
    if (effect.isUnavailable()) { engine.retryMissingPlugins(); commitChange(); return; }
    if (effect.isBuiltIn()) return;
    for (auto* editor : editors) if (editor->entryId == id) { editor->setVisible (true); editor->toFront (true); return; }
    auto* node = engine.getGraph().getNodeForId (effect.node); if (node == nullptr) return;
    auto* processor = node->getProcessor();
    auto* content = processor->hasEditor() ? processor->createEditorAndMakeActive() : new juce::GenericAudioProcessorEditor (*processor);
    if (content == nullptr) return;
    juce::Component::SafePointer<PluginListView> safe (this);
    auto* window = new EditorWindow (effect.displayName, id, [safe] (EditorWindow* closed)
    { if (auto* self = safe.getComponent()) { self->engine.requestPersist(); self->editors.removeObject (closed); } });
    theme::window (*window); window->setContentOwned (content, true);
    window->centreWithSize (window->getWidth(), window->getHeight()); window->setVisible (true); window->toFront (true);
    editors.add (window);
}

void PluginListView::updateScanUi()
{
    const bool scanning = engine.isScanning();
    // Cached plugins and built-ins remain usable while the scanner works.
    scanLabel.setVisible (scanning); scanBar.setVisible (scanning); skipScanButton.setVisible (scanning);
    const auto& skipped = engine.getSkippedPlugins(); noticeLabel.setVisible (! scanning && ! skipped.isEmpty());
    noticeLabel.setText (juce::String (skipped.size()) + (skipped.size() == 1 ? " plugin skipped." : " plugins skipped.")
                        + " Open Library to retry.", juce::dontSendNotification);
    juce::String details; for (const auto& item : skipped) details << juce::File (item.file).getFileNameWithoutExtension() << ": " << item.reason << "\n";
    noticeLabel.setTooltip (details.trimEnd()); resized();
}
