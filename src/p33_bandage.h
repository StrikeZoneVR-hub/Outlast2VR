#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "p20_ik.h"

namespace p33 {
constexpr float Pi=3.14159265358979323846f;

inline float WrapAngle(float a){return std::atan2(std::sin(a),std::cos(a));}
inline int HolsterVisibleCount(int gameCount,bool equipped){
    gameCount=std::clamp(gameCount,0,3);
    return std::max(0,gameCount-(equipped?1:0));
}
inline bool Near(p20::V a,p20::V b,float radius){
    return std::isfinite(radius)&&radius>0&&p20::Length(a-b)<=radius;
}
inline p20::V EstimatedShoulder(p20::V head){
    return head+p20::V{-0.18f,-0.17f,0.02f};
}
inline p20::V EstimatedElbow(p20::V head,p20::V wrist){
    const auto shoulder=EstimatedShoulder(head);
    // Bias slightly downward so wrapping around the forearm remains usable when
    // the tracked hand is close to the chest.
    return shoulder+(wrist-shoulder)*0.52f+p20::V{0,-0.045f,0};
}
struct WrapTracker {
    bool valid=false;
    float previous=0;
    float accumulated=0;
    uint64_t lastTick=0;
    void Reset(){valid=false;previous=0;accumulated=0;lastTick=0;}
    bool Update(p20::V elbow,p20::V wrist,p20::V hand,uint64_t tick,float turns=1.45f){
        const auto axisRaw=wrist-elbow;const float length=p20::Length(axisRaw);
        if(length<0.08f||length>0.65f){Reset();return false;}
        const auto axis=p20::Unit(axisRaw);
        const auto along=p20::Dot(hand-elbow,axis);
        if(along<-0.07f||along>length+0.07f){valid=false;return false;}
        auto radial=hand-(elbow+axis*std::clamp(along,0.0f,length));
        const float radius=p20::Length(radial);
        if(radius<0.045f||radius>0.30f){valid=false;return false;}
        auto u=p20::Cross(axis,std::fabs(axis.y)<.8f?p20::V{0,1,0}:p20::V{1,0,0});
        u=p20::Unit(u);const auto v=p20::Cross(axis,u);
        const float angle=std::atan2(p20::Dot(radial,v),p20::Dot(radial,u));
        if(!valid||!lastTick||tick<lastTick||tick-lastTick>300){
            valid=true;previous=angle;lastTick=tick;return false;
        }
        const float delta=WrapAngle(angle-previous);previous=angle;lastTick=tick;
        // Ignore teleport-sized samples, but allow either wrapping direction.
        if(std::fabs(delta)<1.1f)accumulated+=std::fabs(delta);
        return accumulated>=std::max(0.5f,turns)*2.0f*Pi;
    }
    float Progress(float turns=1.45f)const{
        const float target=std::max(0.5f,turns)*2.0f*Pi;
        return std::clamp(accumulated/target,0.0f,1.0f);
    }
};

struct PskVertex {p20::V p;float u=0,v=0;p20::V n{0,1,0};};
#pragma pack(push,1)
struct PskChunk {char id[20];int32_t typeFlag;int32_t dataSize;int32_t dataCount;};
#pragma pack(pop)
inline std::string ChunkName(const PskChunk& h){
    size_t n=0;while(n<20&&h.id[n])++n;return std::string(h.id,h.id+n);
}
template<class T> inline bool ReadAt(const std::vector<uint8_t>& b,size_t at,T& value){
    if(at>b.size()||sizeof(T)>b.size()-at)return false;
    std::memcpy(&value,b.data()+at,sizeof(T));return true;
}
inline bool ParsePsk(const std::vector<uint8_t>& bytes,std::vector<PskVertex>& out){
    struct Wedge {uint32_t point=0;float u=0,v=0;};
    struct Face {uint32_t w[3]{};};
    std::vector<p20::V> points;std::vector<Wedge> wedges;std::vector<Face> faces;
    size_t at=0;
    while(at+sizeof(PskChunk)<=bytes.size()){
        PskChunk h{};std::memcpy(&h,bytes.data()+at,sizeof(h));at+=sizeof(h);
        if(h.dataSize<0||h.dataCount<0||h.dataSize>1024||h.dataCount>10000000)return false;
        const uint64_t payload=uint64_t(uint32_t(h.dataSize))*uint64_t(uint32_t(h.dataCount));
        if(payload>bytes.size()-at)return false;
        const auto name=ChunkName(h);
        if(name=="PNTS0000"&&h.dataSize>=12){
            points.reserve(h.dataCount);
            for(int i=0;i<h.dataCount;++i){
                float xyz[3]{};std::memcpy(xyz,bytes.data()+at+size_t(i)*h.dataSize,12);
                points.push_back({xyz[0],xyz[1],xyz[2]});
            }
        }else if((name=="VTXW0000"||name=="VTXW3200")&&h.dataSize>=12){
            wedges.reserve(h.dataCount);
            for(int i=0;i<h.dataCount;++i){
                const uint8_t* p=bytes.data()+at+size_t(i)*h.dataSize;
                uint32_t p32=0;uint16_t p16=0;std::memcpy(&p32,p,4);std::memcpy(&p16,p,2);
                Wedge w{};
                if(name=="VTXW3200"&&p32<points.size()){w.point=p32;std::memcpy(&w.u,p+4,4);std::memcpy(&w.v,p+8,4);}
                else {w.point=p16;std::memcpy(&w.u,p+4,4);std::memcpy(&w.v,p+8,4);}
                wedges.push_back(w);
            }
        }else if((name=="FACE0000"||name=="FACE3200")&&h.dataSize>=8){
            faces.reserve(h.dataCount);
            for(int i=0;i<h.dataCount;++i){
                const uint8_t* p=bytes.data()+at+size_t(i)*h.dataSize;Face f{};
                if(name=="FACE3200"&&h.dataSize>=12){std::memcpy(f.w,p,12);}
                else {uint16_t q[3]{};std::memcpy(q,p,6);f.w[0]=q[0];f.w[1]=q[1];f.w[2]=q[2];}
                faces.push_back(f);
            }
        }
        at+=size_t(payload);
    }
    if(points.empty()||wedges.empty()||faces.empty())return false;
    p20::V lo=points[0],hi=points[0];
    for(const auto& p:points){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
    const auto center=(lo+hi)*.5f;const float extent=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,1e-4f});
    const float scale=0.105f/extent; // normalize longest dimension to ~10.5 cm
    out.clear();out.reserve(faces.size()*3);
    for(const auto& f:faces){
        PskVertex tri[3]{};bool ok=true;
        for(int k=0;k<3;++k){
            if(f.w[k]>=wedges.size()||wedges[f.w[k]].point>=points.size()){ok=false;break;}
            const auto& w=wedges[f.w[k]];const auto p=(points[w.point]-center)*scale;
            // Unreal X-forward/Y-right/Z-up -> OpenXR right/up/back.
            tri[k].p={p.y,p.z,-p.x};tri[k].u=w.u;tri[k].v=w.v;
        }
        if(!ok)continue;
        const auto n=p20::Unit(p20::Cross(tri[1].p-tri[0].p,tri[2].p-tri[0].p),{0,1,0});
        for(auto& v:tri){v.n=n;out.push_back(v);}
    }
    return out.size()>=3;
}
inline void MakeFallbackRoll(std::vector<PskVertex>& out,int segments=28){
    out.clear();segments=std::clamp(segments,8,96);
    const float radius=.032f,half=.045f;
    auto add=[&](p20::V a,p20::V b,p20::V c,float ua,float va,float ub,float vb,float uc,float vc){
        auto n=p20::Unit(p20::Cross(b-a,c-a),{0,1,0});
        out.push_back({a,ua,va,n});out.push_back({b,ub,vb,n});out.push_back({c,uc,vc,n});
    };
    for(int i=0;i<segments;++i){
        const float a=2*Pi*i/segments,b=2*Pi*(i+1)/segments;
        p20::V p0{-half,std::cos(a)*radius,std::sin(a)*radius},p1{half,std::cos(a)*radius,std::sin(a)*radius};
        p20::V p2{half,std::cos(b)*radius,std::sin(b)*radius},p3{-half,std::cos(b)*radius,std::sin(b)*radius};
        add(p0,p1,p2,float(i)/segments,1,float(i)/segments,0,float(i+1)/segments,0);
        add(p0,p2,p3,float(i)/segments,1,float(i+1)/segments,0,float(i+1)/segments,1);
    }
}
}
