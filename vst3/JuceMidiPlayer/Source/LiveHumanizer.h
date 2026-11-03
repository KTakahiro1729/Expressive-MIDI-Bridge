#pragma once
// Host-independent delayed-note scheduler. Audio-thread safe: fixed-capacity storage,
// no locks, no heap allocations, and note-off delay matches its note-on delay.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace expressive {
struct LiveNote { std::int64_t due; int channel, pitch, velocity; bool on; };

class LiveHumanizer {
public:
    void reset() noexcept { count = 0; cursor = 0; for (auto& a : heldDelay) a.fill(0); }
    std::int64_t currentSample() const noexcept { return cursor; }
    bool add(int blockOffset, int channel, int pitch, int velocity, bool on,
             double strength, double timingMs, int velocityRange, double sampleRate) noexcept {
        if (channel < 1 || channel > 16 || pitch < 0 || pitch > 127 || count == pending.size()) return false;
        const int index = channel - 1;
        const std::int64_t at = cursor + std::max(0, blockOffset);
        const double s = std::clamp(strength, 0.0, 1.0);
        int delay = 0;
        if (on) {
            const auto grouping = static_cast<std::int64_t>(std::max(1.0, sampleRate * 0.004));
            const auto h = hash(static_cast<std::uint64_t>(at / grouping));
            const double r = static_cast<double>(h & 0xffffu) / 65535.0;
            delay = static_cast<int>(std::llround(r * s * std::clamp(timingMs, 0.0, 40.0) * sampleRate * 0.001));
            heldDelay[static_cast<size_t>(index)][static_cast<size_t>(pitch)] = delay;
            const auto vhash = hash(static_cast<std::uint64_t>(at) + static_cast<std::uint64_t>(pitch) * 131u);
            const double signedR = (static_cast<double>(vhash & 0xffffu) / 65535.0) * 2.0 - 1.0;
            const int variation = static_cast<int>(std::lround(signedR * s * std::clamp(velocityRange, 0, 32)));
            velocity = std::clamp(velocity + variation, 1, 127);
        } else {
            delay = heldDelay[static_cast<size_t>(index)][static_cast<size_t>(pitch)];
            heldDelay[static_cast<size_t>(index)][static_cast<size_t>(pitch)] = 0;
        }
        pending[count++] = {at + delay, channel, pitch, velocity, on};
        return true;
    }
    template<class Emit>
    void emitBlock(int blockSamples, Emit emit) noexcept {
        if (blockSamples <= 0) return;
        const auto end = cursor + blockSamples;
        size_t kept = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto& event = pending[i];
            if (event.due < end) {
                const auto offset = static_cast<int>(std::clamp<std::int64_t>(event.due - cursor, 0, blockSamples - 1));
                emit(event, offset);
            } else {
                pending[kept++] = event;
            }
        }
        count = kept;
        cursor = end;
    }
private:
    static std::uint32_t hash(std::uint64_t n) noexcept {
        n ^= n >> 33; n *= 0xff51afd7ed558ccdULL;
        n ^= n >> 33; n *= 0xc4ceb9fe1a85ec53ULL;
        n ^= n >> 33;
        return static_cast<std::uint32_t>(n);
    }
    std::array<LiveNote, 4096> pending{};
    std::array<std::array<int,128>,16> heldDelay{};
    size_t count = 0;
    std::int64_t cursor = 0;
};
}
