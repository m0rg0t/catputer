#include "lofi/state.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace {
bool sameFavorite(const lofi::Favorite& a,const lofi::Favorite& b) {
    return a.seed==b.seed && a.bankFingerprint==b.bankFingerprint && a.mood==b.mood &&
           a.engine==b.engine && a.texture==b.texture && a.schema==b.schema;
}
bool sameState(const lofi::SavedState& a,const lofi::SavedState& b) {
    if(a.count!=b.count || a.settings.volume!=b.settings.volume ||
       a.settings.brightness!=b.settings.brightness || a.settings.texture!=b.settings.texture ||
       a.settings.motion!=b.settings.motion || a.settings.engine!=b.settings.engine ||
       a.settings.mood!=b.settings.mood) return false;
    for(unsigned i=0;i<lofi::kMaxFavorites;++i) if(!sameFavorite(a.favorites[i],b.favorites[i])) return false;
    return true;
}
}

int main() {
    using namespace lofi;
    SavedState state;
    for(unsigned i=0;i<8;++i) assert(addFavorite(state,{i,42,static_cast<std::uint8_t>(i%3),0,15,kSessionSchema}));
    assert(!addFavorite(state,{123,42,0,0,15,kSessionSchema}));
    std::array<std::uint8_t,kStateBytes> data{};
    assert(encodeState(state,data));
    SavedState decoded; assert(decodeState(data.data(),data.size(),decoded));
    assert(decoded.count==8 && decoded.favorites[7].seed==7);
    for(unsigned i=0;i<data.size();++i) {
        auto corrupt=data; corrupt[i]^=0x80;
        assert(!decodeState(corrupt.data(),corrupt.size(),decoded)); assert(decoded.count==8);
    }
    assert(!decodeState(data.data(),data.size()-1,decoded));
    assert(!decodeState(nullptr,data.size(),decoded));

    SavedState legacy;
    legacy.settings.volume=65;
    for(std::uint8_t schema=1;schema<kSessionSchema;++schema)
        assert(addFavorite(legacy,{42,0,2,0,15,schema}));
    assert(encodeState(legacy,data));
    assert(decodeState(data.data(),data.size(),decoded));
    assert(decoded.settings.volume==65 && decoded.count==kSessionSchema-1);
    for(std::uint8_t schema=1;schema<kSessionSchema;++schema)
        assert(decoded.favorites[schema-1].schema==schema);
    assert(encodeState(decoded,data)); // Saving current settings preserves the old record.
    assert(!addFavorite(legacy,{99,0,2,0,15,static_cast<std::uint8_t>(kSessionSchema+1)}));

    SavedState duplicate;
    duplicate.count=2;
    duplicate.favorites[0]={99,42,1,0,15,kSessionSchema};
    duplicate.favorites[1]=duplicate.favorites[0];
    assert(!encodeState(duplicate,data));

    SavedState distinct;
    assert(addFavorite(distinct,{99,42,1,0,15,kSessionSchema}));
    assert(addFavorite(distinct,{100,42,1,0,15,kSessionSchema}));
    assert(encodeState(distinct,data));
    std::copy_n(data.begin()+16,16,data.begin()+32);
    const auto duplicateCrc=crc32(data.data(),156);
    for(unsigned i=0;i<4;++i) data[156+i]=static_cast<std::uint8_t>(duplicateCrc>>(8*i));
    const auto unchanged=decoded;
    assert(!decodeState(data.data(),data.size(),decoded));
    assert(sameState(decoded,unchanged));

    assert(removeFavorite(state,0)); assert(state.favorites[0].seed==1 && state.count==7);
    assert(!addFavorite(state,state.favorites[0]));
    assert(!removeFavorite(state,8));
    state.settings.volume=101; assert(!encodeState(state,data));
    std::cout<<"state: roundtrip, corruption, bounds and save validation passed\n";
}
