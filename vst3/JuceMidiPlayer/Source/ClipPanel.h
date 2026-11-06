#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UiStyle.h"

// Before/after view of the clip: grey outlines = original, filled = result (brightness =
// velocity). Velocity lane at the bottom. Purely visual.
class PianoRollPreview final : public juce::Component
{
public:
    void setClips(const expressive::Clip* original, const expressive::Clip* rendered)
    {
        source = original;
        result = rendered;
        repaint();
    }
    void setEmptyText(const juce::String& t) { emptyText = t; repaint(); }
    void paint(juce::Graphics&) override;
private:
    const expressive::Clip* source = nullptr;
    const expressive::Clip* result = nullptr;
    juce::String emptyText;
};

// A drag source that hands a freshly written .mid file to the host (drag & drop to a track).
class DragToDawHandle final : public juce::Component
{
public:
    std::function<juce::File()> prepareFile;   // returns the file to drag, or {} on failure
    void setText(const juce::String& title, const juce::String& subtitle);
    void setActive(bool a) { active = a; setMouseCursor(a ? juce::MouseCursor::DraggingHandCursor
                                                          : juce::MouseCursor::NormalCursor); repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging = false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    juce::String title, subtitle;
    bool active = false, hover = false, dragging = false;
};

// The recommended workflow: 1) bring MIDI in, 2) choose how it is played, 3) drag it back.
class ClipPanel final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    ClipPanel(ExpressiveMidiPlayerProcessor&, ui::Lang&);
    ~ClipPanel() override;
    void refreshTexts();
    void loadFile(const juce::File&);
    void paint(juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredHeight = 600;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshState();
    void refreshAiSettingsFields();
    void showError(const juce::String& key);
    juce::String errorText(const juce::String& key) const;
    juce::String sourceSummary() const;
    juce::String resultSummary() const;
    juce::String aiStatusText() const;
    juce::String barText(double beat) const;
    void pushRuleSettings();

    ExpressiveMidiPlayerProcessor& processor;
    expressive::ClipSession& session;
    ui::Lang& tr;
    juce::String message;
    bool messageIsError = false;
    bool settingsVisible = false;
    juce::Rectangle<int> card1, card2, card3;

    // step 1
    juce::Label step1Title, sourceInfo, sourceHelp;
    juce::TextButton openButton, captureButton, changeSourceButton;
    // step 2
    juce::Label step2Title;
    juce::TextButton ruleTab, aiTab;
    std::array<juce::TextButton, 3> styleButtons;
    juce::Label strengthLabel, strengthHelp;
    juce::Slider strengthSlider;
    juce::TextButton rerollButton;
    juce::Label aiStrengthLabel, pedalLabel, aiStatus;
    juce::Slider aiStrengthSlider;
    juce::ComboBox pedalBox;
    juce::TextButton runAiButton, aiSettingsButton;
    // AI settings (replaces step 2+3 while open)
    juce::Label settingsTitle, repoLabel, pythonLabel, deviceLabel, settingsHelp, checkStatus;
    juce::TextEditor repoField, pythonField;
    juce::TextButton repoBrowse, pythonBrowse, checkButton, closeSettings;
    juce::ComboBox deviceBox;
    // step 3
    juce::Label step3Title, resultInfo, placementHelp;
    PianoRollPreview preview;
    DragToDawHandle dragHandle;
    juce::TextButton saveButton, revealButton;
    std::unique_ptr<juce::FileChooser> chooser;
};
