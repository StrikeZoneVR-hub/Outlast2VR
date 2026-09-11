#pragma once
#include "p20_ik.h"
#include <cstdint>
namespace p31 {
struct Rotator {int32_t pitch{},yaw{},roll{};};
// UE3 rotators use 65536 units per full turn. Keep the lens roll, unlike
// the deliberately pitch-locked locomotion camera.
inline Rotator ToRotator(p20::Q q){
    const auto x=p20::Rotate(q,{1,0,0});
    const auto y=p20::Rotate(q,{0,1,0});
    const auto z=p20::Rotate(q,{0,0,1});
    const float yaw=std::atan2(x.y,x.x);
    const float pitch=std::atan2(x.z,std::sqrt(x.x*x.x+x.y*x.y));
    const p20::V side{-std::sin(yaw),std::cos(yaw),0};
    const float roll=std::atan2(p20::Dot(z,side),p20::Dot(y,side));
    constexpr float units=65536.f/6.283185307179586f;
    return {static_cast<int32_t>(std::lround(pitch*units)),static_cast<int32_t>(std::lround(yaw*units)),static_cast<int32_t>(std::lround(roll*units))};
}
inline bool LensPose(p20::V origin,p20::Q rotation,p20::V& lens,Rotator& rot){
    if(!p20::Valid(origin)||!p20::Valid(rotation))return false;
    lens=origin;rot=ToRotator(rotation);return true;
}
}
