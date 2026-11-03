#include "PluginEditor.h"

ExpressiveMidiPlayerEditor::ExpressiveMidiPlayerEditor(ExpressiveMidiPlayerProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    addAndMakeVisible(modeLabel);
    modeLabel.setText("Source", juce::dontSendNotification);
    addAndMakeVisible(modeSelect);
    modeSelect.addItem("Live MIDI on this track", 1);
    modeSelect.addItem("External MIDI file (legacy)", 2);
    modeSelect.setSelectedId(processor.getMode() + 1, juce::dontSendNotification);
    modeSelect.onChange = [this] {
        processor.setMode(modeSelect.getSelectedId() - 1);
        updateModeVisibility();
    };

    addAndMakeVisible(handMappingLabel);
    handMappingLabel.setText("Identify hands", juce::dontSendNotification);
    addAndMakeVisible(handMappingSelect);
    handMappingSelect.addItem("By pitch (approximate)", 1);
    handMappingSelect.addItem("By MIDI channel (1=right, 2=left)", 2);
    handMappingSelect.setSelectedId(processor.getHandMapping() + 1, juce::dontSendNotification);
    handMappingSelect.onChange = [this] {
        processor.setHandMapping(handMappingSelect.getSelectedId() - 1);
        updateModeVisibility();
    };

    addAndMakeVisible(splitLabel);
    splitLabel.setText("Split MIDI note (right >=)", juce::dontSendNotification);
    addAndMakeVisible(splitSlider);
    splitSlider.setRange(21, 108, 1);
    splitSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    splitSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
    splitSlider.setValue(processor.getSplitNote(), juce::dontSendNotification);
    splitSlider.onValueChange = [this] { processor.setSplitNote(static_cast<int>(splitSlider.getValue())); };

    for (int hand = 0; hand < 2; ++hand)
    {
        addAndMakeVisible(handTitle[hand]);
        handTitle[hand].setText(hand == 0 ? "RIGHT HAND" : "LEFT HAND", juce::dontSendNotification);
        handTitle[hand].setFont(juce::Font(juce::FontOptions(17.0f)));
        for (juce::Slider* slider : { &strengthSlider[hand], &timingSlider[hand], &velocitySlider[hand] })
        {
            addAndMakeVisible(*slider);
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);
        }
        strengthSlider[hand].setRange(0, 100, 1);
        strengthSlider[hand].setValue(processor.getStrength(hand) * 100, juce::dontSendNotification);
        strengthSlider[hand].onValueChange = [this, hand] {
            processor.setStrength(hand, strengthSlider[hand].getValue() / 100.0);
        };
        timingSlider[hand].setRange(0, 40, 1);
        timingSlider[hand].setValue(processor.getTimingMs(hand), juce::dontSendNotification);
        timingSlider[hand].onValueChange = [this, hand] {
            processor.setTimingMs(hand, timingSlider[hand].getValue());
        };
        velocitySlider[hand].setRange(0, 32, 1);
        velocitySlider[hand].setValue(processor.getVelocityRange(hand), juce::dontSendNotification);
        velocitySlider[hand].onValueChange = [this, hand] {
            processor.setVelocityRange(hand, static_cast<int>(velocitySlider[hand].getValue()));
        };
    }

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
    reloadButton.onClick = [this] {
        juce::String error;
        if (processor.reloadLastMidi(error))
            status.setText("Reloaded: " + processor.loadedFileName(), juce::dontSendNotification);
        else status.setText(error, juce::dontSendNotification);
    };
    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose performance MIDI", juce::File{}, "*.mid;*.midi");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& picker) {
                const auto file = picker.getResult();
                if (!file.existsAsFile()) return;
                juce::String error;
                if (processor.loadMidi(file, error))
                    status.setText(file.getFileName(), juce::dontSendNotification);
                else status.setText(error, juce::dontSendNotification);
            });
    };
    addAndMakeVisible(status);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(infoLabel);
    infoLabel.setJustificationType(juce::Justification::centredLeft);
    infoLabel.setText("Timing delays are positive only. MIDI CC / sustain passes through unchanged.",
                      juce::dontSendNotification);
    setSize(744, 366);
    updateModeVisibility();
}

void ExpressiveMidiPlayerEditor::updateModeVisibility()
{
    const bool live = processor.getMode() == 0;
    const bool pitch = processor.getHandMapping() == 0;
    handMappingLabel.setVisible(live);
    handMappingSelect.setVisible(live);
    splitLabel.setVisible(live && pitch);
    splitSlider.setVisible(live && pitch);
    for (int hand = 0; hand < 2; ++hand)
    {
        handTitle[hand].setVisible(live);
        strengthSlider[hand].setVisible(live);
        timingSlider[hand].setVisible(live);
        velocitySlider[hand].setVisible(live);
    }
    infoLabel.setVisible(live);
    loadButton.setVisible(!live);
    reloadButton.setVisible(!live);
    offsetLabel.setVisible(!live);
    offsetSlider.setVisible(!live);
    status.setText(live ? (pitch ? "Pitch split is approximate: crossing hands can be misclassified."
                                      : "Channel 1: right; channel 2: left; other channels unchanged.")
                        : (processor.loadedFileName().isEmpty() ? "No MIDI loaded" : processor.loadedFileName()),
                   juce::dontSendNotification);
    repaint();
}

void ExpressiveMidiPlayerEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("Expressive MIDI Bridge - piano hands (MIDI only / silent)", 12, 8,
               getWidth() - 24, 25, juce::Justification::left);
    if (processor.getMode() != 0) return;
    for (int hand = 0; hand < 2; ++hand) paintHandControls(g, hand);
}

void ExpressiveMidiPlayerEditor::paintHandControls(juce::Graphics& g, int hand)
{
    const int x = 12 + hand * 369;
    const int labelWidth = 114;
    g.setColour(juce::Colours::lightgrey);
    g.setFont(13.0f);
    g.drawText("Humanize strength (%)", x, 183, labelWidth, 26, juce::Justification::centredLeft);
    g.drawText("Delay (ms)", x, 220, labelWidth, 26, juce::Justification::centredLeft);
    g.drawText("Velocity (+/-)", x, 257, labelWidth, 26, juce::Justification::centredLeft);
}

void ExpressiveMidiPlayerEditor::resized()
{
    modeLabel.setBounds(12, 40, 62, 28);
    modeSelect.setBounds(78, 40, 305, 28);
    handMappingLabel.setBounds(12, 75, 110, 28);
    handMappingSelect.setBounds(126, 75, 310, 28);
    splitLabel.setBounds(448, 75, 178, 28);
    splitSlider.setBounds(630, 75, 105, 28);
    for (int hand = 0; hand < 2; ++hand)
    {
        const int x = 12 + hand * 369;
        handTitle[hand].setBounds(x, 145, 330, 30);
        strengthSlider[hand].setBounds(x + 118, 183, 235, 27);
        timingSlider[hand].setBounds(x + 118, 220, 235, 27);
        velocitySlider[hand].setBounds(x + 118, 257, 235, 27);
    }
    loadButton.setBounds(12, 145, 580, 32);
    reloadButton.setBounds(608, 145, 120, 32);
    offsetLabel.setBounds(12, 197, 155, 30);
    offsetSlider.setBounds(168, 197, 560, 30);
    status.setBounds(12, 110, getWidth() - 24, 28);
    infoLabel.setBounds(12, 301, getWidth() - 24, 40);
}
