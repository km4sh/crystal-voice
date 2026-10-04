#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"

class PluginListView : public juce::Component, private juce::Timer
{
public:
    explicit PluginListView (AudioEngine&);
    ~PluginListView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshProcessingState();
private:
    static constexpr int rowH = 68, rowPitch = 78;
    void timerCallback() override;
    void showPluginPicker();
    juce::Array<juce::PluginDescription> pickerChoices() const;
    void refreshPluginPicker();
    void addFromPicker (const juce::PluginDescription&);
    void showFolderMenu();
    void chooseFolder();
    void openEditor (juce::uint32 id);
    void rebuildRows();
    void commitChange();
    void requestRemove (juce::uint32 id);
    void requestMove (juce::uint32 id, int destination);
    void toggleBypass (juce::uint32 id);
    void showRowMenu (juce::uint32 id, juce::Component* target);
    void updateScanUi();
    void layoutRows (bool animate);
    struct Row : juce::Component, juce::SettableTooltipClient
    {
        Row (PluginListView&, int index);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        PluginListView& owner;
        juce::uint32 id;
        int index, grabOffsetY = 0;
        bool dragging = false;
        juce::TextButton openButton { "[ EDIT ]" }, enabledButton { "ON" }, moreButton { "..." };
    };
    void beginDrag (Row&, const juce::MouseEvent&);
    void updateDrag (const juce::MouseEvent&);
    void updateDragPosition();
    void finishDrag();
    void cancelDrag (bool animate);
    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (const juce::String&, juce::uint32, std::function<void (EditorWindow*)>);
        void closeButtonPressed() override;
        juce::uint32 entryId;
        std::function<void (EditorWindow*)> onClose;
    };
    AudioEngine& engine;
    juce::TextButton addButton { "[ + ADD ]" }, foldersButton { "[ LIBRARY ]" },
        bypassButton { "[ BYPASS ]" }, skipScanButton { "Skip" };
    juce::Label scanLabel, noticeLabel;
    double scanProgress = 0.0;
    juce::ProgressBar scanBar { scanProgress };
    juce::Viewport viewport;
    juce::Component rowsHolder;
    juce::OwnedArray<Row> rows;
    juce::ComponentAnimator rowAnimator;
    int dragSource = -1, dragDestination = -1, dragPointerY = 0;
    juce::OwnedArray<EditorWindow> editors;
    std::unique_ptr<juce::DocumentWindow> pickerWindow;
};
