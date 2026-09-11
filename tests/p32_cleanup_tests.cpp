#include "p32_cleanup.h"
#include <cassert>
#include <cmath>

int main(){
    using namespace p32;
    assert(std::fabs(ClampHudScale(0.72f)-0.72f)<1e-6f);
    assert(std::fabs(ClampHudScale(0.01f)-0.35f)<1e-6f);
    assert(std::fabs(ClampHudScale(9.f)-1.25f)<1e-6f);
    assert(std::fabs(ClampHudDistance(1.55f)-1.55f)<1e-6f);
    assert(SelectMirrorEye(true,100,true,101,103)==1);
    assert(SelectMirrorEye(false,0,true,101,103)==0);
    assert(SelectMirrorEye(true,90,true,91,103)==-1);
    assert(SameMirrorSurface(1920,1080,28,1,1,1,1920,1080,28,1,1,1));
    assert(!SameMirrorSurface(1920,1080,28,1,1,1,1920,1080,29,1,1,1));
    return 0;
}
