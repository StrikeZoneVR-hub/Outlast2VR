#pragma once
#include "p13_ui_classifier.h"
#include <vector>
#include <map>

namespace p14 {
// Match executable instructions exactly, independent of optional RDEF/debug data.
// No size-only, name-only, or approximate instruction matches.
inline std::vector<uint8_t> Instructions(const void* bytes,size_t size) {
    auto p=static_cast<const uint8_t*>(bytes);
    auto read=[&](size_t o,uint32_t& v){if(o>size||size-o<4)return false;std::memcpy(&v,p+o,4);return true;};
    uint32_t total=0,count=0;
    if(!p||size<32||std::memcmp(p,"DXBC",4)||!read(24,total)||total!=size||!read(28,count)||count>32||32+4*count>size)return {};
    std::vector<uint8_t> result;
    for(uint32_t i=0;i<count;++i){
        uint32_t o=0,n=0;
        if(!read(32+4*i,o)||o>size||size-o<8||!read(o+4,n)||n>size-o-8)return {};
        if(!std::memcmp(p+o,"SHEX",4)||!std::memcmp(p+o,"SHDR",4)){
            if(!result.empty()||n<8)return {};
            result.assign(p+o+8,p+o+8+n);
        }
    }
    return result;
}
struct UiRegistry {
    std::map<std::vector<uint8_t>,p13::UiShader> known;
    void Add(const void* bytes,size_t size){
        auto key=Instructions(bytes,size);if(key.empty())return;
        auto kind=p13::ClassifyUiShader(bytes,size);
        auto [it,inserted]=known.emplace(std::move(key),kind);
        if(!inserted&&it->second!=kind)it->second=p13::UiShader::None;
    }
    p13::UiShader Find(const void* bytes,size_t size)const{
        auto key=Instructions(bytes,size);auto it=known.find(key);
        return it==known.end()?p13::UiShader::None:it->second;
    }
};
}
