#pragma once
#include "p20_ik.h"
#include <cstdint>
namespace p25 {
#pragma pack(push,4)
// USkeletalMeshComponent attachment record, verified in native AttachComponent.
struct Attachment {
    uint64_t component=0,bone=0;
    p20::V translation{};
    int32_t pitch=0,yaw=0,roll=0;
    p20::V scale{1,1,1};
};
#pragma pack(pop)
static_assert(sizeof(Attachment)==0x34);
inline bool Holster(Attachment& out,const p20::Skeleton& before,const p20::Skeleton& after,float units,uint64_t rootName){
    if(!std::isfinite(units)||units<10||units>1000)return false;
    const auto& root=after[0];
    if(!p20::Valid(root.q)||!p20::Valid(root.p)||std::fabs(root.scale-1)>.01f)return false;
    // Recover body yaw from the hip rotation, without changing either hand or body.
    auto delta=p20::Mul(after[2].q,p20::Conjugate(before[2].q));
    auto forward=p20::Rotate(delta,p20::Rotate(before[1].q,{1,0,0}));forward.z=0;
    if(!p20::Valid(forward)||p20::Length(forward)<.1f)return false;
    forward=p20::Unit(forward);auto right=p20::Cross({0,0,1},forward);
    auto position=after[2].p+right*(.23f*units)+forward*(.02f*units)+p20::V{0,0,-.06f*units};
    out.translation=p20::Rotate(p20::Conjugate(root.q),position-root.p);
    auto localForward=p20::Rotate(p20::Conjugate(root.q),forward);
    out.bone=rootName;out.pitch=-16384;out.roll=0;
    out.yaw=static_cast<int32_t>(std::lround(std::atan2(localForward.y,localForward.x)*32768.f/3.14159265359f));
    out.scale={1,1,1};
    return p20::Valid(out.translation)&&p20::Length(out.translation)<10*units;
}
}
