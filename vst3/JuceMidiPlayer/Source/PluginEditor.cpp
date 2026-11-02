#include "PluginEditor.h"

ExpressiveMidiPlayerEditor::ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(loadButton);
    addAndMakeVisible(reloadButton);
    reloadButton.onClick = [this]
    {
        juce::String error;
        if (processor.reloadLastMidi(error))
            status.setText("Reloaded: " + processor.loadedFileName(), juce::dontSendNotification);
        else
            status.setText(error, juce::dontSendNotification);
    };
    addAndMakeVisible(status);
    addAndMakeVisible(offsetLabel);
    addAndMakeVisible(offsetSlider);
    offsetLabel.setText("Start offset (beats)", juce::dontSendNotification);
    offsetSlider.setRange(0.0, 512.0, 0.25);
    offsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
    offsetSlider.setValue(processor.getStartBeats(), juce::dontSendNotification);
    offsetSlider.onValueChange = [this] { processor.setStartBeats(offsetSlider.getValue()); };
    status.setJustificationType(juce::Justification::centredLeft);
    status.setText(processor.loadedFileName().isEmpty() ? "No MIDI loaded (input passes through)"
                                                    : processor.loadedFileName(),
                   juce::dontSendNotification);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Choose performance MIDI", juce::File{}, "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& picker)
            {
                const auto file = picker.getResult();
                if (!file.existsAsFile()) return;
                juce::String error;
                if (processor.loadMidi(file, error))
                    status.setText(file.getFileName(), juce::dontSendNotification);
                else
                    status.setText(error, juce::dontSendNotification);
            });
    };
    setSize(475, 157);
}

void ExpressiveMidiPlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
    g.drawText("Expressive MIDI Player (MIDI only / no audio)", 12, 10, getWidth()-24, 22, juce::Justification::left);
}

void ExpressiveMidiPlayerEditor::resized()
{
    loadButton.setBounds(12, 39, getWidth()-118, 30);
    reloadButton.setBounds(getWidth()-99, 39, 87, 30);
    status.setBounds(12, 75, getWidth()-24, 26);
    offsetLabel.setBounds(12, 111, 150, 30);
    offsetSlider.setBounds(160, 109, getWidth()-172, 34);
}
