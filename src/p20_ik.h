#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace p20 {
struct V {float x=0,y=0,z=0;};
inline V operator+(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V operator-(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V operator*(V a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V Cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Length(V a){return std::sqrt(Dot(a,a));}
inline bool Valid(V a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline V Unit(V a,V fallback={1,0,0}){float l=Length(a);return l>1e-6f?a*(1/l):fallback;}
struct Q {float x=0,y=0,z=0,w=1;};
inline Q Conjugate(Q q){return {-q.x,-q.y,-q.z,q.w};}
inline Q Mul(Q a,Q b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Q Normalize(Q q){float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return n>1e-6f?Q{q.x/n,q.y/n,q.z/n,q.w/n}:Q{};}
inline V Rotate(Q q,V p){V v{q.x,q.y,q.z};return p+Cross(v,p)*(2*q.w)+Cross(v,Cross(v,p))*2;}
inline bool Valid(Q q){float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::fabs(n-1)<.02f;}
inline Q Between(V from,V to){
    from=Unit(from);to=Unit(to);float d=std::clamp(Dot(from,to),-1.f,1.f);
    if(d<-.9999f){V axis=Unit(Cross(from,std::fabs(from.z)<.9f?V{0,0,1}:V{0,1,0}));return {axis.x,axis.y,axis.z,0};}
    V c=Cross(from,to);return Normalize({c.x,c.y,c.z,1+d});
}
inline Q Fraction(Q q,float fraction){if(q.w<0)q={-q.x,-q.y,-q.z,-q.w};return Normalize({q.x*fraction,q.y*fraction,q.z*fraction,1+(q.w-1)*fraction});}
struct Atom {Q q;V p;float scale=1;};
static_assert(sizeof(Atom)==32);
using Skeleton=std::array<Atom,75>;
using Parents=std::array<int,75>;
inline bool Descendant(int bone,int root,const Parents& parents){for(int i=0;i<75&&bone>0;++i){if(bone==root)return true;bone=parents[bone];}return root==0;}
inline void TransformTree(Skeleton& s,const Parents& parents,int root,Q delta,V translation={},int count=75){
    const V pivot=s[root].p;
    for(int i=root;i<count;++i)if(Descendant(i,root,parents)){s[i].p=pivot+Rotate(delta,s[i].p-pivot)+translation;s[i].q=Normalize(Mul(delta,s[i].q));}
}
struct Limb {V joint,end;};
inline bool TwoBone(V root,V target,V pole,float a,float b,Limb& result){
    if(!Valid(root)||!Valid(target)||!Valid(pole)||!std::isfinite(a)||!std::isfinite(b)||a<.01f||b<.01f)return false;
    V aim=Unit(target-root);float distance=std::clamp(Length(target-root),std::fabs(a-b)+.001f,a+b-.001f);
    V bend=pole-root; bend=bend-aim*Dot(bend,aim);
    if(Length(bend)<.001f)bend=Cross(aim,std::fabs(aim.z)<.9f?V{0,0,1}:V{0,1,0});
    bend=Unit(bend);float x=(distance*distance+a*a-b*b)/(2*distance);
    float y=std::sqrt(std::max(0.f,a*a-x*x));result={root+aim*x+bend*y,root+aim*distance};return true;
}
inline bool SolveLimb(Skeleton& s,const Parents& parents,int upper,int lower,int end,V target,V pole,float extension=1,int count=75){
    const float a=Length(s[lower].p-s[upper].p),b=Length(s[end].p-s[lower].p);Limb limb;
    if(!std::isfinite(extension)||extension<1||extension>1.2f||a+b<.01f)return false;
    const float stretch=std::clamp(Length(target-s[upper].p)/(a+b),1.f,extension);
    if(stretch>1){
        const auto upperDelta=(s[lower].p-s[upper].p)*(stretch-1);
        const auto lowerDelta=(s[end].p-s[lower].p)*(stretch-1);
        TransformTree(s,parents,lower,{},upperDelta,count);TransformTree(s,parents,end,{},lowerDelta,count);
    }
    if(!TwoBone(s[upper].p,target,pole,a*stretch,b*stretch,limb))return false;
    TransformTree(s,parents,upper,Between(s[lower].p-s[upper].p,limb.joint-s[upper].p),{},count);
    TransformTree(s,parents,lower,Between(s[end].p-s[lower].p,limb.end-s[lower].p),{},count);return true;
}
inline Q UprightHand(V forward){
    forward.z=0;forward=Unit(forward);
    const Q aim=Between({1,0,0},forward);
    return Normalize(Mul(Between(Rotate(aim,{0,0,1}),{0,0,1}),aim));
}
inline Q GripCanonical(Q q){return Normalize({q.z,-q.x,-q.y,q.w});}
inline Q GripTarget(Q grip,Q basis,Q offset){return Normalize(Mul(Mul(basis,GripCanonical(grip)),offset));}
inline void OrientArm(Skeleton& s,const Parents& parents,int lower,int hand,Q target,int count=75){
    // Carry pronation/supination through the forearm. Swing remains at the wrist:
    // distributing swing would move the already-solved controller position.
    const V axis=Unit(s[hand].p-s[lower].p);
    const Q delta=Normalize(Mul(target,Conjugate(s[hand].q)));
    const float projection=Dot(V{delta.x,delta.y,delta.z},axis);
    // A 180-degree pure swing has no unique twist; retain the solved forearm.
    if(projection*projection+delta.w*delta.w>1e-8f){
        const Q twist=Normalize({axis.x*projection,axis.y*projection,axis.z*projection,delta.w});
        TransformTree(s,parents,lower,twist,{},count);
    }
    TransformTree(s,parents,hand,Normalize(Mul(target,Conjugate(s[hand].q))),{},count);
}
inline float WrapYaw(float a){return std::atan2(std::sin(a),std::cos(a));}
struct Targets {V head,left,right;Q headRotation,leftRotation,rightRotation;bool roomscaleFollow=true;float bodyYaw=0;bool eyeAnchored=false;float armExtension=1;bool leftTracked=true,rightTracked=true;};
inline void RebaseHelper(Skeleton& s,const Skeleton& original,const Parents& parents,int helper,int driver,int count=75){
    if(helper<0||driver<0||helper>=count||driver>=count)return;
    const Q delta=Normalize(Mul(s[driver].q,Conjugate(original[driver].q)));
    for(int i=helper;i<count;++i)if(Descendant(i,helper,parents)){
        s[i].p=s[driver].p+Rotate(delta,original[i].p-original[driver].p);
        s[i].q=Normalize(Mul(delta,original[i].q));s[i].scale=original[i].scale;
    }
}
inline void RepairSkinHelpers(Skeleton& s,const Skeleton& original,const Parents& parents,int count=75){
    // These are sibling/auxiliary skinning chains, not descendants of the joint
    // whose deformation they support. Preserve their original driver-relative pose.
    for(const auto pair:std::array<std::array<int,2>,10>{{{10,12},{36,38},{33,9},{59,35},{64,61},{69,66},{70,60},{71,65},{72,39},{73,13}}})
        RebaseHelper(s,original,parents,pair[0],pair[1],count);
}
inline bool UpperBodyBone(int bone){
    // Root, camera, hips, both legs and the unknown final attachment remain
    // byte-identical to the engine pose. 3..59 is the spine/head/arm range in
    // the validated Hero rig; 72/73 are the two hand auxiliary bones.
    return (bone>=3&&bone<=59)||bone==72||bone==73;
}
inline bool RightArmBone(int bone){
    // The validated Hero right-arm chain is 35..59, including fingers and the
    // arm skin helpers. Bone 72 is the right-hand auxiliary attachment helper.
    return (bone>=35&&bone<=59)||bone==72;
}
inline bool LeftArmBone(int bone){
    return (bone>=9&&bone<=33)||bone==73;
}
inline bool TrackedArmBone(int bone){return LeftArmBone(bone)||RightArmBone(bone);}
inline bool RigDescendant(int bone,int root,const Parents& parents,int count){
    if(count<1||count>75||bone<0||bone>=count||root<0||root>=count)return false;
    for(int depth=0;depth<count;++depth){
        if(bone==root)return true;
        if(bone==0)return false;
        const int parent=parents[bone];
        if(parent<0||parent>=bone||parent>=count)return false;
        bone=parent;
    }
    return false;
}
inline V KeepOutsideSphere(V point,V center,float radius,V fallback){
    if(!Valid(point)||!Valid(center)||!Valid(fallback)||!std::isfinite(radius)||radius<=0)return point;
    const auto delta=point-center;const float distance=Length(delta);
    return distance>=radius?point:center+Unit(delta,Unit(fallback))*radius;
}
inline bool OrdinaryMovementState(unsigned char state){
    // Main-game ordinary movement uses 0/1/2. The school player controller uses
    // 29/30 for its normal controllable movement; the cinematic classifier also
    // confirms those states are not scripted. Camera mode, head lock and the
    // native-action handoff still independently protect authored animations.
    // State 9 is the opening helicopter and must remain native-owned.
    return state==0||state==1||state==2||state==29||state==30;
}
inline void RepairUpperSkinHelpers(Skeleton& s,const Skeleton& original,const Parents& parents,int count=75){
    for(const auto pair:std::array<std::array<int,2>,6>{{{10,12},{36,38},{33,9},{59,35},{72,39},{73,13}}})
        RebaseHelper(s,original,parents,pair[0],pair[1],count);
}
inline void RepairRightArmSkinHelpers(Skeleton& s,const Skeleton& original,const Parents& parents){
    for(const auto pair:std::array<std::array<int,2>,3>{{{36,38},{59,35},{72,39}}})
        RebaseHelper(s,original,parents,pair[0],pair[1]);
}
inline bool RightArm(Skeleton& s,const Parents& parents,V target,Q rotation,float extension=1){
    if(!Valid(target)||!Valid(rotation)||!std::isfinite(extension))return false;
    for(int i=0;i<75;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||!std::isfinite(s[i].scale)||
       s[i].scale<.5f||s[i].scale>2|| (i>0&&(parents[i]<0||parents[i]>=i)))return false;
    const auto original=s;
    if(!SolveLimb(s,parents,35,38,39,target,original[38].p,extension))return false;
    OrientArm(s,parents,38,39,rotation);
    RepairRightArmSkinHelpers(s,original,parents);
    // A raised physical camcorder is allowed to affect only its supporting
    // right arm. Native head, camera, torso, left arm, hips and legs are exact.
    for(int i=0;i<75;++i)if(!RightArmBone(i))s[i]=original[i];
    for(const auto& atom:s)if(!Valid(atom.q)||!Valid(atom.p))return false;
    return true;
}
inline bool TrackedArms(Skeleton& s,const Parents& parents,const Targets& t,int count=75){
    if(!Valid(t.left)||!Valid(t.right)||!Valid(t.leftRotation)||!Valid(t.rightRotation)||
       !std::isfinite(t.armExtension)||count<60||count>75)return false;
    for(int i=0;i<count;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||!std::isfinite(s[i].scale)||
       s[i].scale<.5f||s[i].scale>2|| (i>0&&(parents[i]<0||parents[i]>=i)))return false;
    const auto original=s;
    if(t.leftTracked){
        if(!SolveLimb(s,parents,9,12,13,t.left,original[12].p,t.armExtension,count))return false;
        OrientArm(s,parents,12,13,t.leftRotation,count);
    }
    if(t.rightTracked){
        if(!SolveLimb(s,parents,35,38,39,t.right,original[38].p,t.armExtension,count))return false;
        OrientArm(s,parents,38,39,t.rightRotation,count);
    }
    RepairUpperSkinHelpers(s,original,parents,count);
    // The engine pose remains authoritative everywhere except the two arm
    // chains. This keeps authored locomotion, body reactions and cinematics.
    for(int i=0;i<count;++i)if(!TrackedArmBone(i))s[i]=original[i];
    for(int i=0;i<count;++i)if(!Valid(s[i].q)||!Valid(s[i].p))return false;
    return true;
}
inline bool TrackedArmsMapped(Skeleton& s,const Parents& parents,const Targets& t,int count,
    int leftUpper,int leftLower,int leftHand,int rightUpper,int rightLower,int rightHand,
    const std::array<bool,75>& writable){
    if(!Valid(t.left)||!Valid(t.right)||!Valid(t.leftRotation)||!Valid(t.rightRotation)||
       !std::isfinite(t.armExtension)||count<16||count>75)return false;
    for(int index:{leftUpper,leftLower,leftHand,rightUpper,rightLower,rightHand})
        if(index<0||index>=count)return false;
    if(!RigDescendant(leftLower,leftUpper,parents,count)||
       !RigDescendant(leftHand,leftLower,parents,count)||
       !RigDescendant(rightLower,rightUpper,parents,count)||
       !RigDescendant(rightHand,rightLower,parents,count)||
       RigDescendant(rightUpper,leftUpper,parents,count)||
       RigDescendant(leftUpper,rightUpper,parents,count))return false;
    for(int i=0;i<count;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||!std::isfinite(s[i].scale)||
       s[i].scale<.5f||s[i].scale>2|| (i>0&&(parents[i]<0||parents[i]>=i)))return false;
    const auto original=s;
    if(t.leftTracked){
        if(!SolveLimb(s,parents,leftUpper,leftLower,leftHand,t.left,original[leftLower].p,
            t.armExtension,count))return false;
        OrientArm(s,parents,leftLower,leftHand,t.leftRotation,count);
    }
    if(t.rightTracked){
        if(!SolveLimb(s,parents,rightUpper,rightLower,rightHand,t.right,original[rightLower].p,
            t.armExtension,count))return false;
        OrientArm(s,parents,rightLower,rightHand,t.rightRotation,count);
    }
    // A dynamically discovered rig has no trusted auxiliary-helper table.
    // Restore every atom outside the two validated shoulder subtrees so IK can
    // never leak into the school torso, head, camera, hips, or lower body.
    for(int i=0;i<count;++i)if(!writable[i])s[i]=original[i];
    for(int i=0;i<count;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||s[i].scale!=original[i].scale)return false;
    return true;
}
inline Atom BlendAtom(const Atom& nativePose,const Atom& trackedPose,float alpha){
    alpha=std::clamp(alpha,0.0f,1.0f);
    Q target=trackedPose.q;
    const float dot=nativePose.q.x*target.x+nativePose.q.y*target.y+
        nativePose.q.z*target.z+nativePose.q.w*target.w;
    if(dot<0)target={-target.x,-target.y,-target.z,-target.w};
    Atom out;
    out.q=Normalize({nativePose.q.x+(target.x-nativePose.q.x)*alpha,
        nativePose.q.y+(target.y-nativePose.q.y)*alpha,
        nativePose.q.z+(target.z-nativePose.q.z)*alpha,
        nativePose.q.w+(target.w-nativePose.q.w)*alpha});
    out.p=nativePose.p+(trackedPose.p-nativePose.p)*alpha;
    out.scale=nativePose.scale;
    return out;
}
inline bool UpperBody(Skeleton& s,const Parents& parents,const Targets& t){
    if(!Valid(t.head)||!Valid(t.left)||!Valid(t.right)||!Valid(t.headRotation)||
       !Valid(t.leftRotation)||!Valid(t.rightRotation)||!std::isfinite(t.bodyYaw))return false;
    for(int i=0;i<75;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||!std::isfinite(s[i].scale)||
       s[i].scale<.5f||s[i].scale>2|| (i>0&&(parents[i]<0||parents[i]>=i)))return false;
    const auto original=s;

    // Let the shoulders follow the headset naturally, but cap the torso to a
    // comfortable 45-degree turn. The player turns farther with the game's
    // normal stick controls; the native pelvis/capsule never rotates here.
    const float torsoYaw=std::clamp(WrapYaw(t.bodyYaw)*.65f,-.7853982f,.7853982f);
    const Q yaw{0,0,std::sin(torsoYaw*.5f),std::cos(torsoYaw*.5f)};
    const V pivot=t.eyeAnchored?s[1].p:s[7].p;
    TransformTree(s,parents,3,yaw,pivot+Rotate(yaw,s[3].p-pivot)-s[3].p);
    const auto turned=s;

    // Use only a small, bounded portion of positional head movement to bend
    // the spine. This lets leaning feel connected without pulling the pelvis,
    // feet, collision capsule, or camera away from the game's animation.
    V lean=t.head-s[7].p;
    if(!Valid(lean)||Length(lean)>100.f)return false;
    lean={std::clamp(lean.x,-12.f,12.f),std::clamp(lean.y,-12.f,12.f),std::clamp(lean.z,-8.f,8.f)};
    const V desiredHead=s[7].p+lean*.35f;
    for(int bone:{3,4,5,6}){
        const auto from=s[7].p-s[bone].p;
        const auto to=desiredHead-s[bone].p;
        if(Length(from)>.01f&&Length(to)>.01f)TransformTree(s,parents,bone,Fraction(Between(from,to),.10f));
    }

    // Preserve the game's animated local head pose instead of rotating the
    // face/head mesh directly from the HMD. The camera remains engine-owned.
    const Q nativeHeadLocal=Normalize(Mul(Conjugate(original[6].q),original[7].q));
    s[7].q=Normalize(Mul(s[6].q,nativeHeadLocal));

    if(t.leftTracked){
        if(!SolveLimb(s,parents,9,12,13,t.left,turned[12].p, t.armExtension))return false;
        OrientArm(s,parents,12,13,t.leftRotation);
    }
    if(t.rightTracked){
        if(!SolveLimb(s,parents,35,38,39,t.right,turned[38].p,t.armExtension))return false;
        OrientArm(s,parents,38,39,t.rightRotation);
    }
    RepairUpperSkinHelpers(s,original,parents);

    // Enforce the hybrid boundary after every solver operation. Even an
    // unexpected parent chain cannot leak a transform into the lower body.
    for(int i=0;i<75;++i)if(!UpperBodyBone(i))s[i]=original[i];
    for(const auto& atom:s)if(!Valid(atom.q)||!Valid(atom.p))return false;
    return true;
}
inline bool Body(Skeleton& s,const Parents& parents,const Targets& t){
    if(!Valid(t.head)||!Valid(t.left)||!Valid(t.right)||!Valid(t.headRotation)||!Valid(t.leftRotation)||!Valid(t.rightRotation)||!std::isfinite(t.bodyYaw))return false;
    for(int i=0;i<75;++i)if(!Valid(s[i].q)||!Valid(s[i].p)||!std::isfinite(s[i].scale)||s[i].scale<.5f||s[i].scale>2|| (i>0&&(parents[i]<0||parents[i]>=i)))return false;
    const auto original=s;
    const Q yaw{0,0,std::sin(t.bodyYaw*.5f),std::cos(t.bodyYaw*.5f)};
    // P24 can anchor the rigid turn to the rendered eye rather than the neck.
    const V pivot=t.eyeAnchored?s[1].p:s[7].p;
    TransformTree(s,parents,2,yaw,pivot+Rotate(yaw,s[2].p-pivot)-s[2].p);
    const auto turned=s;
    V headDelta=t.head-s[7].p;if(Length(headDelta)>500||std::fabs(headDelta.z)>100)return false;
    const V room=t.roomscaleFollow?V{headDelta.x,headDelta.y,0}:V{};
    const V leftFoot=s[62].p+room,rightFoot=s[67].p+room;
    TransformTree(s,parents,2,{},room);
    V hipDelta=(headDelta-room)*.45f;hipDelta.z=std::clamp(hipDelta.z,-35.f,20.f);
    TransformTree(s,parents,2,{},hipDelta);
    for(int bone:{3,4,5,6}){
        Q aim=Between(s[7].p-s[bone].p,t.head-s[bone].p);
        TransformTree(s,parents,bone,Fraction(aim,.3f));
    }
    TransformTree(s,parents,7,Mul(t.headRotation,Conjugate(s[7].q)));
    if(!SolveLimb(s,parents,60,61,62,leftFoot,turned[61].p+room)||!SolveLimb(s,parents,65,66,67,rightFoot,turned[66].p+room))return false;
    TransformTree(s,parents,62,Mul(turned[62].q,Conjugate(s[62].q)));
    TransformTree(s,parents,67,Mul(turned[67].q,Conjugate(s[67].q)));
    if(!SolveLimb(s,parents,9,12,13,t.left,turned[12].p+room,t.armExtension)||!SolveLimb(s,parents,35,38,39,t.right,turned[38].p+room,t.armExtension))return false;
    OrientArm(s,parents,12,13,t.leftRotation);
    OrientArm(s,parents,38,39,t.rightRotation);
    RepairSkinHelpers(s,original,parents);
    // Camera/root remain engine-owned. Limb subtrees include twist/finger bones.
    s[0]=original[0];s[1]=original[1];
    for(const auto& atom:s)if(!Valid(atom.q)||!Valid(atom.p))return false;
    return true;
}
}
