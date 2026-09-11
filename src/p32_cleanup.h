#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace p32 {
inline float ClampHudScale(float value){
    return std::isfinite(value)?std::clamp(value,0.35f,1.25f):0.72f;
}
inline float ClampHudDistance(float value){
    return std::isfinite(value)?std::clamp(value,0.75f,3.0f):1.55f;
}
inline int SelectMirrorEye(bool rightAvailable,uint64_t rightFrame,
                           bool leftAvailable,uint64_t leftFrame,uint64_t now){
    const auto fresh=[&](bool available,uint64_t frame){return available&&now>=frame&&now-frame<=4;};
    if(fresh(rightAvailable,rightFrame))return 1;
    if(fresh(leftAvailable,leftFrame))return 0;
    return -1;
}
inline bool SameMirrorSurface(uint32_t sw,uint32_t sh,uint32_t sf,uint32_t sa,uint32_t sm,uint32_t ss,
                              uint32_t dw,uint32_t dh,uint32_t df,uint32_t da,uint32_t dm,uint32_t ds){
    return sw==dw&&sh==dh&&sf==df&&sa==1&&da==1&&sm==1&&dm==1&&ss==ds;
}
}
