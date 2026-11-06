#pragma once
// Host-independent MIDI clip model and the rule-based "expressive" renderer used by the
// plugin's clip workflow (bring MIDI in -> choose how it is played -> drag result back).
// No JUCE dependency, so it is compiled and tested directly by tests/clip_model_test.cpp.
//
// Times are in quarter-note beats (PPQ) relative to the song start. Seconds are derived
// from a single constant BPM (the host tempo at the time of rendering).
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
    // Musical context used for drop placement and for the metrical model.
    double beatsPerBar = 4.0;       // quarter-note beats per bar (6/8 = 3)
    int timeSigNumerator = 4, timeSigDenominator = 4;
    double originBeat = 0.0;   // beat at which the exported file should start (bar line)

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

// Deterministic, platform-independent random numbers (std::normal_distribution differs
// between standard libraries, which would make the same project sound different on
// Windows and macOS).
class StableRandom
{
public:
    explicit StableRandom(std::uint64_t seed) : state(seed) {}
    double uniform() noexcept // (0, 1)
    {
        state += 0x9e3779b97f4a7c15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return (static_cast<double>(z >> 11) + 0.5) / 9007199254740992.0;
    }
    double gauss() noexcept
    {
        if (hasSpare) { hasSpare = false; return spare; }
        const double u = uniform(), v = uniform();
        const double r = std::sqrt(-2.0 * std::log(u));
        const double a = 6.283185307179586 * v;
        spare = r * std::sin(a);
        hasSpare = true;
        return r * std::cos(a);
    }
private:
    std::uint64_t state;
    double spare = 0.0;
    bool hasSpare = false;
};

enum class Style { classical = 0, pop = 1, jazz = 2 };

struct RenderSettings
{
    double strength = 0.65;   // 0..1, 0 = identity
    Style style = Style::classical;
    std::uint32_t seed = 23;
};

struct RenderStats
{
    int notes = 0;
    double meanAbsShiftMs = 0.0, maxAbsShiftMs = 0.0;
    double meanAbsVelocityChange = 0.0;
};

// Rule-based (non-AI) phrasing: chord tones share a timing offset, metrical accents and a
// four-bar dynamic arc shape velocity, durations change slightly. Pitches/counts and all
// non-note events (pedal etc.) are preserved. Port of expressive_midi.core.render_baseline.
inline Clip renderRuleBased(const Clip& source, const RenderSettings& settings, double bpm,
                            RenderStats* stats = nullptr)
{
    Clip out = source;
    const double strength = std::clamp(settings.strength, 0.0, 1.0);
    if (!(bpm > 0.0) || !std::isfinite(bpm)) bpm = 120.0;
    const double secPerBeat = 60.0 / bpm;
    const double bar = source.beatsPerBar > 0.0 ? source.beatsPerBar : 4.0;
    const double tick = 1.0 / 960.0;

    std::vector<size_t> order(source.notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return std::tie(source.notes[a].start, source.notes[a].pitch)
             < std::tie(source.notes[b].start, source.notes[b].pitch);
    });

    if (strength > 0.0 && !order.empty())
    {
        StableRandom rng(0x5eed0000ULL ^ settings.seed);
        const double maxShiftMs = settings.style == Style::pop ? 10.0
                                : settings.style == Style::jazz ? 23.0 : 22.0;
        size_t g = 0;
        while (g < order.size())
        {
            // Nearly simultaneous notes (within 12 ms) form a chord group.
            size_t h = g + 1;
            const double groupStart = source.notes[order[g]].start;
            while (h < order.size() && (source.notes[order[h]].start - groupStart) * secPerBeat <= 0.012)
                ++h;

            const double beat = groupStart;
            const double pos = std::fmod(beat, bar);
            const double phrase = std::fmod(beat, 4.0 * bar) / (4.0 * bar);
            const double arc = std::sin(3.141592653589793 * phrase);
            const double downbeat = std::cos(6.283185307179586 * pos / bar);
            const double variance = rng.gauss();
            const double shiftSec = strength * maxShiftMs / 1000.0
                                  * (0.6 * variance + 0.4 * std::sin(beat * 1.7));
            const double target = std::max(0.0, beat + shiftSec / secPerBeat);
            const double delta = target - beat;
            double dynamic = strength * (10.0 * downbeat + 7.0 * arc + rng.gauss() * 4.0);
            if (settings.style == Style::jazz)
                dynamic += strength * (pos >= 1.8 ? 6.0 : -3.0);

            for (size_t k = g; k < h; ++k)
            {
                const auto& src = source.notes[order[k]];
                auto& dst = out.notes[order[k]];
                dst.start = std::max(0.0, src.start + delta);
                dst.velocity = std::clamp(static_cast<int>(std::lround(src.velocity + dynamic
                                          + rng.gauss() * 2.5 * strength)), 1, 127);
                const double lengthSec = std::max(0.0, (src.end - src.start) * secPerBeat);
                const double factor = 1.0 + strength * (0.028 * (arc - 0.4) + rng.gauss() * 0.018);
                const double newLengthSec = std::max(0.02, lengthSec * factor);
                dst.end = std::max(dst.start + tick, dst.start + newLengthSec / secPerBeat);
            }
            g = h;
        }

        // Prevent repeated same-pitch notes from overlapping after the shifts.
        std::map<int, std::vector<size_t>> samePitch;
        for (size_t i : order)
            samePitch[source.notes[i].channel * 128 + source.notes[i].pitch].push_back(i);
        for (auto& entry : samePitch)
        {
            auto& seq = entry.second; // already in original onset order
            for (size_t k = 0; k + 1 < seq.size(); ++k)
            {
                auto& prev = out.notes[seq[k]];
                auto& next = out.notes[seq[k + 1]];
                if (prev.end >= next.start)
                {
                    prev.end = std::max(prev.start + tick, next.start - tick);
                    if (prev.end >= next.start)
                    {
                        next.start = prev.end + tick;
                        next.end = std::max(next.end, prev.end + 2.0 * tick);
                    }
                }
            }
        }
    }

    if (stats)
    {
        *stats = {};
        stats->notes = static_cast<int>(out.notes.size());
        double shiftSum = 0.0, velSum = 0.0;
        for (size_t i = 0; i < out.notes.size(); ++i)
        {
            const double s = std::abs(out.notes[i].start - source.notes[i].start) * secPerBeat * 1000.0;
            shiftSum += s;
            stats->maxAbsShiftMs = std::max(stats->maxAbsShiftMs, s);
            velSum += std::abs(out.notes[i].velocity - source.notes[i].velocity);
        }
        if (!out.notes.empty())
        {
            stats->meanAbsShiftMs = shiftSum / static_cast<double>(out.notes.size());
            stats->meanAbsVelocityChange = velSum / static_cast<double>(out.notes.size());
        }
    }
    return out;
}

// Bar line at or before the first note, used as the start of exported files so that the
// user can drop the result exactly on a bar.
inline double barStartBefore(double beat, double beatsPerBar, double barReferenceBeat = 0.0)
{
    const double bar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    return barReferenceBeat + std::floor((beat - barReferenceBeat) / bar + 1e-9) * bar;
}

} // namespace expressive
