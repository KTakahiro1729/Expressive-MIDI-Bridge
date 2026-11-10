#include "../vst3/JuceMidiPlayer/Source/ClipModel.h"
#include <cassert>
#include <iostream>

using namespace expressive;

int main()
{
    int checks = 0;
    // FIFO pairing per channel+pitch; open notes closed at the end; velocity-0 note-on = off.
    NotePairer p;
    p.noteOn(1, 60, 90, 0.0);
    p.noteOn(1, 60, 80, 0.5);
    p.noteOff(1, 60, 1.0);
    p.noteOn(1, 62, 0, 1.5);   // stray off: ignored
    p.noteOn(2, 64, 70, 2.0);
    p.noteOn(2, 64, 0, 3.0);   // off
    p.noteOn(3, 65, 50, 3.5);
    p.noteOff(3, 65, 3.5);     // zero length gets the minimum length
    auto notes = p.finish(4.0);
    assert(notes.size() == 4);
    assert(notes[0].pitch == 60 && notes[0].velocity == 90 && notes[0].end == 1.0);
    assert(notes[1].pitch == 60 && notes[1].velocity == 80 && notes[1].end == 4.0);
    assert(notes[2].channel == 2 && notes[2].end == 3.0);
    assert(notes[3].end > notes[3].start);
    checks += 5;

    Clip c;
    c.notes = notes;
    c.events.push_back({ 5.0, { 0xB0, 64, 0 }, 3 });
    assert(c.firstBeat() == 0.0 && c.lastBeat() == 5.0 && !c.empty());
    checks += 1;
    std::cout << "Clip model: " << checks << " checks passed\n";
    return 0;
}
