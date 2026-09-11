#include "../src/dinput8_proxy.cpp"
#include <fstream>
#include <stdexcept>
static void Check(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
static void Near(float a,float b,const char* msg){Check(std::fabs(a-b)<.003f,msg);}
static void TestBody(p20::Skeleton s,const p20::Parents& parents){
    for(auto chain:{std::pair{12,13},std::pair{38,39}}){
        const auto axis=p20::Unit(s[chain.second].p-s[chain.first].p);
        for(float angle:{-3.13f,-1.57f,0.f,1.57f,3.13f}){
            auto rotated=s;
            const p20::Q roll{axis.x*std::sin(angle*.5f),axis.y*std::sin(angle*.5f),axis.z*std::sin(angle*.5f),std::cos(angle*.5f)};
            const auto target=p20::Mul(roll,s[chain.second].q);
            p20::OrientArm(rotated,parents,chain.first,chain.second,target);
            Near(p20::Length(rotated[chain.second].p-s[chain.second].p),0,"forearm roll preserves controller position");
            Near(p20::Length(rotated[chain.first].p-s[chain.first].p),0,"forearm roll preserves elbow position");
            for(auto v:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}}){
                Near(p20::Length(p20::Rotate(rotated[chain.first].q,v)-p20::Rotate(p20::Mul(roll,s[chain.first].q),v)),0,"controller roll carried through forearm");
                Near(p20::Length(p20::Rotate(rotated[chain.second].q,v)-p20::Rotate(target,v)),0,"hand target retained after forearm twist");
            }
            // Quaternion sign must not alter either limb pose.
            auto opposite=s;p20::OrientArm(opposite,parents,chain.first,chain.second,{-target.x,-target.y,-target.z,-target.w});
            Near(p20::Length(p20::Rotate(opposite[chain.first].q,{0,1,0})-p20::Rotate(rotated[chain.first].q,{0,1,0})),0,"quaternion sign invariant");
        }
        auto singular=s;const auto swing=p20::Between(axis,axis*-1);
        const auto target=p20::Mul(swing,s[chain.second].q);
        p20::OrientArm(singular,parents,chain.first,chain.second,target);
        Check(p20::Valid(singular[chain.first].q)&&p20::Valid(singular[chain.second].q),"180 degree wrist swing remains finite");
        Near(p20::Length(singular[chain.second].p-s[chain.second].p),0,"singular swing preserves wrist position");
    }
    auto original=s;p20::Targets t;
    t.head=s[7].p+p20::V{2,1,-4};t.left=s[13].p+p20::V{0,-5,4};t.right=s[39].p+p20::V{0,-5,4};
    t.headRotation=s[7].q;t.leftRotation=s[13].q;t.rightRotation=s[39].q;
    Check(p20::Body(s,parents,t),"body solver accepted valid rig");
    Check(!std::memcmp(s.data(),original.data(),64),"camera/root unchanged byte-for-byte");
    for(auto pair:{std::pair{10,12},std::pair{11,12},std::pair{36,38},std::pair{37,38},std::pair{33,9},std::pair{59,35},std::pair{64,61},std::pair{69,66},std::pair{70,60},std::pair{71,65},std::pair{72,39},std::pair{73,13}}){
        // 11/37 are descendants of the fore-twist chain in the actual Hero rig.
        if((pair.first==11&&parents[11]!=10)||(pair.first==37&&parents[37]!=36))continue;
        auto delta=p20::Mul(s[pair.second].q,p20::Conjugate(original[pair.second].q));
        auto expected=s[pair.second].p+p20::Rotate(delta,original[pair.first].p-original[pair.second].p);
        Near(p20::Length(s[pair.first].p-expected),0,"sibling helper follows driver position");
        p20::V point=original[pair.second].p+p20::V{2,3,4};
        auto helperDelta=p20::Mul(s[pair.first].q,p20::Conjugate(original[pair.first].q));
        auto viaHelper=s[pair.first].p+p20::Rotate(helperDelta,point-original[pair.first].p);
        auto viaDriver=s[pair.second].p+p20::Rotate(delta,point-original[pair.second].p);
        Near(p20::Length(viaHelper-viaDriver),0,"skin helper and driver deformation agree");
    }
    for(auto edge:{std::pair{9,12},std::pair{12,13},std::pair{35,38},std::pair{38,39},std::pair{60,61},std::pair{61,62},std::pair{65,66},std::pair{66,67}})
        Near(p20::Length(s[edge.first].p-s[edge.second].p),p20::Length(original[edge.first].p-original[edge.second].p),"limb lengths preserved");
    t.head.x=NAN;Check(!p20::Body(s,parents,t),"invalid target rejected");
    s=original;const p20::V room{200,-75,0};
    t.head=s[7].p+room;t.left=s[13].p+room;t.right=s[39].p+room;
    t.headRotation=s[7].q;t.leftRotation=s[13].q;t.rightRotation=s[39].q;t.roomscaleFollow=true;
    Check(p20::Body(s,parents,t),"roomscale follow beyond old one-metre cutoff");
    Near(p20::Length(s[2].p-(original[2].p+room)),0,"pelvis follows full horizontal displacement");
    Near(p20::Length(s[62].p-(original[62].p+room)),0,"left foot target moves with roomscale body");
    Near(p20::Length(s[67].p-(original[67].p+room)),0,"right foot target moves with roomscale body");
    Check(!std::memcmp(s.data(),original.data(),64),"roomscale does not double-move camera bone");
    for(float angle:{-3.0f,-1.5707963f,0.f,1.5707963f,3.f}){
        s=original;const p20::Q yaw{0,0,std::sin(angle*.5f),std::cos(angle*.5f)};
        const auto pivot=original[7].p;
        auto turn=[&](p20::V p){return pivot+p20::Rotate(yaw,p-pivot);};
        t.head=pivot;t.left=turn(original[13].p);t.right=turn(original[39].p);t.bodyYaw=angle;
        t.headRotation=p20::Mul(yaw,original[7].q);t.leftRotation=original[13].q;t.rightRotation=original[39].q;
        Check(p20::Body(s,parents,t),"body yaw valid");
        Near(p20::Length(s[2].p-turn(original[2].p)),0,"hips turn around tracked head");
        Near(p20::Length(s[9].p-turn(original[9].p)),0,"left shoulder turns with body");
        Near(p20::Length(s[35].p-turn(original[35].p)),0,"right shoulder turns with body");
        Near(std::fabs(p20::Dot(p20::Rotate(s[13].q,{1,0,0}),p20::Rotate(t.leftRotation,{1,0,0}))),1,"left hand target rotation retained despite body yaw");
        Check(!std::memcmp(s.data(),original.data(),64),"yaw leaves root and camera byte-identical");
    }
    // Deliberately separate eye and anatomical head so a neck-pivot regression
    // cannot pass on an artificial rig with coincident horizontal pivots.
    auto anchored=original;anchored[1].p=anchored[1].p+p20::V{9,7,3};
    for(float angle:{-3.13f,-1.5707963f,0.f,1.5707963f,3.13f}){
        s=anchored;const p20::Q yaw{0,0,std::sin(angle*.5f),std::cos(angle*.5f)};
        const p20::V shift{32,-17,0};const auto eye=anchored[1].p;
        auto turn=[&](p20::V p){return eye+p20::Rotate(yaw,p-eye)+shift;};
        t.eyeAnchored=true;t.bodyYaw=angle;t.head=turn(anchored[7].p);
        t.left=turn(anchored[13].p);t.right=turn(anchored[39].p);
        t.headRotation=p20::Mul(yaw,anchored[7].q);t.leftRotation=anchored[13].q;t.rightRotation=anchored[39].q;
        Check(p20::Body(s,parents,t),"eye-anchored body solve");
        for(int bone:{2,7,9,35}){
            Near(p20::Length(s[bone].p-turn(anchored[bone].p)),0,"body matches current yaw about eye without neck-pivot drift");
            Near(p20::Length(s[bone].p-(eye+shift)),p20::Length(anchored[bone].p-eye),"eye-to-shoulder/head clearance preserved during yaw");
        }
        for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})
            Near(p20::Length(p20::Rotate(s[39].q,axis)-p20::Rotate(t.rightRotation,axis)),0,"right wrist orientation unchanged by eye anchoring");
        for(auto edge:{std::pair{9,12},std::pair{12,13},std::pair{35,38},std::pair{38,39}})
            Near(p20::Length(s[edge.first].p-s[edge.second].p),p20::Length(anchored[edge.first].p-anchored[edge.second].p),"eye anchoring preserves arm lengths");
        Check(!std::memcmp(s.data(),anchored.data(),64),"eye anchor never writes root/camera bones");
    }
}
static void TestHybridUpperBody(p20::Skeleton s,const p20::Parents& parents){
    const auto original=s;p20::Targets t;
    t.head=s[7].p+p20::V{8,-6,4};
    t.left=s[13].p+p20::V{-4,-8,6};t.right=s[39].p+p20::V{4,-8,6};
    t.headRotation=p20::Normalize({0,.3f,0,.954f});
    t.leftRotation=s[13].q;t.rightRotation=s[39].q;
    t.bodyYaw=2.4f;t.eyeAnchored=true;t.armExtension=1.05f;
    Check(p20::UpperBody(s,parents,t),"hybrid upper-body solver accepted valid rig");

    for(int i=0;i<75;++i)
        Near(s[i].scale,original[i].scale,"hybrid upper-body solver preserves every bone scale");

    for(int i=0;i<75;++i)if(!p20::UpperBodyBone(i))
        Check(!std::memcmp(&s[i],&original[i],sizeof(p20::Atom)),"hybrid boundary preserves every non-upper-body atom");
    Check(std::memcmp(&s[3],&original[3],sizeof(p20::Atom))!=0,"bounded torso follow applied");
    Check(std::memcmp(&s[13],&original[13],sizeof(p20::Atom))!=0&&
          std::memcmp(&s[39],&original[39],sizeof(p20::Atom))!=0,"both tracked arms applied");

    const auto beforeLocal=p20::Normalize(p20::Mul(p20::Conjugate(original[6].q),original[7].q));
    const auto afterLocal=p20::Normalize(p20::Mul(p20::Conjugate(s[6].q),s[7].q));
    for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})
        Near(p20::Length(p20::Rotate(beforeLocal,axis)-p20::Rotate(afterLocal,axis)),0,"native local head animation retained");

    Check(p20::OrdinaryMovementState(0)&&p20::OrdinaryMovementState(1)&&
          p20::OrdinaryMovementState(2),"confirmed main-game movement states accepted");
    Check(p20::OrdinaryMovementState(29)&&p20::OrdinaryMovementState(30),
        "confirmed school ordinary movement states keep tracked upper-body IK");
    for(unsigned char state:{3,4,5,6,7,8,9,10,16,17,255})
        Check(!p20::OrdinaryMovementState(state),"unknown/scripted state rejected");

    auto oneHand=original;t.leftTracked=false;t.rightTracked=true;
    Check(p20::UpperBody(oneHand,parents,t),"single tracked controller remains safe");
    Check(std::memcmp(&oneHand[39],&original[39],sizeof(p20::Atom))!=0,"tracked right arm remains active");
    for(int i=0;i<75;++i)if(!p20::UpperBodyBone(i))
        Check(!std::memcmp(&oneHand[i],&original[i],sizeof(p20::Atom)),"single-hand solve cannot reach lower body");
}
static void TestPhysicalCamcorderArm(p20::Skeleton s,const p20::Parents& parents){
    const auto original=s;
    const auto target=s[39].p+p20::V{-6,2,2};
    const auto rotation=p20::Normalize({.08f,.18f,-.12f,.972f});
    Check(p20::RightArm(s,parents,target,rotation,1.05f),"physical camcorder right-arm solve");
    Near(p20::Length(s[39].p-target),0,"physical camcorder wrist follows tracked controller");
    for(int i=0;i<75;++i)
        Near(s[i].scale,original[i].scale,"physical camcorder solver preserves every bone scale");
    for(int i=0;i<75;++i)if(!p20::RightArmBone(i))
        Check(!std::memcmp(&s[i],&original[i],sizeof(p20::Atom)),"physical camcorder cannot change non-right-arm bones");
    Check(std::memcmp(&s[35],&original[35],sizeof(p20::Atom))!=0&&
          std::memcmp(&s[39],&original[39],sizeof(p20::Atom))!=0,
          "physical camcorder changes shoulder and wrist");
    for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})
        Near(p20::Length(p20::Rotate(s[39].q,axis)-p20::Rotate(rotation,axis)),0,
            "physical camcorder wrist orientation follows tracked controller");
    auto invalid=original;
    Check(!p20::RightArm(invalid,parents,{NAN,0,0},rotation,1.05f),"invalid camcorder target rejected");
    Check(!p20::RightArm(invalid,parents,target,rotation,1.5f),"unsafe camcorder arm stretch rejected");
}
static void TestTrackedArms(p20::Skeleton s,const p20::Parents& parents){
    const auto original=s;p20::Targets t;
    t.left=s[13].p+p20::V{-8,-10,7};t.right=s[39].p+p20::V{8,-10,7};
    t.leftRotation=s[13].q;t.rightRotation=s[39].q;t.armExtension=1.05f;
    Check(p20::TrackedArms(s,parents,t),"tracked-arm solver accepted valid targets");
    Check(std::memcmp(&s[13],&original[13],sizeof(p20::Atom))!=0&&
          std::memcmp(&s[39],&original[39],sizeof(p20::Atom))!=0,
          "both wrists follow tracked targets");
    for(int i=0;i<75;++i)if(!p20::TrackedArmBone(i))
        Check(!std::memcmp(&s[i],&original[i],sizeof(p20::Atom)),
            "tracked arms preserve torso, head, camera and lower body byte-for-byte");

    auto oneHand=original;t.leftTracked=false;t.rightTracked=true;
    Check(p20::TrackedArms(oneHand,parents,t),"single tracked arm remains valid");
    for(int i=0;i<75;++i)if(p20::LeftArmBone(i))
        Check(!std::memcmp(&oneHand[i],&original[i],sizeof(p20::Atom)),
            "untracked left arm retains native animation");

    const p20::V eye{10,20,30},inside{12,20,30},fallback{0,1,0};
    const auto protectedPoint=p20::KeepOutsideSphere(inside,eye,32,fallback);
    Near(p20::Length(protectedPoint-eye),32,"near-eye wrist is clamped to safety radius");
    const p20::V outside{50,20,30};
    Near(p20::Length(p20::KeepOutsideSphere(outside,eye,32,fallback)-outside),0,
        "normal tracked wrist remains unchanged outside safety radius");
    const auto centered=p20::KeepOutsideSphere(eye,eye,32,fallback);
    Near(p20::Length(centered-eye),32,"coincident eye/wrist uses stable fallback direction");

    // The real school mesh is a distinct 67-bone layout, not a truncation of
    // the normal 75-bone Hero. Exercise an intentionally reordered skeleton so
    // a regression to hard-coded Hero indices cannot pass this test.
    p20::Skeleton school{};p20::Parents schoolParents{};
    auto schoolBone=[&](int i,int parent,p20::V position){schoolParents[i]=parent;school[i].p=position;};
    schoolBone(5,2,{0,0,135});
    schoolBone(8,5,{-20,0,135});schoolBone(11,8,{-40,-5,130});
    schoolBone(12,11,{-60,-10,125});schoolBone(13,12,{-65,-11,124});
    schoolBone(28,5,{20,0,135});schoolBone(31,28,{40,-5,130});
    schoolBone(32,31,{60,-10,125});schoolBone(33,32,{65,-11,124});
    std::array<bool,75> schoolWritable{};
    for(int i=0;i<67;++i)schoolWritable[i]=
        p20::RigDescendant(i,8,schoolParents,67)||p20::RigDescendant(i,28,schoolParents,67);
    const auto schoolOriginal=school;p20::Targets schoolTargets;
    schoolTargets.left=school[12].p+p20::V{-8,-10,7};
    schoolTargets.right=school[32].p+p20::V{8,-10,7};
    schoolTargets.leftRotation=school[12].q;schoolTargets.rightRotation=school[32].q;
    schoolTargets.armExtension=1.05f;
    Check(p20::TrackedArmsMapped(school,schoolParents,schoolTargets,67,8,11,12,28,31,32,schoolWritable),
        "reordered 67-bone school rig accepts dynamically mapped arms");
    Check(std::memcmp(&school[12],&schoolOriginal[12],sizeof(p20::Atom))!=0&&
          std::memcmp(&school[32],&schoolOriginal[32],sizeof(p20::Atom))!=0,
        "dynamically mapped school wrists follow tracked targets");
    for(int i=0;i<67;++i)if(!schoolWritable[i])
        Check(!std::memcmp(&school[i],&schoolOriginal[i],sizeof(p20::Atom)),
            "mapped school solve preserves every non-arm atom");
    Check(P20FindMappedBone({"schoolroot","schoolcamera","schoolhips","schoolhead",
        "schoollupperarm","schoollforearm","schoollhand","schoollhandindex0"},{"lhand","lefthand"})==6,
        "school name mapper selects wrist rather than a finger");
    Check(P20FindMappedBone({"schoolrupperarm","schoolrforearm","schoolrhand"},{"rupperarm","rightupperarm"})==0,
        "school name mapper accepts a non-Hero prefix");

    const auto native=original[13];auto tracked=native;tracked.p.x+=20;
    auto start=p20::BlendAtom(native,tracked,0.0f);
    auto middle=p20::BlendAtom(native,tracked,0.5f);
    auto end=p20::BlendAtom(native,tracked,1.0f);
    Near(start.p.x,native.p.x,"animation handoff starts at native pose");
    Near(middle.p.x,native.p.x+10,"animation handoff blends without snap");
    Near(end.p.x,tracked.p.x,"animation handoff reaches tracked pose");
}
int main(int argc,char** argv)try{
    const auto grip=p20::Normalize({.2f,.1f,-.3f,.8f});
    const auto basis=p20::UprightHand({0,1,0});const auto desired=p20::Normalize({.1f,.3f,.2f,.9f});
    const auto offset=p20::Mul(p20::Conjugate(p20::GripCanonical(grip)),p20::Mul(p20::Conjugate(basis),desired));
    auto initial=p20::GripTarget(grip,basis,offset);
    for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})Near(p20::Length(p20::Rotate(initial,axis)-p20::Rotate(desired,axis)),0,"canonical calibration preserves initial left pose");
    for(float angle:{-3.f,-1.f,0.f,1.f,3.f}){
        const p20::Q recenter{0,std::sin(angle*.5f),0,std::cos(angle*.5f)};
        const auto nextGrip=p20::Mul(p20::Conjugate(recenter),grip);
        const auto next=p20::GripTarget(nextGrip,basis,offset);
        const auto delta=p20::Mul(p20::Mul(basis,p20::GripCanonical(p20::Conjugate(recenter))),p20::Conjugate(basis));
        for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})
            Near(p20::Length(p20::Rotate(next,axis)-p20::Rotate(delta,p20::Rotate(initial,axis))),0,"recenter changes only coordinate frame, not wrist offset");
    }
    for(float angle:{-3.f,-1.5f,0.f,1.5f,3.f}){
        const p20::V forward{std::cos(angle),std::sin(angle),0};const auto hand=p20::UprightHand(forward);
        Near(p20::Length(p20::Rotate(hand,{1,0,0})-forward),0,"right neutral fingers point forward");
        Near(p20::Length(p20::Rotate(hand,{0,0,1})-p20::V{0,0,1}),0,"right neutral thumb points up");
    }
    Near(p20::Length(p22::Request({.05f,0,0},{},.02f,100)),0,"roomscale lean deadzone");
    Near(p20::Length(p22::Request({0,2,0},{},.02f,100)),0,"crouch never drives horizontal movement");
    Near(p20::Length(p22::Request({2,0,0},{},.02f,100)),0,"tracking jump rejected");
    Near(p20::Length(p22::Request({.5f,0,0},{},1,100)),0,"stale tick rejected");
    auto step=p22::Request({.5f,0,0},{},.02f,100);Near(step.x,.04f,"movement speed bounded");
    const p20::V right{0,1,0},forward{1,0,0},local{.2f,0,-.3f};
    Near(p20::Length(p22::ToLocal(p22::ToWorld(local,right,forward,100),right,forward,100)-local),0,"world/local consumed movement roundtrip");
    // Only the accepted collision displacement is removed from camera/hand offsets.
    const auto partial=p22::ToLocal({10,0,0},right,forward,100);
    Near(partial.z,-.1f,"partial sweep consumption");
    Near((local-partial).z,-.2f,"blocked remainder stays a tracked lean");
    for(int i=0;i<500;++i){p20::Limb result;float angle=i*.123f; p20::V target{std::cos(angle)*i*.3f,std::sin(angle)*i*.3f,i*.05f};
        Check(p20::TwoBone({},target,{0,0,1},28.33f,29.82f,result),"limb solve");
        Near(p20::Length(result.joint),28.33f,"upper length");Near(p20::Length(result.end-result.joint),29.82f,"lower length");}
    p20::Limb limb;Check(!p20::TwoBone({},{NAN,0,0},{},1,1,limb),"nan rejected");
    Check(!p20::TwoBone({},{},{},0,1,limb),"zero bone rejected");
    auto q=p20::Between({1,0,0},{-1,0,0});Near(p20::Rotate(q,{1,0,0}).x,-1,"opposite-vector rotation");
    p20::Skeleton s;p20::Parents parents{};
    auto bone=[&](int i,int parent,p20::V p){parents[i]=parent;s[i].p=p;};
    bone(1,0,{0,0,170});bone(2,0,{0,0,90});bone(3,2,{0,0,105});bone(4,3,{0,0,120});bone(5,4,{0,0,135});bone(6,5,{0,0,150});bone(7,6,{0,0,165});
    bone(9,5,{-20,0,135});bone(12,9,{-40,-5,130});bone(13,12,{-60,-10,125});
    bone(35,5,{20,0,135});bone(38,35,{40,-5,130});bone(39,38,{60,-10,125});
    bone(60,2,{-10,0,90});bone(61,60,{-10,-5,50});bone(62,61,{-10,0,10});
    bone(65,2,{10,0,90});bone(66,65,{10,-5,50});bone(67,66,{10,0,10});
    TestBody(s,parents);
    TestHybridUpperBody(s,parents);
    TestTrackedArms(s,parents);
    TestPhysicalCamcorderArm(s,parents);
    // Exercise exact production restore: preserve engine edits since our last write.
    std::vector<unsigned char> mesh(0x400);void* atoms=s.data();int count=75;
    std::memcpy(mesh.data()+0x318,&atoms,8);std::memcpy(mesh.data()+0x320,&count,4);
    g_p20Mesh=mesh.data();g_p20RigCount=75;g_p20Atoms=atoms;g_p20Before=s;s[13].p.x+=3;s[14].p.y+=4;
    s[60].p.x+=9;const auto untouchedLower=s[60].p.x;g_p20After=s;g_p20HaveBackup=true;
    s[14].p.y+=10;const auto engineEdit=s[14].p.y;P20Restore(mesh.data());
    Near(s[13].p.x,g_p20Before[13].p.x,"restored our unmodified arm atom");Near(s[14].p.y,engineEdit,"preserved engine arm atom change");
    Near(s[60].p.x,untouchedLower,"restore path never writes a lower-body atom");Check(!g_p20HaveBackup,"restore consumed");
    // A school-to-world transition may leave the retired school component in
    // Pawn+44C while the visible Hero component moves to another pawn field.
    // Confirm the recovery scan accepts only a pawn-owned skeletal component,
    // and remains dormant while the current rig is still applying normally.
    std::vector<unsigned char> pawnMemory(0x2100),alternateComponent(0x400),alternateRig(0x100),alternateBones(75*80);
    void* primaryComponent=reinterpret_cast<void*>(uintptr_t{0x12345000});
    void* alternatePointer=alternateComponent.data();void* alternateRigPointer=alternateRig.data();void* alternateBonePointer=alternateBones.data();
    std::memcpy(pawnMemory.data()+0x44c,&primaryComponent,sizeof(primaryComponent));
    std::memcpy(pawnMemory.data()+0x500,&alternatePointer,sizeof(alternatePointer));
    std::memcpy(alternateComponent.data()+0x278,&alternateRigPointer,sizeof(alternateRigPointer));
    std::memcpy(alternateRig.data()+0xcc,&alternateBonePointer,sizeof(alternateBonePointer));
    int alternateCount=75;std::memcpy(alternateRig.data()+0xd4,&alternateCount,4);std::memcpy(alternateRig.data()+0xd8,&alternateCount,4);
    g_p20Pawn.store(pawnMemory.data());g_p20PawnTick.store(GetTickCount64());g_p20RigPawn=pawnMemory.data();
    g_p20Mesh=mesh.data();g_p20LastAppliedTick.store(0);
    Check(P20IsPlayerMesh(alternateComponent.data()),"stale school slot recovers pawn-owned normal Hero component");
    g_p20LastAppliedTick.store(GetTickCount64());
    Check(!P20IsPlayerMesh(alternateComponent.data()),"alternate scan stays dormant while verified rig is applying");
    if(argc>1){
        std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});Check(bytes.size()==75*80,"captured ref size");
        for(int i=0;i<75;++i){p20::Q local; p20::V pos;std::memcpy(&local,bytes.data()+i*80+16,16);std::memcpy(&pos,bytes.data()+i*80+32,12);std::memcpy(&parents[i],bytes.data()+i*80+64,4);
            Check(i==0||(parents[i]>=0&&parents[i]<i),"captured parent chain");
            s[i].q=i?p20::Normalize(p20::Mul(s[parents[i]].q,local)):p20::Normalize(local);
            s[i].p=i?s[parents[i]].p+p20::Rotate(s[parents[i]].q,pos):pos;s[i].scale=1;}
        TestBody(s,parents);puts("PASS captured 75-bone Hero rig IK invariants.");
    }
    puts("PASS MC2 hybrid upper-body boundary, state gating, solver invariants, camera/lower-body preservation, and native restore semantics.");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
