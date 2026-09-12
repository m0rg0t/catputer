#include "../native/storage.h"
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
    namespace fs=std::filesystem; using namespace lofi;
    const auto dir=fs::temp_directory_path()/("lofi-state-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path=dir/"state.bin";
    SavedState first; first.settings.volume=20; assert(saveState(path,first));
    SavedState next=first;next.settings.volume=50;assert(saveState(path,next));
    SavedState loaded;assert(loadState(path,loaded) && loaded.settings.volume==50);
    {std::ofstream bad(path,std::ios::trunc);bad<<"truncated";}
    assert(loadState(path,loaded) && loaded.settings.volume==20);
    assert(saveState(path,next));assert(readState(path.string()+".bak",loaded) && loaded.settings.volume==20);
    fs::remove(path);fs::remove(path.string()+".bak");
    assert(saveState(path.string()+".tmp",first));assert(loadState(path,loaded) && loaded.settings.volume==20);
    auto invalid=next;invalid.count=255;assert(!saveState(path,invalid));
    fs::remove_all(dir);
    std::cout<<"storage: validated writes, corrupt primary, backup and temp recovery passed\n";
}
