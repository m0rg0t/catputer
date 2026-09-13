#include "../native/storage.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>

namespace {

void put32(std::uint8_t* output, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        output[i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

std::array<std::uint8_t, lofi::kStateLegacyBytes> legacyState() {
    std::array<std::uint8_t, lofi::kStateLegacyBytes> bytes{};
    std::memcpy(bytes.data(), "LOFI", 4);
    bytes[4] = lofi::kStateFormatLegacy;
    bytes[5] = static_cast<std::uint8_t>(lofi::kStateLegacyBytes);
    bytes[8] = 65;
    bytes[9] = 70;
    bytes[10] = 15;
    bytes[11] = 2;
    put32(bytes.data() + 156, lofi::crc32(bytes.data(), 156));
    return bytes;
}

} // namespace

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
    const auto legacyPath = dir/"legacy.bin";
    const auto legacy = legacyState();
    { std::ofstream file(legacyPath,std::ios::binary|std::ios::trunc);
      file.write(reinterpret_cast<const char*>(legacy.data()),legacy.size()); }
    assert(readState(legacyPath,loaded) && loaded.settings.volume==65 &&
           loaded.settings.bpm==0 && loaded.settings.meter==MusicMeter::Auto);
    auto invalid=next;invalid.count=255;assert(!saveState(path,invalid));
    fs::remove_all(dir);
    std::cout<<"storage: validated writes, corrupt primary, backup and temp recovery passed\n";
}
