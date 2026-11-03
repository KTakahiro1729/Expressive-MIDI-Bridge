#pragma once
// Audio-thread-safe, allocation-free choice of independent piano-hand settings.
// Mapping affects expression only; the original MIDI channel is preserved.
#include <cstdint>

namespace expressive {
enum class Hand : std::uint8_t { right = 0, left = 1, unchanged = 2 };
enum class HandMapping : std::uint8_t { pitchSplit = 0, midiChannels = 1 };

inline Hand classifyHand(int channel, int pitch, HandMapping mapping, int splitNote) noexcept
{
    if (channel < 1 || channel > 16 || pitch < 0 || pitch > 127)
        return Hand::unchanged;
    if (mapping == HandMapping::midiChannels)
    {
        if (channel == 1) return Hand::right;
        if (channel == 2) return Hand::left;
        return Hand::unchanged; // never silently apply RH settings to other parts
    }
    if (splitNote < 0) splitNote = 0;
    if (splitNote > 127) splitNote = 127;
    return pitch >= splitNote ? Hand::right : Hand::left;
}
} // namespace expressive
