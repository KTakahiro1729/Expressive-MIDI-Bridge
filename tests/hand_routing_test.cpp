#include "../vst3/JuceMidiPlayer/Source/HandRouting.h"
#include "../vst3/JuceMidiPlayer/Source/LiveHumanizer.h"
#include <cassert>
#include <iostream>
#include <vector>

using expressive::Hand;
using expressive::HandMapping;
using expressive::classifyHand;

int main()
{
    // Pitch split works when both hands share MIDI channel 1.
    assert(classifyHand(1, 59, HandMapping::pitchSplit, 60) == Hand::left);
    assert(classifyHand(1, 60, HandMapping::pitchSplit, 60) == Hand::right);
    assert(classifyHand(1, 66, HandMapping::pitchSplit, 67) == Hand::left);
    assert(classifyHand(1, 67, HandMapping::pitchSplit, 67) == Hand::right);

    // Explicit MIDI channel mapping survives crossed hands.
    assert(classifyHand(1, 40, HandMapping::midiChannels, 60) == Hand::right);
    assert(classifyHand(2, 90, HandMapping::midiChannels, 60) == Hand::left);
    assert(classifyHand(3, 60, HandMapping::midiChannels, 60) == Hand::unchanged);
    assert(classifyHand(16, 60, HandMapping::midiChannels, 60) == Hand::unchanged);
    assert(classifyHand(0, 60, HandMapping::pitchSplit, 60) == Hand::unchanged);
    assert(classifyHand(1, 128, HandMapping::midiChannels, 60) == Hand::unchanged);

    expressive::LiveHumanizer h;
    struct Note { int pitch, channel, velocity, sample; bool on; };
    std::vector<Note> notes;
    // Independent settings: RH off (identity) while LH has maximum delay.
    for (const auto& spec : { std::pair<int, int>{76, 1}, {48, 2} })
    {
        const auto hand = classifyHand(spec.second, spec.first, HandMapping::midiChannels, 60);
        const double strength = hand == Hand::right ? 0.0 : 1.0;
        assert(h.add(480, spec.second, spec.first, 100, true, strength, 40, 0, 48000));
        assert(h.add(2980, spec.second, spec.first, 0, false, strength, 40, 0, 48000));
    }
    for (int i = 0; i < 42; ++i)
        h.emitBlock(128, [&](const expressive::LiveNote& n, int sample) {
            notes.push_back({n.pitch, n.channel, n.velocity, static_cast<int>(i * 128 + sample), n.on});
        });
    assert(notes.size() == 4);
    int rightOn = -1, rightOff = -1, leftOn = -1, leftOff = -1;
    for (const auto& n : notes)
    {
        assert(n.channel == (n.pitch == 76 ? 1 : 2)); // MIDI channels are preserved
        if (n.pitch == 76) (n.on ? rightOn : rightOff) = n.sample;
        else (n.on ? leftOn : leftOff) = n.sample;
    }
    assert(rightOn == 480 && rightOff == 2980);
    assert(leftOn > 480 && leftOff - leftOn == 2500);
    assert(leftOn < 2400); // 40ms at 48kHz, strictly below the upper bound
    std::cout << "Hand routing checks passed\n";
}
