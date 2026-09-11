#pragma once
#include "p20_ik.h"
namespace p22 {
inline bool ScriptedMovementState(unsigned char state){
    return state==3||state==4||state==5||state==6||state==9||state==16||state==17;
}
// Keep a tracked head inside a small body-relative volume.  `body` is the
// horizontal room-space displacement already consumed by the game body.  The
// vertical origin remains the calibrated eye height because the native pawn is
// never moved vertically by room-scale tracking.
inline p20::V BoundHeadToBody(p20::V head,p20::V body,float horizontalRadius,float verticalRadius){
    if(!p20::Valid(head)||!p20::Valid(body)||!std::isfinite(horizontalRadius)||
       !std::isfinite(verticalRadius)||horizontalRadius<=0||verticalRadius<=0)return body;
    auto relative=head-body;
    const float horizontal=std::sqrt(relative.x*relative.x+relative.z*relative.z);
    if(horizontal>horizontalRadius){
        const float scale=horizontalRadius/horizontal;
        relative.x*=scale;relative.z*=scale;
    }
    return {body.x+relative.x,std::clamp(head.y,-verticalRadius,verticalRadius),body.z+relative.z};
}
// Input/output are recentered OpenXR metres. Preserve a small leaning radius.
inline p20::V Request(p20::V head,p20::V consumed,float dt,float units){
    if(!p20::Valid(head)||!p20::Valid(consumed)||!std::isfinite(dt)||dt<=0||dt>.1f||!std::isfinite(units)||units<10||units>1000)return {};
    auto d=head-consumed;d.y=0;const float n=p20::Length(d);
    if(n<=.08f||n>1.f)return {};
    return d*(std::min(n-.08f,std::min(.10f,2.f*dt))/n);
}
inline p20::V ToWorld(p20::V p,p20::V right,p20::V forward,float units){return (right*p.x-forward*p.z)*units;}
inline p20::V ToLocal(p20::V p,p20::V right,p20::V forward,float units){return {p20::Dot(p,right)/units,0,-p20::Dot(p,forward)/units};}
}
