#include "p33_bandage.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>

static void AddChunk(std::vector<uint8_t>& b,const char* id,int size,int count,const std::vector<uint8_t>& data){
    p33::PskChunk h{};std::strncpy(h.id,id,19);h.dataSize=size;h.dataCount=count;
    const auto old=b.size();b.resize(old+sizeof(h)+data.size());std::memcpy(b.data()+old,&h,sizeof(h));
    std::memcpy(b.data()+old+sizeof(h),data.data(),data.size());
}
int main(){
    using namespace p33;
    assert(HolsterVisibleCount(0,false)==0);
    assert(HolsterVisibleCount(3,false)==3);
    assert(HolsterVisibleCount(3,true)==2);
    assert(HolsterVisibleCount(1,true)==0);
    assert(Near({0,0,0},{.1f,0,0},.2f));
    assert(!Near({0,0,0},{.3f,0,0},.2f));

    WrapTracker w;const p20::V elbow{0,0,0},wrist{0,.25f,0};
    bool complete=false;uint64_t tick=1000;
    for(int i=0;i<=90&&!complete;++i){
        const float a=float(i)/90.0f*2.0f*Pi*1.5f;
        const float y=.12f;
        p20::V hand{std::cos(a)*.10f,y,std::sin(a)*.10f};
        complete=w.Update(elbow,wrist,hand,tick,1.4f);tick+=16;
    }
    assert(complete);assert(w.Progress(1.4f)>.99f);

    // Synthetic PSK triangle exercises 16-bit wedges/faces.
    std::vector<uint8_t> file;
    std::vector<uint8_t> pts(36);float p[9]={0,0,0, 1,0,0, 0,1,0};std::memcpy(pts.data(),p,sizeof(p));
    AddChunk(file,"PNTS0000",12,3,pts);
    std::vector<uint8_t> wed(48);
    for(int i=0;i<3;++i){uint16_t idx=uint16_t(i);std::memcpy(wed.data()+i*16,&idx,2);
        float uv[2]={i==1?1.f:0.f,i==2?1.f:0.f};std::memcpy(wed.data()+i*16+4,uv,8);}
    AddChunk(file,"VTXW0000",16,3,wed);
    std::vector<uint8_t> face(12);uint16_t wi[3]={0,1,2};std::memcpy(face.data(),wi,6);
    AddChunk(file,"FACE0000",12,1,face);
    std::vector<PskVertex> mesh;assert(ParsePsk(file,mesh));assert(mesh.size()==3);
    for(const auto& v:mesh){assert(p20::Valid(v.p));assert(p20::Valid(v.n));}
    return 0;
}
