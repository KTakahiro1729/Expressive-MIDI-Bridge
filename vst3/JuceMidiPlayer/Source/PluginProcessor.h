#pragma once
#include <JuceHeader.h>
#include "LiveHumanizer.h"
#include "HandRouting.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// An intentionally small MIDI-only VST3: load a generated MIDI file and output
// its notes on the DAW transport. No synthesis and NO model inference on audio thread.
class ExpressiveMidiPlayerProcessor final : public juce::AudioProcessor
{
public:
    ExpressiveMidiPlayerProcessor();
    ~ExpressiveMidiPlayerProcessor() override = default;
    const juce::String getName() const override { return "Expressive MIDI Player"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override
    { return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
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

    // UI/message-thread API. A loaded clip is immutable during audio rendering.
    bool loadMidi(const juce::File&, juce::String& error);
    bool reloadLastMidi(juce::String& error);
    juce::String loadedFileName() const { return lastLoadedName; } // UI thread only
    void setStartBeats(double v) { startBeats.store(v); }
    double getStartBeats() const { return startBeats.load(); }
    bool loadMidiBytes(const void*, size_t, juce::String& error);
    void setMode(int v) { mode.store(v == 1 ? 1 : 0); }
    int getMode() const { return mode.load(); }
    void setHandMapping(int value) { handMapping.store(value == 1 ? 1 : 0); }
    int getHandMapping() const { return handMapping.load(); }
    void setSplitNote(int note) { splitNote.store(juce::jlimit(21, 108, note)); }
    int getSplitNote() const { return splitNote.load(); }
    void setStrength(int hand, double v) { hands[clampHand(hand)].strength.store(juce::jlimit(0.0, 1.0, v)); }
    double getStrength(int hand) const { return hands[clampHand(hand)].strength.load(); }
    void setTimingMs(int hand, double v) { hands[clampHand(hand)].timingMs.store(juce::jlimit(0.0, 40.0, v)); }
    double getTimingMs(int hand) const { return hands[clampHand(hand)].timingMs.load(); }
    void setVelocityRange(int hand, int v) { hands[clampHand(hand)].velocityRange.store(juce::jlimit(0, 32, v)); }
    int getVelocityRange(int hand) const { return hands[clampHand(hand)].velocityRange.load(); }

private:
    struct ScheduledEvent
    {
        double ppq;
        juce::MidiMessage message;
    };
    struct Clip
    {
        std::vector<ScheduledEvent> events;
    };
    std::shared_ptr<const Clip> clip;
    juce::String lastLoadedPath, lastLoadedName;
    juce::MemoryBlock embeddedMidi; // saved within DAW project, not just file path
    std::atomic<double> startBeats { 0.0 };
    std::atomic<int> clipRevision { 0 };
    int lastClipRevision = 0;
    std::atomic<int> mode { 0 }; // 0 = live MIDI from track; 1 = legacy MIDI file
    struct HandSettings
    {
        std::atomic<double> strength { 0.65 };
        std::atomic<double> timingMs { 16.0 };
        std::atomic<int> velocityRange { 12 };
    };
    static constexpr int clampHand(int h) noexcept { return h == 1 ? 1 : 0; }
    std::array<HandSettings, 2> hands; // 0 = right, 1 = left
    std::atomic<int> handMapping { 0 }; // 0 = pitch, 1 = channel (1=RH, 2=LH)
    std::atomic<int> splitNote { 60 }; // MIDI C4; only used in pitch mode
    expressive::LiveHumanizer liveHumanizer;
    int previousMode = -1;
    int previousHandMapping = -1;
    int previousSplitNote = -1;
    bool wasLivePlaying = false;
    double expectedLivePpq = -1.0;
    bool wasPlaying = false;
    double nextExpectedPpq = -1.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerProcessor)
};
