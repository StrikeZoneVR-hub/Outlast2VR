#pragma once
#include <cstdint>
#include <cmath>
namespace p15 {
inline bool RotateInput(float& forward,float& strafe,float yaw){
    if(!std::isfinite(forward)||!std::isfinite(strafe)||!std::isfinite(yaw)||
       std::fabs(forward)>100000||std::fabs(strafe)>100000)return false;
    const float c=std::cos(yaw),s=std::sin(yaw),f=forward,r=strafe;
    forward=c*f-s*r;strafe=s*f+c*r;return true;
}
inline int32_t RotatorUnits(float radians){
    if(!std::isfinite(radians))return 0;
    constexpr double kUnitsPerRadian=10430.3783504704527; // 65536 / (2*pi)
    return static_cast<int32_t>(std::llround(static_cast<double>(radians)*kUnitsPerRadian));
}
inline int32_t AddRotatorUnits(int32_t value,int32_t delta){
    return static_cast<int32_t>(static_cast<uint32_t>(value)+static_cast<uint32_t>(delta));
}
inline int32_t RemoveRotatorUnits(int32_t value,int32_t delta){
    return static_cast<int32_t>(static_cast<uint32_t>(value)-static_cast<uint32_t>(delta));
}
}
