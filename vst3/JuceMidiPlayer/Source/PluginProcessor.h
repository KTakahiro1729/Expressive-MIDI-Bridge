#pragma once
#include <JuceHeader.h>
#include "LiveHumanizer.h"
#include <atomic>
#include <cstdint>
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
    void setStrength(double v) { strength.store(juce::jlimit(0.0, 1.0, v)); }
    double getStrength() const { return strength.load(); }
    void setTimingMs(double v) { timingMs.store(juce::jlimit(0.0, 40.0, v)); }
    double getTimingMs() const { return timingMs.load(); }
    void setVelocityRange(int v) { velocityRange.store(juce::jlimit(0, 32, v)); }
    int getVelocityRange() const { return velocityRange.load(); }

    // Read-only status for the editor (written on the audio thread, polled by a UI timer).
    struct Activity
    {
        std::uint32_t midiIn = 0, midiOut = 0; // monotonically increasing note counters
        bool hasClock = false, playing = false;
        double bpm = 0.0, ppq = 0.0;
    };
    Activity getActivity() const noexcept
    {
        Activity a;
        a.midiIn = midiInCounter.load(std::memory_order_relaxed);
        a.midiOut = midiOutCounter.load(std::memory_order_relaxed);
        a.hasClock = hostHasClock.load(std::memory_order_relaxed);
        a.playing = hostPlaying.load(std::memory_order_relaxed);
        a.bpm = hostBpm.load(std::memory_order_relaxed);
        a.ppq = hostPpq.load(std::memory_order_relaxed);
        return a;
    }
    int loadedNoteCount() const { return clipNoteCount.load(); }
    double loadedLengthBeats() const { return clipLengthBeats.load(); }
    bool hasLoadedClip() const { return clipNoteCount.load() > 0 || clipLengthBeats.load() > 0.0; }

    // Editor-only preference, saved with the project. 0 = English, 1 = Japanese, -1 = follow OS.
    void setUiLanguage(int v) { uiLanguage.store(v < -1 || v > 1 ? -1 : v); }
    int getUiLanguage() const { return uiLanguage.load(); }

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
    std::atomic<double> strength { 0.65 }, timingMs { 16.0 };
    std::atomic<int> velocityRange { 12 };
    std::atomic<std::uint32_t> midiInCounter { 0 }, midiOutCounter { 0 };
    std::atomic<bool> hostHasClock { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 };
    std::atomic<int> clipNoteCount { 0 };
    std::atomic<double> clipLengthBeats { 0.0 };
    std::atomic<int> uiLanguage { -1 };
    expressive::LiveHumanizer liveHumanizer;
    int previousMode = -1;
    bool wasLivePlaying = false;
    double expectedLivePpq = -1.0;
    bool wasPlaying = false;
    double nextExpectedPpq = -1.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExpressiveMidiPlayerProcessor)
};
