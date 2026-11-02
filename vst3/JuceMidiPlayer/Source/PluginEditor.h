#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ExpressiveMidiPlayerEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    ExpressiveMidiPlayerProcessor& processor;
    juce::TextButton loadButton { "Load MIDI..." };
    juce::TextButton reloadButton { "Reload" };
    juce::Label status;
    juce::Label offsetLabel;
    juce::Slider offsetSlider;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerEditor)
};
