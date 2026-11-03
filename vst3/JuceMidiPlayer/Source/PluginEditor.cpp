#include "PluginEditor.h"

ExpressiveMidiPlayerEditor::ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(modeSelect);
    modeLabel.setText("Source", juce::dontSendNotification);
    modeSelect.addItem("Live MIDI on this track", 1);
    modeSelect.addItem("External MIDI file (legacy)", 2);
    modeSelect.setSelectedId(processor.getMode() + 1, juce::dontSendNotification);
    modeSelect.onChange = [this]
    {
        processor.setMode(modeSelect.getSelectedId() - 1);
        updateModeVisibility();
    };
    addAndMakeVisible(status);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(strengthLabel);
    addAndMakeVisible(strengthSlider);
    strengthLabel.setText("Humanize strength", juce::dontSendNotification);
    strengthSlider.setRange(0, 100, 1);
    strengthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    strengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 25);
    strengthSlider.setValue(processor.getStrength() * 100, juce::dontSendNotification);
    strengthSlider.onValueChange = [this] { processor.setStrength(strengthSlider.getValue() / 100.0); };
    addAndMakeVisible(timingLabel);
    addAndMakeVisible(timingSlider);
    timingLabel.setText("Timing (ms late)", juce::dontSendNotification);
    timingSlider.setRange(0, 40, 1);
    timingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 25);
    timingSlider.setValue(processor.getTimingMs(), juce::dontSendNotification);
    timingSlider.onValueChange = [this] { processor.setTimingMs(timingSlider.getValue()); };
    addAndMakeVisible(velocityLabel);
    addAndMakeVisible(velocitySlider);
    velocityLabel.setText("Velocity range (+/-)", juce::dontSendNotification);
    velocitySlider.setRange(0, 32, 1);
    velocitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 25);
    velocitySlider.setValue(processor.getVelocityRange(), juce::dontSendNotification);
    velocitySlider.onValueChange = [this] { processor.setVelocityRange(static_cast<int>(velocitySlider.getValue())); };
    addAndMakeVisible(infoLabel);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setText("Rule-based live processing, NOT Pianist Transformer AI.", juce::dontSendNotification);
    addAndMakeVisible(loadButton);
    addAndMakeVisible(reloadButton);
    addAndMakeVisible(offsetLabel);
    addAndMakeVisible(offsetSlider);
    offsetLabel.setText("Start offset (beats)", juce::dontSendNotification);
    offsetSlider.setRange(0.0, 512.0, 0.25);
    offsetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    offsetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
    offsetSlider.setValue(processor.getStartBeats(), juce::dontSendNotification);
    offsetSlider.onValueChange = [this] { processor.setStartBeats(offsetSlider.getValue()); };
    reloadButton.onClick = [this]
    {
        juce::String error;
        if (processor.reloadLastMidi(error))
            status.setText("Reloaded: " + processor.loadedFileName(), juce::dontSendNotification);
        else status.setText(error, juce::dontSendNotification);
    };
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
                else status.setText(error, juce::dontSendNotification);
            });
    };
    setSize(520, 302);
    updateModeVisibility();
}

void ExpressiveMidiPlayerEditor::updateModeVisibility()
{
    const bool live = processor.getMode() == 0;
    strengthLabel.setVisible(live);
    strengthSlider.setVisible(live);
    timingLabel.setVisible(live);
    timingSlider.setVisible(live);
    velocityLabel.setVisible(live);
    velocitySlider.setVisible(live);
    infoLabel.setVisible(live);
    loadButton.setVisible(!live);
    reloadButton.setVisible(!live);
    offsetLabel.setVisible(!live);
    offsetSlider.setVisible(!live);
    status.setText(live ? "Track MIDI in -> MIDI out -> route to piano instrument track"
                        : (processor.loadedFileName().isEmpty() ? "No file loaded" : processor.loadedFileName()),
                   juce::dontSendNotification);
}

void ExpressiveMidiPlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
    g.drawText("Expressive MIDI Bridge - MIDI only (silent)", 12, 10,
               getWidth()-24, 22, juce::Justification::left);
}

void ExpressiveMidiPlayerEditor::resized()
{
    modeLabel.setBounds(12, 39, 62, 30);
    modeSelect.setBounds(80, 40, getWidth()-92, 28);
    status.setBounds(12, 78, getWidth()-24, 27);
    strengthLabel.setBounds(12, 118, 163, 30);
    strengthSlider.setBounds(179, 118, getWidth()-191, 30);
    timingLabel.setBounds(12, 156, 163, 30);
    timingSlider.setBounds(179, 156, getWidth()-191, 30);
    velocityLabel.setBounds(12, 194, 163, 30);
    velocitySlider.setBounds(179, 194, getWidth()-191, 30);
    infoLabel.setBounds(12, 240, getWidth()-24, 40);
    loadButton.setBounds(12, 118, getWidth()-118, 30);
    reloadButton.setBounds(getWidth()-99, 118, 87, 30);
    offsetLabel.setBounds(12, 166, 150, 30);
    offsetSlider.setBounds(165, 166, getWidth()-177, 34);
}
