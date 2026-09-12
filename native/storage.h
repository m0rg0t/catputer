#pragma once
#include "lofi/state.h"
#include <filesystem>
#include <fstream>

namespace lofi {
inline bool readState(const std::filesystem::path& path,SavedState& state) {
    std::ifstream file(path,std::ios::binary);
    if(!file) return false;
    std::array<std::uint8_t,kStateBytes+1> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    return decodeState(bytes.data(),static_cast<std::size_t>(file.gcount()),state);
}
inline bool loadState(const std::filesystem::path& path,SavedState& state) {
    return readState(path,state) || readState(path.string()+".bak",state) || readState(path.string()+".tmp",state);
}
inline bool saveState(const std::filesystem::path& path,const SavedState& state) {
    namespace fs=std::filesystem;
    std::array<std::uint8_t,kStateBytes> bytes{};
    if(!encodeState(state,bytes)) return false;
    try {
        if(!path.parent_path().empty()) fs::create_directories(path.parent_path());
        const fs::path temp=path.string()+".tmp",backup=path.string()+".bak";
        { std::ofstream file(temp,std::ios::binary|std::ios::trunc);
          file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
          file.flush(); if(!file) return false; }
        SavedState verify;
        if(!readState(temp,verify)) return false;
        if(fs::exists(path)) {
            if(readState(path,verify)) {
                if(fs::exists(backup)) fs::remove(backup);
                fs::rename(path,backup);
            } else fs::remove(path); // Preserve an already valid backup on recovery.
        }
        fs::rename(temp,path);
        return true;
    } catch(const std::exception&) { return false; }
}
}
