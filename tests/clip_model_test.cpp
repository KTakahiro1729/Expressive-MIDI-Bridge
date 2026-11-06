#include "../vst3/JuceMidiPlayer/Source/ClipModel.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace expressive;

static Clip demoClip()
{
    Clip c;
    // Eight bars: a held chord per bar plus eighth-note melody (like the Python demo).
    for (int bar = 0; bar < 8; ++bar)
    {
        for (int p : { 48, 52, 55 })
            c.notes.push_back({ 1, p, 62, bar * 4.0, bar * 4.0 + 3.9 });
        for (int s = 0; s < 8; ++s)
            c.notes.push_back({ 1, 60 + (s % 5), 78, bar * 4.0 + s * 0.5, bar * 4.0 + s * 0.5 + 0.4 });
    }
    c.events.push_back({ 0.0, { 0xB0, 64, 127 }, 3 });
    c.events.push_back({ 16.0, { 0xB0, 64, 0 }, 3 });
    return c;
}

int main()
{
    int checks = 0;
    // Pairing: FIFO per pitch, open notes closed at the end, zero-velocity note-on = off.
    {
        NotePairer p;
        p.noteOn(1, 60, 90, 0.0);
        p.noteOn(1, 60, 80, 0.5);
        p.noteOff(1, 60, 1.0);
        p.noteOn(1, 62, 0, 1.5);   // stray off: ignored
        p.noteOn(2, 64, 70, 2.0);
        p.noteOn(2, 64, 0, 3.0);   // off
        auto notes = p.finish(4.0);
        assert(notes.size() == 3);
        assert(notes[0].pitch == 60 && notes[0].velocity == 90 && notes[0].end == 1.0);
        assert(notes[1].pitch == 60 && notes[1].velocity == 80 && notes[1].end == 4.0);
        assert(notes[2].channel == 2 && notes[2].end == 3.0);
        checks += 6;
    }
    const auto src = demoClip();
    // Strength 0 is an exact identity.
    {
        auto out = renderRuleBased(src, { 0.0, Style::classical, 23 }, 120.0);
        for (size_t i = 0; i < src.notes.size(); ++i)
            assert(out.notes[i].start == src.notes[i].start && out.notes[i].velocity == src.notes[i].velocity
                   && out.notes[i].end == src.notes[i].end);
        ++checks;
    }
    // Rendering preserves note count, pitches, channels and pedal events; is deterministic.
    {
        RenderStats stats;
        auto a = renderRuleBased(src, { 0.8, Style::classical, 7 }, 120.0, &stats);
        auto b = renderRuleBased(src, { 0.8, Style::classical, 7 }, 120.0);
        auto c = renderRuleBased(src, { 0.8, Style::classical, 8 }, 120.0);
        assert(a.notes.size() == src.notes.size() && a.events.size() == src.events.size());
        bool differs = false, changed = false;
        for (size_t i = 0; i < src.notes.size(); ++i)
        {
            assert(a.notes[i].pitch == src.notes[i].pitch && a.notes[i].channel == src.notes[i].channel);
            assert(a.notes[i].start == b.notes[i].start && a.notes[i].velocity == b.notes[i].velocity);
            assert(a.notes[i].velocity >= 1 && a.notes[i].velocity <= 127);
            assert(a.notes[i].end > a.notes[i].start);
            assert(a.notes[i].start >= 0.0);
            differs = differs || a.notes[i].start != c.notes[i].start;
            changed = changed || a.notes[i].velocity != src.notes[i].velocity;
        }
        assert(differs && changed);
        assert(a.events[0].beat == 0.0 && a.events[1].beat == 16.0 && a.events[1].bytes[1] == 64);
        // Shifts stay within the style maximum (22 ms) plus nothing else.
        assert(stats.maxAbsShiftMs <= 0.8 * 22.0 * (0.6 * 6.0 + 0.4) + 1e-6);
        assert(stats.meanAbsShiftMs > 0.0 && stats.meanAbsVelocityChange > 0.0);
        checks += 6;
    }
    // Chord tones (same onset) share the same timing shift.
    {
        auto out = renderRuleBased(src, { 1.0, Style::jazz, 3 }, 100.0);
        for (int bar = 0; bar < 8; ++bar)
        {
            const size_t base = static_cast<size_t>(bar) * 11;
            assert(std::abs(out.notes[base].start - out.notes[base + 1].start) < 1e-12);
            assert(std::abs(out.notes[base].start - out.notes[base + 2].start) < 1e-12);
        }
        ++checks;
    }
    // Repeated same-pitch notes never overlap after rendering.
    {
        Clip rep;
        for (int i = 0; i < 64; ++i)
            rep.notes.push_back({ 1, 72, 80, i * 0.125, i * 0.125 + 0.124 });
        auto out = renderRuleBased(rep, { 1.0, Style::classical, 11 }, 180.0);
        std::vector<ClipNote> sorted = out.notes;
        for (size_t i = 0; i + 1 < sorted.size(); ++i)
            assert(sorted[i].end < sorted[i + 1].start);
        ++checks;
    }
    // Bar helper.
    {
        assert(barStartBefore(9.5, 4) == 8.0);
        assert(barStartBefore(8.0, 4) == 8.0);
        assert(barStartBefore(7.0, 3) == 6.0);
        assert(barStartBefore(0.2, 4) == 0.0);
        checks += 4;
    }
    // Random generator is reproducible and roughly standard normal.
    {
        StableRandom r1(42), r2(42);
        double sum = 0, sq = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const double x = r1.gauss();
            assert(x == r2.gauss());
            sum += x; sq += x * x;
        }
        assert(std::abs(sum / 20000) < 0.05 && std::abs(sq / 20000 - 1.0) < 0.05);
        checks += 2;
    }
    std::cout << "Clip model: " << checks << " checks passed\n";
    return 0;
}
