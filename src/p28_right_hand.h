#pragma once
#include "p20_ik.h"
namespace p28 {
// OpenXR grip -Z runs from little finger toward thumb, not
// along the extended fingers. Hero_R_Hand uses +X along fingers
// and +Z toward the thumb (validated reference bone axes).
// After GripCanonical changes handedness/basis, the fixed +90 degree Y
// rotation maps bone +X to grip -Y and bone +Z to grip -Z.
// No captured startup pose: putting the controller down during loading must
// not change its anatomical alignment for the rest of the session.
inline p20::Q RightGripOffset(){return {0,.7071067811865475f,0,.7071067811865475f};}
// Captured Hero_L_Hand finger chains extend +X, but the thumb is on -Z
// (opposite Hero_R_Hand). Rotate the left bone frame 180 degrees about +X.
inline p20::Q LeftGripOffset(){return p20::Mul(RightGripOffset(),{1,0,0,0});}
inline p20::Q LeftGripTarget(p20::Q grip,p20::Q basis){return p20::GripTarget(grip,basis,LeftGripOffset());}
inline p20::Q RightGripTarget(p20::Q grip,p20::Q basis){
    return p20::GripTarget(grip,basis,RightGripOffset());
}
}
