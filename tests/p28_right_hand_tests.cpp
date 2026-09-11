#include "../src/p28_right_hand.h"
#include <cstdio>
using namespace p20;
bool Near(V a,V b){return Length(a-b)<.0001f;}
V ToHero(V v){return {-v.z,v.x,v.y};}
int main(){
    // Grip with the tube pointing up: fingers forward, thumb up, handshake.
    const Q neutral{.7071067811865475f,0,0,.7071067811865475f};
    const auto hand=p28::RightGripTarget(neutral,{});
    const auto left=p28::LeftGripTarget(neutral,{});
    if(!Near(Rotate(left,{1,0,0}),{1,0,0})||!Near(Rotate(left,{0,0,-1}),{0,0,1}))return 7;
    if(!Near(Rotate(hand,{1,0,0}),{1,0,0})||!Near(Rotate(hand,{0,0,1}),{0,0,1}))return 1;
    const Q rotations[]={Q{},neutral,Normalize({.2f,.3f,.1f,.6f}),Normalize({-.7f,.1f,-.4f,.3f})};
    for(auto q:rotations){
        const auto leftTarget=p28::LeftGripTarget(q,{});
        if(!Near(Rotate(leftTarget,{1,0,0}),ToHero(Rotate(q,{0,-1,0}))))return 8;
        if(!Near(Rotate(leftTarget,{0,0,-1}),ToHero(Rotate(q,{0,0,-1}))))return 9;
        const auto target=p28::RightGripTarget(q,{});
        if(!Valid(target))return 2;
        if(!Near(Rotate(target,{1,0,0}),ToHero(Rotate(q,{0,-1,0}))))return 3;
        if(!Near(Rotate(target,{0,0,1}),ToHero(Rotate(q,{0,0,-1}))))return 4;
        // Changing the reference yaw and the mesh basis together must not
        // change the physical hand's world orientation after recentering.
        const Q center{0,.382683432f,0,.923879533f};
        const auto recentered=p28::RightGripTarget(Mul(Conjugate(center),q),GripCanonical(center));
        const auto leftRecentered=p28::LeftGripTarget(Mul(Conjugate(center),q),GripCanonical(center));
        if(!Near(Rotate(leftTarget,{1,0,0}),Rotate(leftRecentered,{1,0,0}))||
           !Near(Rotate(leftTarget,{0,0,-1}),Rotate(leftRecentered,{0,0,-1})))return 10;
        if(!Near(Rotate(target,{1,0,0}),Rotate(recentered,{1,0,0})))return 5;
        if(!Near(Rotate(target,{0,0,1}),Rotate(recentered,{0,0,1})))return 6;
    }
    std::puts("P28 right wrist: neutral handshake, grip axes, arbitrary rotations and recenter covariance passed.");
}
