#include "ui/PluginListView.h"
#include "ui/PluginPicker.h"
#include "ui/Theme.h"
#include "audio/isolation/IsolatedPlugin.h"

namespace
{
    IsolatedPlugin* isolatedFor (AudioEngine& engine, const PluginChain::Entry& entry)
    {
        auto node = engine.getGraph().getNodeForId (entry.node);
        return node != nullptr ? dynamic_cast<IsolatedPlugin*> (node->getProcessor()) : nullptr;
    }
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
    auto* isolated = isolatedFor (owner.engine, effect);
    const bool failed = isolated != nullptr && isolated->failed();
    setName (effect.displayName);
    setWantsKeyboardFocus (true);
    setViewportIgnoreDragFlag (true);
    setTooltip (effect.displayName + "\n" + (effect.isUnavailable() ? effect.error : failed ? isolated->failureReason()
        : isolated != nullptr ? "Independent plugin process: " + juce::String (isolated->workerProcessId()) : effect.manufacturer));
    enabledButton.setClickingTogglesState (true);
    enabledButton.setToggleState (! effect.bypassed, juce::dontSendNotification);
    enabledButton.setButtonText (effect.bypassed ? "OFF" : "ON");
    enabledButton.setEnabled (! effect.isUnavailable());
    enabledButton.setTooltip ("Enable or bypass this effect");
    enabledButton.onClick = [this] { owner.toggleBypass (id); };
    openButton.onClick = [this] { owner.openEditor (id); };
    openButton.setTooltip (effect.isUnavailable() ? effect.error : failed ? isolated->failureReason() : "Open the effect editor");
    openButton.setButtonText (effect.isUnavailable() ? "Retry" : failed ? "Reload" : "[ EDIT ]");
    if (failed) openButton.setColour (juce::TextButton::textColourOffId, theme::danger);
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
    auto* isolated = isolatedFor (owner.engine, effect);
    const bool failed = isolated != nullptr && isolated->failed();
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (dragging ? theme::raised : theme::background); g.fillRect (bounds);
    g.setColour (dragging || isMouseOver (true) ? theme::accent.withAlpha (0.6f) : theme::border); g.drawRect (bounds, 1);
    const auto stateColour = failed ? theme::danger : effect.isUnavailable() ? theme::warning : effect.bypassed ? theme::muted : theme::accent;
    g.setColour (stateColour); g.fillRect (0, 0, 2, getHeight());
    g.setColour (theme::muted.withAlpha (0.6f));
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row) g.fillRect (11.0f + column * 5, 26.0f + row * 6, 2.0f, 2.0f);
    g.setColour (stateColour);
    g.setFont (theme::font (13, true));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), 27, 16, 28, 36, juce::Justification::centred);
    const int right = openButton.getX() - 10;
    g.setFont (theme::font (15, true));
    g.setColour (effect.bypassed ? theme::muted : theme::text);
    g.drawText (effect.displayName, 64, 10, juce::jmax (1, right - 64), 24, juce::Justification::centredLeft);
    juce::String detail = failed ? "Process failed - settings retained" : effect.isUnavailable() ? "Unavailable - settings preserved" : effect.manufacturer;
    if (auto* node = owner.engine.getGraph().getNodeForId (effect.node); node != nullptr && ! failed)
    {
        auto* processor = node->getProcessor();
        auto* device = owner.engine.getDeviceManager().getCurrentAudioDevice();
        const double rate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
        detail << " / " << processor->getMainBusNumInputChannels() << ">" << processor->getMainBusNumOutputChannels()
               << " / " << juce::String (rate > 0 ? processor->getLatencySamples() * 1000.0 / rate : 0, 1) << " ms";
        if (isolated != nullptr) detail << " / isolated";
    }
    g.setColour (failed ? theme::danger : effect.isUnavailable() ? theme::warning : theme::muted); g.setFont (theme::font (11));
    g.drawText (detail, 64, 36, juce::jmax (1, right - 64), 20, juce::Justification::centredLeft);
}

void PluginListView::Row::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < 28 && e.mods.isLeftButtonDown()) owner.beginDrag (*this, e);
}
void PluginListView::Row::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) owner.updateDrag (e);
}
void PluginListView::Row::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    owner.finishDrag();
}
void PluginListView::Row::mouseDoubleClick (const juce::MouseEvent& e) { if (e.x >= 28) owner.openEditor (id); }
bool PluginListView::Row::keyPressed (const juce::KeyPress& key)
{
    if (dragging && key == juce::KeyPress::escapeKey) { owner.cancelDrag (true); return true; }
    return false;
}

void PluginListView::beginDrag (Row& row, const juce::MouseEvent& event)
{
    if (rows.size() < 2) return;
    cancelDrag (false);
    dragSource = rows.indexOf (&row); dragDestination = dragSource;
    rowAnimator.cancelAnimation (&row, false);
    row.dragging = true; row.grabOffsetY = event.y;
    dragPointerY = event.getEventRelativeTo (&viewport).y;
    if (row.isShowing()) row.grabKeyboardFocus();
    row.toFront (false); row.repaint(); startTimer (16);
}

void PluginListView::updateDrag (const juce::MouseEvent& event)
{
    dragPointerY = event.getEventRelativeTo (&viewport).y;
    updateDragPosition();
}

void PluginListView::updateDragPosition()
{
    if (! juce::isPositiveAndBelow (dragSource, rows.size())) return;
    auto* row = rows[dragSource];
    const int y = juce::jlimit (0, (rows.size() - 1) * rowPitch,
                               viewport.getViewPositionY() + dragPointerY - row->grabOffsetY);
    row->setTopLeftPosition (0, y);
    const int destination = (y + rowPitch / 2) / rowPitch;
    if (destination != dragDestination) { dragDestination = destination; layoutRows (true); }
}

void PluginListView::finishDrag()
{
    const auto id = rows[dragSource]->id;
    const int destination = dragDestination;
    rows[dragSource]->dragging = false; rows[dragSource]->repaint();
    dragSource = dragDestination = -1; startTimer (1000);
    requestMove (id, destination);
}

void PluginListView::cancelDrag (bool animate)
{
    if (dragSource < 0) return;
    for (auto* row : rows) { row->dragging = false; row->repaint(); }
    dragSource = dragDestination = -1; startTimer (1000);
    layoutRows (animate);
}

PluginListView::EditorWindow::EditorWindow (const juce::String& title, juce::uint32 id,
                                           std::function<void (EditorWindow*)> close)
    : DocumentWindow (title, theme::background, closeButton), entryId (id), onClose (std::move (close)) {}
void PluginListView::EditorWindow::closeButtonPressed() { const auto callback = onClose; if (callback) callback (this); }

PluginListView::PluginListView (AudioEngine& e) : engine (e)
{
    theme::primary (addButton);
    addButton.onClick = [this] { showPluginPicker(); };
    foldersButton.onClick = [this] { showFolderMenu(); };
    presetsButton.onClick = [this] { showPresetMenu(); };
    presetsButton.setColour (juce::TextButton::textColourOffId, theme::cyan);
    engine.onChainReplacing = [this] { cancelDrag (false); editors.clear(); };
    engine.onPluginStatusChanged = [this] { rebuildRows(); };
    foldersButton.setColour (juce::TextButton::textColourOffId, theme::cyan);
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Compare with your dry microphone. Mute still applies.");
    bypassButton.setColour (juce::TextButton::textColourOnId, theme::warning);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, theme::warning.withAlpha (0.12f));
    bypassButton.onClick = [this] { engine.setMasterBypass (bypassButton.getToggleState()); refreshProcessingState(); };
    refreshProcessingState();
    foldersButton.setTooltip ("Plugin folders, scanning and recovery");
    for (auto* component : std::initializer_list<juce::Component*> {
        &addButton, &foldersButton, &presetsButton, &bypassButton, &viewport, &scanLabel, &scanBar, &skipScanButton, &noticeLabel }) addAndMakeVisible (component);
    viewport.setViewedComponent (&rowsHolder, false); viewport.setScrollBarsShown (true, false);
    viewport.setComponentID ("effect-viewport"); rowsHolder.setComponentID ("effect-rows");
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
    engine.onChainReplacing = nullptr;
    engine.onPluginStatusChanged = nullptr;
    rowAnimator.cancelAllAnimations (false);
    pickerWindow.reset(); editors.clear();
}
void PluginListView::timerCallback()
{
    if (dragSource >= 0)
    {
        if (! isShowing()) { cancelDrag (false); return; }
        if (viewport.autoScroll (viewport.getWidth() / 2, dragPointerY, 28, 10)) updateDragPosition();
    }
    else if (isShowing())
    {
        for (auto* row : rows) row->repaint();
        presetsButton.setTooltip ("Current: " + (engine.getCurrentPresetName().isEmpty() ? juce::String ("Unsaved chain") : engine.getCurrentPresetName())
            + (engine.isPresetModified() ? " (modified)" : "") + "\nStartup: "
            + (engine.getStartupPresetName().isEmpty() ? juce::String ("Last session") : engine.getStartupPresetName()));
        repaint (14, 34, getWidth() - 28, 20);
    }
}
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
    const auto presetName = engine.getCurrentPresetName();
    g.drawText (juce::String (rows.size()).paddedLeft ('0', 2) + " STAGES // "
                + (presetName.isEmpty() ? juce::String ("TOP TO BOTTOM") : presetName + (engine.isPresetModified() ? " *" : ""))
                + (engine.getStartupPresetName().isNotEmpty() ? " // STARTUP: " + engine.getStartupPresetName() : juce::String()),
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
    toolbar.removeFromLeft (8); presetsButton.setBounds (toolbar.removeFromLeft (116));
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
    layoutRows (false);
}

void PluginListView::layoutRows (bool animate)
{
    for (int i = 0; i < rows.size(); ++i)
    {
        auto* row = rows[i];
        int slot = i;
        if (dragSource >= 0)
        {
            slot = i < dragSource ? i : i - 1;
            if (slot >= dragDestination) ++slot;
            if (i == dragSource) slot = dragDestination;
        }
        if (row->index != slot) { row->index = slot; row->repaint(); }
        if (i == dragSource) { row->setSize (rowsHolder.getWidth(), rowH); continue; }
        const juce::Rectangle<int> target (0, slot * rowPitch, rowsHolder.getWidth(), rowH);
        if (! animate) { rowAnimator.cancelAnimation (row, false); row->setBounds (target); }
        else if (rowAnimator.getComponentDestination (row) != target)
            rowAnimator.animateComponent (row, target, 1.0f, 160, false, 1.0, 0.0);
    }
}

void PluginListView::rebuildRows()
{
    const int y = viewport.getViewPositionY();
    cancelDrag (false); rowAnimator.cancelAllAnimations (false); rows.clear();
    for (int i = 0; i < (int) engine.getChain().entries().size(); ++i)
    { auto* row = new Row (*this, i); rowsHolder.addAndMakeVisible (row); rows.add (row); }
    resized(); viewport.setViewPosition (0, y); repaint();
}
void PluginListView::commitChange() { engine.markPresetModified(); engine.rebuildGraph(); engine.requestPersist(); rebuildRows(); }

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
        {
            const int source = self->engine.getChain().indexOf (id);
            if (source < 0) return;
            const int target = juce::jlimit (0, (int) self->engine.getChain().entries().size() - 1, destination);
            if (source != target)
            {
                self->engine.getChain().movePlugin (source, target);
                self->engine.markPresetModified(); self->engine.rebuildGraph(); self->engine.requestPersist();
                for (int i = 0; i < self->rows.size(); ++i)
                    if (self->rows[i]->id == id) { self->rows.move (i, target); break; }
            }
            self->layoutRows (true);
        }
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
    juce::PopupMenu menu;
    menu.addSectionHeader ("VST3 EFFECT LIBRARY");
    juce::PopupMenu automatic;
    for (const auto& folder : engine.getDefaultPluginFolders())
    {
        const bool present = juce::File (folder).isDirectory();
        automatic.addItem (-1, folder + (present ? "" : "  [not installed]"), false, present);
    }
    menu.addSubMenu ("Automatic scan folders", automatic);
    menu.addItem (1, "Add VST3 folder...");
    menu.addSectionHeader ("CUSTOM FOLDERS");
    const auto folders = engine.getPluginFolders();
    for (int i = 0; i < folders.size(); ++i)
    { juce::PopupMenu sub; sub.addItem (1000 + i, "Remove from library"); menu.addSubMenu (folders[i], sub); }
    if (folders.isEmpty()) menu.addItem (-1, "No custom folders", false);
    menu.addSeparator();
    menu.addItem (-1, "Scans .vst3 effects; VST2 .dll is unsupported", false);
    menu.addItem (2, "Rescan all plugins", ! engine.isScanning());
    menu.addItem (3, "Retry skipped plugins", ! engine.isScanning() && ! engine.getSkippedPlugins().isEmpty());
    menu.addItem (4, "Reload failed / missing effects");
    juce::Component::SafePointer<PluginListView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&foldersButton), [safe, folders] (int result)
    {
        auto* self = safe.getComponent(); if (self == nullptr) return;
        if (result == 1) self->chooseFolder();
        else if (result == 2) self->engine.rescanAllPlugins();
        else if (result == 3) self->engine.retrySkippedPlugins();
        else if (result == 4) { self->engine.retryMissingPlugins (true); self->commitChange(); }
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

void PluginListView::reportPresetError (const juce::String& error)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Chain preset", error);
}

void PluginListView::namePreset()
{
    auto* dialog = new juce::AlertWindow ("Save chain preset", "Name this effect chain. Plugin parameters are included.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", engine.getCurrentPresetName().isEmpty() ? juce::String ("Voice chain") : engine.getCurrentPresetName(), "Name");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PluginListView> safe (this);
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe, dialog] (int result)
    {
        if (auto* self = safe.getComponent(); self != nullptr && result == 1)
        {
            juce::String error;
            if (! self->engine.savePreset (dialog->getTextEditorContents ("name"), true, error)) self->reportPresetError (error);
            self->repaint();
        }
    }), true);
}

void PluginListView::showPresetMenu()
{
    const auto presets = PresetStore().list();
    juce::PopupMenu menu, load, startup;
    menu.addSectionHeader ("CHAIN PRESETS");
    menu.addItem (1, "Save changes", engine.getCurrentPreset().isNotEmpty());
    menu.addItem (2, "Save as new preset...");
    for (int i = 0; i < presets.size(); ++i)
    {
        load.addItem (100 + i, presets[i].name, true, presets[i].id == engine.getCurrentPreset());
        startup.addItem (10000 + i, presets[i].name, true, presets[i].id == engine.getStartupPreset());
    }
    menu.addSubMenu ("Load preset", load, ! presets.isEmpty());
    menu.addSeparator();
    menu.addSubMenu ("Startup preset (saved version)", startup, ! presets.isEmpty());
    menu.addItem (4, "Use last session at startup", true, engine.getStartupPreset().isEmpty());
    menu.addSeparator(); menu.addItem (5, "Export current chain..."); menu.addItem (6, "Import preset...");
    juce::Component::SafePointer<PluginListView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton), [safe, presets] (int result)
    {
        auto* self = safe.getComponent(); if (self == nullptr || result == 0) return;
        juce::String error;
        if (result == 1)
        { if (! self->engine.savePreset (self->engine.getCurrentPresetName(), false, error)) self->reportPresetError (error); }
        else if (result == 2) self->namePreset();
        else if (result == 4)
        { if (! self->engine.setStartupPreset ({}, error)) self->reportPresetError (error); }
        else if (result == 5 || result == 6) self->choosePresetFile (result == 6);
        else if (juce::isPositiveAndBelow (result - 10000, presets.size()))
        { if (! self->engine.setStartupPreset (presets[result - 10000].id, error)) self->reportPresetError (error); }
        else if (juce::isPositiveAndBelow (result - 100, presets.size()))
        { if (! self->engine.loadPreset (presets[result - 100], error)) self->reportPresetError (error); else self->rebuildRows(); }
        self->repaint();
    });
}

void PluginListView::choosePresetFile (bool import)
{
    auto chooser = std::make_shared<juce::FileChooser> (import ? "Import chain preset" : "Export chain preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Voice chain.cvpreset"), "*.cvpreset");
    juce::Component::SafePointer<PluginListView> safe (this);
    chooser->launchAsync ((import ? juce::FileBrowserComponent::openMode : juce::FileBrowserComponent::saveMode)
                          | juce::FileBrowserComponent::canSelectFiles
                          | (import ? 0 : juce::FileBrowserComponent::warnAboutOverwriting),
        [safe, chooser, import] (const juce::FileChooser& selected)
    {
        auto* self = safe.getComponent(); const auto file = selected.getResult();
        if (self == nullptr || file == juce::File()) return;
        ChainPreset preset; juce::String error; PresetStore store;
        if (import)
        {
            if (! store.importFile (file, preset, error)) { self->reportPresetError (error); return; }
            preset.id.clear();
            if (! store.save (preset, error) || ! self->engine.loadPreset (preset, error)) self->reportPresetError (error);
            else self->rebuildRows();
        }
        else
        {
            if (! self->engine.snapshotPluginStates (true)) { self->reportPresetError ("Audio is busy. Try exporting again."); return; }
            preset.name = self->engine.getCurrentPresetName().isEmpty() ? juce::String ("Voice chain") : self->engine.getCurrentPresetName();
            preset.id = juce::Uuid().toString().removeCharacters ("-");
            preset.plugins = self->engine.captureState (false).plugins;
            if (! store.exportFile (preset, file.withFileExtension ("cvpreset"), error)) self->reportPresetError (error);
        }
        self->repaint();
    });
}

void PluginListView::openEditor (juce::uint32 id)
{
    const int index = engine.getChain().indexOf (id); if (index < 0) return;
    const auto& effect = engine.getChain().entries()[(size_t) index];
    if (effect.isUnavailable()) { engine.retryMissingPlugins (true, id); commitChange(); return; }
    if (effect.isBuiltIn()) return;
    for (auto* editor : editors) if (editor->entryId == id) { editor->setVisible (true); editor->toFront (true); return; }
    auto* node = engine.getGraph().getNodeForId (effect.node); if (node == nullptr) return;
    auto* processor = node->getProcessor();
    if (auto* isolated = dynamic_cast<IsolatedPlugin*> (processor))
    {
        if (isolated->failed()) { engine.retryMissingPlugins (true, id); commitChange(); return; }
        juce::String error;
        if (! isolated->showRemoteEditor (error)) reportPresetError (error);
        return;
    }
    auto* content = processor->hasEditor() ? processor->createEditorAndMakeActive() : new juce::GenericAudioProcessorEditor (*processor);
    if (content == nullptr) return;
    juce::Component::SafePointer<PluginListView> safe (this);
    auto* window = new EditorWindow (effect.displayName, id, [safe] (EditorWindow* closed)
    { if (auto* self = safe.getComponent()) {
        if (self->engine.snapshotPluginStates (true)) self->engine.requestPersist();
        else self->reportPresetError ("Could not capture the latest plugin parameters. Try Save changes again.");
        self->editors.removeObject (closed);
    } });
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
