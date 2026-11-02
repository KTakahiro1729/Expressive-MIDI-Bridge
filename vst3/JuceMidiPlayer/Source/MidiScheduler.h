#pragma once
// Host-agnostic, allocation-free per-block MIDI scheduling.
// All event coordinates are quarter-note (PPQ) positions.
#include <algorithm>
#include <cmath>

namespace expressive
{
    template <typename EventContainer, typename Emit>
    void scheduleBlock(const EventContainer& sortedEvents, double startPpq, double bpm,
                       double sampleRate, int samples, Emit emit)
    {
        if (!std::isfinite(startPpq) || !std::isfinite(bpm) || bpm <= 0.0 ||
            !std::isfinite(sampleRate) || sampleRate <= 0.0 || samples <= 0)
            return;
        const double quarterNotes = (samples / sampleRate) * (bpm / 60.0);
        const double end = startPpq + quarterNotes;
        const auto begin = std::lower_bound(sortedEvents.begin(), sortedEvents.end(), startPpq,
            [](const auto& event, double start) { return event.ppq < start - 1e-9; });
        for (auto it = begin; it != sortedEvents.end() && it->ppq < end - 1e-9; ++it)
        {
            const int sample = std::clamp(static_cast<int>(std::round((it->ppq - startPpq) *
                                              60.0 / bpm * sampleRate)), 0, samples - 1);
            emit(*it, sample);
        }
    }
}
