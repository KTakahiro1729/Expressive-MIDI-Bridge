#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ClipPanel.h"
#include "UiStyle.h"
#include <array>
#include <cstdint>

// The editor is organised top-to-bottom in the order a user needs it:
//   1. what this plugin is (it is silent and only outputs MIDI),
//   2. which mode is active (two large tabs instead of a dropdown),
//   3. a live signal-flow panel with MIDI IN/OUT lamps and a context-sensitive hint,
//   4. the controls for the active mode, each with a plain-language explanation,
//   5. an optional step-by-step setup guide.
// All strings exist in English and Japanese; the language follows the OS until changed.
class ExpressiveMidiPlayerEditor final : public juce::AudioProcessorEditor,
                                         public juce::FileDragAndDropTarget,
                                         private juce::Timer
{
public:
    explicit ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor&);
    ~ExpressiveMidiPlayerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    enum class Tone { neutral, ok, warning, error };
    struct Preset { const char* en; const char* ja; double strength, timing; int velocity; };

    void timerCallback() override;
    void refreshTexts();
    void refreshVisibility();
    void refreshStatus();
    void refreshDescriptions();
    void refreshPresetHighlight();
    void selectMode(int mode);
    void applyPreset(const Preset&);
    void loadFile(const juce::File&);
    void showMessage(const juce::String& text, Tone tone);
    void setupSlider(juce::Slider&, double min, double max, double step, double defaultValue,
                     const juce::String& suffix);
    juce::String tr(const char* en, const char* ja) const;
    juce::String fileSummary() const;
    juce::String guideText() const;
    bool isLive() const { return processor.getMode() == ExpressiveMidiPlayerProcessor::modeLive; }
    bool isClip() const { return processor.getMode() == ExpressiveMidiPlayerProcessor::modeClip; }
    void updateSizeForMode();

    ExpressiveMidiPlayerProcessor& processor;
    bool japanese = false;
    ui::Lang lang;
    bool dragHover = false;
    bool guideVisible = false;

    // Activity lamps: lit for a short time after the note counters change.
    std::uint32_t lastIn = 0, lastOut = 0;
    std::uint32_t lastInMs = 0, lastOutMs = 0;
    bool inLit = false, outLit = false, everIn = false, everOut = false;

    // Transient message (load errors, reloads); shown instead of the hint for a few seconds.
    juce::String message;
    Tone messageTone = Tone::neutral;
    std::uint32_t messageUntilMs = 0;

    juce::String transportText, hintText;
    Tone hintTone = Tone::neutral;

    juce::TextButton languageButton, guideButton;
    juce::TextButton clipTab, liveTab, fileTab;
    std::unique_ptr<ClipPanel> clipPanel;
    juce::Label hintLabel;

    juce::Label presetLabel;
    static const std::array<Preset, 4> presets;
    std::array<juce::TextButton, 4> presetButtons;
    juce::Label strengthLabel, timingLabel, velocityLabel;
    juce::Label strengthHelp, timingHelp, velocityHelp, liveFooter;
    juce::Slider strengthSlider, timingSlider, velocitySlider;

    juce::TextButton loadButton, reloadButton;
    juce::Label fileInfo, offsetLabel, offsetHelp, dropHint;
    juce::Slider offsetSlider;

    juce::TextEditor guide;
    juce::TooltipWindow tooltips { this, 500 };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> statusArea, flowArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerEditor)
};
