#include "../src/p22_roomscale.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void Check(bool condition,const char* label){if(!condition)throw std::runtime_error(label);}
static bool Near(float a,float b,float epsilon=1e-5f){return std::fabs(a-b)<=epsilon;}

int main()try{
    const p20::V body{0.25f,0.0f,-0.10f};
    auto inside=p22::BoundHeadToBody({0.30f,0.12f,-0.14f},body,0.10f,0.18f);
    Check(Near(inside.x,0.30f)&&Near(inside.y,0.12f)&&Near(inside.z,-0.14f),"natural 6DoF preserved");

    auto outside=p22::BoundHeadToBody({1.25f,0.60f,-0.10f},body,0.10f,0.18f);
    Check(Near(outside.x,0.35f)&&Near(outside.y,0.18f)&&Near(outside.z,-0.10f),"walk-away clamped relative to body");

    auto opposite=p22::BoundHeadToBody({-0.75f,-0.60f,-0.10f},body,0.10f,0.18f);
    Check(Near(opposite.x,0.15f)&&Near(opposite.y,-0.18f)&&Near(opposite.z,-0.10f),"opposite walk-away clamped relative to body");

    const auto bad=p22::BoundHeadToBody({NAN,0,0},body,0.10f,0.18f);
    Check(Near(bad.x,body.x)&&Near(bad.y,body.y)&&Near(bad.z,body.z),"invalid tracking fails closed at body");
    Check(p22::ScriptedMovementState(9),"opening helicopter state remains native");
    Check(!p22::ScriptedMovementState(0)&&!p22::ScriptedMovementState(1)&&!p22::ScriptedMovementState(2),
        "ordinary movement remains tracked");
    Check(!p22::ScriptedMovementState(29)&&!p22::ScriptedMovementState(30),
        "school movement states remain non-cinematic and eligible for tracked arms");
    std::puts("PASS: body-relative neck volume preserves local 6DoF and blocks headset/body separation.");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
