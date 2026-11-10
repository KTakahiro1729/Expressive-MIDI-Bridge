#pragma once
// Host-independent MIDI clip model used for the plugin's before/after preview.
// No JUCE dependency, so it is compiled and tested directly by tests/clip_model_test.cpp.
// Times are in quarter-note beats (PPQ) from the start of the MIDI file.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace expressive {

struct ClipNote
{
    int channel = 1;        // 1..16
    int pitch = 60;         // 0..127
    int velocity = 64;      // 1..127
    double start = 0.0;     // beats
    double end = 0.0;       // beats, > start
};

// Non-note channel messages (sustain pedal, other CCs, pitch bend, program change ...).
struct ClipEvent
{
    double beat = 0.0;
    std::uint8_t bytes[3] { 0, 0, 0 };
    int size = 0;
};

struct Clip
{
    std::vector<ClipNote> notes;
    std::vector<ClipEvent> events;
    double beatsPerBar = 4.0;       // quarter-note beats per bar (6/8 = 3)
    int timeSigNumerator = 4, timeSigDenominator = 4;

    bool empty() const noexcept { return notes.empty(); }
    double firstBeat() const noexcept
    {
        double b = notes.empty() ? 0.0 : notes.front().start;
        for (const auto& n : notes) b = std::min(b, n.start);
        return b;
    }
    double lastBeat() const noexcept
    {
        double b = 0.0;
        for (const auto& n : notes) b = std::max(b, n.end);
        for (const auto& e : events) b = std::max(b, e.beat);
        return b;
    }
};

// Pairs raw note-on/off messages into notes, FIFO per channel+pitch (same rule as the
// Python core). Note-ons still open at `closeAtBeat` are closed there.
class NotePairer
{
public:
    void noteOn(int channel, int pitch, int velocity, double beat)
    {
        if (velocity <= 0) { noteOff(channel, pitch, beat); return; }
        open[key(channel, pitch)].push_back({ channel, pitch, velocity, beat, beat });
    }
    void noteOff(int channel, int pitch, double beat)
    {
        auto it = open.find(key(channel, pitch));
        if (it == open.end() || it->second.empty()) return;
        auto note = it->second.front();
        it->second.erase(it->second.begin());
        note.end = std::max(beat, note.start + minLength);
        done.push_back(note);
    }
    std::vector<ClipNote> finish(double closeAtBeat)
    {
        for (auto& entry : open)
            for (auto note : entry.second)
            {
                note.end = std::max(closeAtBeat, note.start + minLength);
                done.push_back(note);
            }
        open.clear();
        std::stable_sort(done.begin(), done.end(), [](const ClipNote& a, const ClipNote& b)
                         { return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch); });
        return std::move(done);
    }
    static constexpr double minLength = 1.0 / 960.0;
private:
    static int key(int channel, int pitch) { return channel * 128 + pitch; }
    std::map<int, std::vector<ClipNote>> open;
    std::vector<ClipNote> done;
};

} // namespace expressive
