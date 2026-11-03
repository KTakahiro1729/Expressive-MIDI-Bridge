#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

class ExpressiveMidiPlayerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    ExpressiveMidiPlayerProcessor& processor;
    juce::ComboBox modeSelect, handMappingSelect;
    juce::Label modeLabel, handMappingLabel, splitLabel, status, infoLabel;
    juce::Slider splitSlider;
    std::array<juce::Label, 2> handTitle;
    std::array<juce::Slider, 2> strengthSlider, timingSlider, velocitySlider;
    juce::TextButton loadButton { "Load MIDI..." };
    juce::TextButton reloadButton { "Reload" };
    juce::Label offsetLabel;
    juce::Slider offsetSlider;
    std::unique_ptr<juce::FileChooser> chooser;
    void updateModeVisibility();
    void paintHandControls(juce::Graphics&, int handIndex);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerEditor)
};
