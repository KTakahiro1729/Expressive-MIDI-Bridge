#pragma once
#include <JuceHeader.h>
#include "AiEngine.h"

// Audio effect that leaves audio and MIDI untouched. It sits on the piano track's insert
// so that its window (drop .mid -> AI -> drag the result back) lives next to that track.
class ExpressiveMidiPlayerProcessor final : public juce::AudioProcessor,
                                           private juce::Timer
{
public:
    ExpressiveMidiPlayerProcessor();
    ~ExpressiveMidiPlayerProcessor() override;
    const juce::String getName() const override { return "Expressive MIDI Bridge"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    expressive::AiSession& getSession() { return session; }
    double getHostBpm() const { return hostBpm.load(); }   // 0 if unknown

private:
    void timerCallback() override { session.poll(); }
    std::atomic<double> hostBpm { 0.0 };
    expressive::AiSession session;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerProcessor)
};
