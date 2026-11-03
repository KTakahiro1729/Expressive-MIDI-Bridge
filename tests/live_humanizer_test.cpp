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
    std::cout << "Live humanizer: 8 checks passed\n";
}
