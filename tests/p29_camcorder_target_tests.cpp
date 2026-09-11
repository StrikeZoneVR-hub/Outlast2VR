#include "p29_camcorder_target.h"
#include <cstdio>
int main() {
    using p29::MatchesNativeTarget;
    // Always-on checks: assert would be disabled in RelWithDebInfo.
    if(!MatchesNativeTarget(512,256,1,512,256,true,false))return 1;
    if(!MatchesNativeTarget(1024,1024,1,1024,1024,true,false))return 2;
    if(MatchesNativeTarget(512,256,1,1024,512,true,false))return 3;
    if(MatchesNativeTarget(512,256,1,0,0,true,false))return 4;
    if(MatchesNativeTarget(512,256,4,512,256,true,false))return 5;
    if(MatchesNativeTarget(512,256,1,512,256,false,false))return 6;
    if(MatchesNativeTarget(512,256,1,512,256,true,true))return 7;
    if(MatchesNativeTarget(8192,8192,1,8192,8192,true,false))return 8;
    if(MatchesNativeTarget(32,32,1,32,32,true,false))return 9;
    // No UI-shader argument: unknown shaders cannot exclude the target.
    std::puts("P29B native dimensions, mismatches, missing target, MRT, backbuffer, MSAA and size bounds passed.");
    return 0;
}
