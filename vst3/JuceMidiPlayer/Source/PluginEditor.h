#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UiStyle.h"
#include "Widgets.h"

// One screen, top to bottom:
//   [AI setup card - only until the AI is installed]
//   [drop zone for the .mid]  [expression slider]
//   [before/after preview + status]  [drag the result onto the track]
// No tabs, no modes. Text follows the OS language (Japanese or English).
class ExpressiveMidiPlayerEditor final : public juce::AudioProcessorEditor,
                                         public juce::FileDragAndDropTarget,
                                         private juce::Timer,
                                         private juce::ChangeListener
{
public:
    explicit ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor&);
    ~ExpressiveMidiPlayerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void openChooser();
    void loadFile(const juce::File&);
    juce::String stageText() const;
    juce::String errorText(const juce::String& key) const;

    ExpressiveMidiPlayerProcessor& processor;
    expressive::AiSession& session;
    ui::Lang tr;
    bool dragHover = false;
    bool aiReady = false;
    juce::String message;   // load errors

    juce::Label setupTitle, setupText, setupStatus;
    juce::TextButton setupButton;
    double setupProgress = -1.0;
    juce::ProgressBar setupBar { setupProgress };

    juce::Rectangle<int> setupArea, dropArea;
    juce::Label dropTitle, dropText;
    juce::Label strengthLabel;
    juce::Slider strengthSlider;
    juce::ToggleButton alignToggle;
    PianoRollPreview preview;
    juce::Label status;
    DragToDawHandle dragHandle;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 500 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerEditor)
};
