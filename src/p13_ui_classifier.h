#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace p13 {
enum class UiShader : uint32_t { None, Scaleform2D, Scaleform3D, Canvas };
// Bounded RDEF parser. Never classify by buffer size alone: the shader must
// declare the exact standalone UI transform family found in this game's cache.
inline UiShader ClassifyUiShader(const void* data,size_t size) {
    const auto* p=static_cast<const uint8_t*>(data);
    auto read=[](const uint8_t* b,size_t n,size_t at,uint32_t& value) {
        if(at>n || n-at<4)return false; std::memcpy(&value,b+at,4);return true;
    };
    if(!p || size<32 || std::memcmp(p,"DXBC",4))return UiShader::None;
    uint32_t total=0,count=0;
    if(!read(p,size,24,total)||total>size||total<32||!read(p,total,28,count)||count>32||32+count*4>total)return UiShader::None;
    const uint8_t* rd=nullptr; uint32_t rn=0; bool vertex=false;
    for(uint32_t i=0;i<count;++i) {
        uint32_t o=0,n=0;
        if(!read(p,total,32+4*i,o)||o>total||total-o<8||!read(p,total,o+4,n)||n>total-o-8)return UiShader::None;
        if(!std::memcmp(p+o,"RDEF",4)){rd=p+o+8;rn=n;}
        if(!std::memcmp(p+o,"SHEX",4)||!std::memcmp(p+o,"SHDR",4)) {
            uint32_t token=0; if(read(p+o+8,n,0,token))vertex=(token>>16)==1;
        }
    }
    if(!rd||!vertex)return UiShader::None;
    auto equals=[&](uint32_t o,const char* text) {
        const size_t len=std::strlen(text)+1;
        return o<=rn&&len<=rn-o&&!std::memcmp(rd+o,text,len);
    };
    uint32_t buffers=0,table=0;
    if(!read(rd,rn,0,buffers)||buffers!=1||!read(rd,rn,4,table))return UiShader::None;
    uint32_t name=0,vars=0,vo=0,bytes=0;
    if(!read(rd,rn,table,name)||!equals(name,"$Globals")||!read(rd,rn,table+4,vars)||vars>32||
       !read(rd,rn,table+8,vo)||!read(rd,rn,table+12,bytes)||vo>rn||vars>(rn-vo)/40)return UiShader::None;
    bool scaleform=false; UiShader kind=UiShader::None;
    for(uint32_t i=0;i<vars;++i) {
        uint32_t no=0,start=0,n=0;
        if(!read(rd,rn,vo+40*i,no)||!read(rd,rn,vo+40*i+4,start)||!read(rd,rn,vo+40*i+8,n)||start>bytes||n>bytes-start)return UiShader::None;
        if(equals(no,"mvp")&&(n==32||n==64))kind=n==32?UiShader::Scaleform2D:UiShader::Scaleform3D;
        if(equals(no,"cxmul")||equals(no,"texgen")||equals(no,"vfuniforms"))scaleform=true;
        if(vars==1&&bytes==64&&start==0&&n==64&&(equals(no,"Transform")||equals(no,"c_Transform")))return UiShader::Canvas;
    }
    return scaleform?kind:UiShader::None;
}
}
