#include "lofi/controller.h"
#include "lofi/sample_bank.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace lofi;
    Controller c(UINT64_C(0x123456789abcdef0));
    Snapshot snap; snap.config=c.initialConfig(); snap.bpm=72;
    c.setSnapshot(snap); c.tick(100);
    c.key('f'); assert(c.saved.count==1 && c.saved.favorites[0].seed==snap.config.seed);
    c.key('l'); auto recall=c.key('\n'); assert(recall.kind==ActionKind::Config && recall.config.seed==snap.config.seed);
    c.saved.favorites[0].seed=42;c.key('l');assert(c.key('\n').config.seed==42);
    auto changed=c.key('e');assert(changed.config.seed==42 && changed.config.soundEngine==SoundEngine::Hybrid);
    auto pause=c.key(' ');auto resume=c.key(' ');assert(pause.paused && !resume.paused);
    c.saved.favorites[0].engine=1; c.saved.favorites[0].bankFingerprint=0;
    c.key('l'); assert(c.key('\n').kind==ActionKind::None);
    c.key('s'); for(int i=0;i<20;++i) c.key('.'); assert(c.view.selection==6);
    c.saved.settings.volume=100; auto reset=c.key('\n'); assert(reset.kind==ActionKind::Config && c.saved.settings.volume==35);
    c.tick(200); assert(c.view.itemCount==7);
    for(int i=0;i<20;++i) c.key(';'); assert(c.view.selection==0);
    for(int i=0;i<30;++i) c.key(','); assert(c.saved.settings.volume==0);
    for(int i=0;i<30;++i) c.key('/'); assert(c.saved.settings.volume==100);
    c.dirty=false; c.storageResult(true); assert(!c.dirty);
    c.key('-'); c.storageResult(true); assert(c.dirty); // A previous save must not erase a newer edit.
    c.dirty=false; c.storageResult(false); assert(c.dirty);
    c.key(27); c.tick(10000); assert(c.view.clean);
    c.key('h'); c.tick(10001); assert(!c.view.clean);
    std::cout<<"controller: menu bounds, favorites, reset and save acknowledgements passed\n";
}
