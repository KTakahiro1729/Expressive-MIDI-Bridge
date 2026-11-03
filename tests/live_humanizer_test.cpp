#include "../vst3/JuceMidiPlayer/Source/LiveHumanizer.h"
#include <cassert>
#include <vector>
#include <iostream>

struct Out { int sample, pitch, velocity; bool on; };
int main() {
    expressive::LiveHumanizer h;
    std::vector<Out> out;
    auto sink = [&](const expressive::LiveNote& n, int at) { out.push_back({at,n.pitch,n.velocity,n.on}); };
    // 0 strength is exact, including velocity and note-off timing.
    assert(h.add(7, 1, 60, 87, true, 0, 40, 32, 48000));
    assert(h.add(28, 1, 60, 0, false, 0, 40, 32, 48000));
    h.emitBlock(64, sink);
    assert(out.size()==2 && out[0].sample==7 && out[0].velocity==87 && out[1].sample==28);
    out.clear(); h.reset();
    // Delayed note-on and its matching note-off are both emitted, including across blocks.
    assert(h.add(0, 1, 64, 75, true, 1, 40, 0, 48000));
    assert(h.add(2000, 1, 64, 0, false, 1, 40, 0, 48000));
    for(int i=0; i<50; ++i) h.emitBlock(128, sink);
    assert(out.size()==2 && out[0].on && !out[1].on && out[1].velocity==0);
    assert(out[0].pitch==64 && out[1].pitch==64);
    out.clear(); h.reset();
    assert(h.currentSample()==0);
    assert(!h.add(0,0,60,1,true,1,10,3,48000));
    assert(!h.add(0,1,128,1,true,1,10,3,48000));
    assert(h.add(0,2,64,1,true,0,0,0,48000));
    h.emitBlock(32,sink);
    assert(out.size()==1 && out[0].velocity==1);
    // Overlapping same-key notes each retain their own onset/off delay (FIFO pairing).
    out.clear(); h.reset();
    std::vector<Out> absolute;
    int blockStart=0;
    auto absSink = [&](const expressive::LiveNote& n, int at) {
        absolute.push_back({blockStart + at,n.pitch,n.velocity,n.on});
    };
    assert(h.add(0,1,67,65,true,1,3,0,48000));
    assert(h.add(300,1,67,90,true,1,3,0,48000));
    assert(h.add(600,1,67,0,false,1,3,0,48000));
    assert(h.add(1000,1,67,0,false,1,3,0,48000));
    for (int i=0;i<15;++i) { h.emitBlock(128, absSink); blockStart+=128; }
    assert(absolute.size()==4 && absolute[0].on && absolute[1].on);
    assert(!absolute[2].on && !absolute[3].on);
    assert(absolute[2].sample-absolute[0].sample==600);
    assert(absolute[3].sample-absolute[1].sample==700);
    std::cout << "Live humanizer: 13 checks passed\n";
}
