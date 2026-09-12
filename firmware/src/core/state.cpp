#include "lofi/state.h"
#include <cstring>

namespace lofi {
namespace {
void put32(std::uint8_t* out, std::uint32_t n) {
    for (unsigned i=0;i<4;++i) out[i]=static_cast<std::uint8_t>(n>>(8*i));
}
std::uint32_t get32(const std::uint8_t* in) {
    std::uint32_t n=0; for (unsigned i=0;i<4;++i) n|=std::uint32_t(in[i])<<(8*i); return n;
}
// Preserve known older favorite records when loading settings. The controller
// marks them unavailable rather than silently replaying a different score.
bool validFavorite(const Favorite& f) { return f.mood<3 && f.engine<2 && f.texture<=100 && f.schema>=1 && f.schema<=kSessionSchema; }
bool sameFavorite(const Favorite& a,const Favorite& b) {
    return a.seed==b.seed && a.bankFingerprint==b.bankFingerprint && a.mood==b.mood &&
           a.engine==b.engine && a.texture==b.texture && a.schema==b.schema;
}
}
bool validSettings(const Settings& s) {
    return s.volume<=100 && s.brightness>=10 && s.brightness<=100 && s.texture<=100 && s.motion<=2 && s.engine<2 && s.mood<3;
}
std::uint32_t crc32(const std::uint8_t* p, std::size_t size) {
    std::uint32_t crc=0xffffffffu;
    for (std::size_t i=0;i<size;++i) {
        crc^=p[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return ~crc;
}
std::uint32_t fingerprint(const char* p) {
    std::uint32_t hash=2166136261u;
    if(p) while(*p) { hash^=static_cast<unsigned char>(*p++); hash*=16777619u; }
    return hash;
}
bool encodeState(const SavedState& s, std::array<std::uint8_t,kStateBytes>& out) {
    if(!validSettings(s.settings) || s.count>kMaxFavorites) return false;
    for(unsigned i=0;i<s.count;++i) {
        if(!validFavorite(s.favorites[i])) return false;
        for(unsigned j=0;j<i;++j) if(sameFavorite(s.favorites[j],s.favorites[i])) return false;
    }
    out.fill(0); std::memcpy(out.data(), "LOFI",4); out[4]=1; out[5]=kStateBytes;
    const auto& v=s.settings;
    out[8]=v.volume; out[9]=v.brightness; out[10]=v.texture; out[11]=v.motion; out[12]=v.engine; out[13]=v.mood; out[14]=s.count;
    for(unsigned i=0;i<s.count;++i) {
        auto* p=out.data()+16+i*16; const auto& f=s.favorites[i];
        put32(p,static_cast<std::uint32_t>(f.seed)); put32(p+4,static_cast<std::uint32_t>(f.seed>>32)); put32(p+8,f.bankFingerprint); p[12]=f.mood; p[13]=f.engine; p[14]=f.texture; p[15]=f.schema;
    }
    put32(out.data()+156,crc32(out.data(),156)); return true;
}
bool decodeState(const std::uint8_t* p, std::size_t size, SavedState& dst) {
    if(!p || size!=kStateBytes || std::memcmp(p,"LOFI",4)!=0 || p[4]!=1 || p[5]!=kStateBytes || get32(p+156)!=crc32(p,156)) return false;
    if(p[6] || p[7] || p[15]) return false;
    for(std::size_t i=144;i<156;++i) if(p[i]) return false;
    SavedState s; s.settings={p[8],p[9],p[10],p[11],p[12],p[13]}; s.count=p[14];
    if(!validSettings(s.settings) || s.count>kMaxFavorites) return false;
    for(unsigned i=0;i<s.count;++i) {
        const auto* r=p+16+i*16; s.favorites[i]={std::uint64_t(get32(r))|(std::uint64_t(get32(r+4))<<32),get32(r+8),r[12],r[13],r[14],r[15]};
        if(!validFavorite(s.favorites[i])) return false;
        for(unsigned j=0;j<i;++j) {
            if(sameFavorite(s.favorites[j],s.favorites[i])) return false;
        }
    }
    for(std::size_t i=16+s.count*16;i<144;++i) if(p[i]) return false;
    dst=s; return true;
}
int findFavorite(const SavedState& s,const Favorite& f) {
    for(unsigned i=0;i<s.count && i<kMaxFavorites;++i) {
        const auto& a=s.favorites[i];
        if(sameFavorite(a,f)) return static_cast<int>(i);
    }
    return -1;
}
bool addFavorite(SavedState& s,const Favorite& f) {
    if(s.count>=kMaxFavorites || !validFavorite(f) || findFavorite(s,f)>=0) return false;
    s.favorites[s.count++]=f; return true;
}
bool removeFavorite(SavedState& s,std::size_t i) {
    if(s.count>kMaxFavorites || i>=s.count) return false;
    for(std::size_t j=i+1;j<s.count;++j) s.favorites[j-1]=s.favorites[j];
    s.favorites[--s.count]={}; return true;
}
}
