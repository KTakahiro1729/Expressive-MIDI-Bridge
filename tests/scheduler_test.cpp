#include "../vst3/JuceMidiPlayer/Source/MidiScheduler.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <utility>
#include <iostream>

struct E { double ppq; int id; };
int main()
{
    const std::vector<E> notes{{0.0,0},{0.5,1},{1.0,2},{1.5,3},{2.0,4}};
    std::vector<std::pair<int,int>> out;
    auto sink = [&out](const E& event, int sample) { out.emplace_back(event.id, sample); };
    // 120 BPM, 48 kHz: a 24000-sample block spans exactly 1 quarter note.
    expressive::scheduleBlock(notes,0.0,120.0,48000.0,24000,sink);
    assert((out == std::vector<std::pair<int,int>>{{0,0},{1,12000}}));
    out.clear();
    expressive::scheduleBlock(notes,1.0,120.0,48000.0,24000,sink);
    assert((out == std::vector<std::pair<int,int>>{{2,0},{3,12000}}));
    out.clear();
    expressive::scheduleBlock(notes,-1.0,120.0,48000.0,24000,sink);
    assert(out.empty());
    out.clear();
    expressive::scheduleBlock(notes,2.0,120.0,48000.0,0,sink);
    assert(out.empty());
    out.clear();
    // Start offset of 4 beats: clip PPQ = host PPQ - 4.
    expressive::scheduleBlock(notes,4.0-4.0,120.0,48000.0,24000,sink);
    assert((out == std::vector<std::pair<int,int>>{{0,0},{1,12000}}));
    std::cout << "MIDI scheduler: 5 tests passed\n";
}
