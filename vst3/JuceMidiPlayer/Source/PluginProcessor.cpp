#include "PluginProcessor.h"
#include "PluginEditor.h"

ExpressiveMidiPlayerProcessor::ExpressiveMidiPlayerProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    startTimerHz(10);
}

ExpressiveMidiPlayerProcessor::~ExpressiveMidiPlayerProcessor()
{
    stopTimer();
}

bool ExpressiveMidiPlayerProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layout.getMainInputChannelSet() == out;
}

void ExpressiveMidiPlayerProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // Audio passes through unchanged. Only the tempo is read, for the MIDI the AI writes.
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (const auto bpm = position->getBpm())
                if (*bpm > 0.0 && std::isfinite(*bpm))
                    hostBpm.store(*bpm);
}

void ExpressiveMidiPlayerProcessor::getStateInformation(juce::MemoryBlock& data)
{
    juce::XmlElement state("ExpressiveMidiBridge");
    session.saveState(state);
    copyXmlToBinary(state, data);
}

void ExpressiveMidiPlayerProcessor::setStateInformation(const void* data, int size)
{
    if (const auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName("ExpressiveMidiBridge"))
            session.loadState(*xml);
}

juce::AudioProcessorEditor* ExpressiveMidiPlayerProcessor::createEditor()
{
    return new ExpressiveMidiPlayerEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExpressiveMidiPlayerProcessor();
}
