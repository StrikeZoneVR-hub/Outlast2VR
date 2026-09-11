#include "p28_right_hand.h"
struct P20Tracked {p20::V position[3];p20::Q rotation[3];XrPosef center{};float units=100;ULONGLONG tick=0;uint64_t roomGeneration=0;bool handValid[2]{};};
std::mutex g_p20PoseMutex;
P20Tracked g_p20Tracked;
std::atomic<void*> g_p20Pawn{nullptr};
std::atomic<ULONGLONG> g_p20PawnTick{0};
std::atomic<bool> g_p20Enabled{true};
std::atomic<bool> g_p20PhysicalCamcorder{false};
std::atomic<bool> g_p21RoomscaleFollow{true};
std::atomic<bool> g_p22BodyYaw{true},g_p22RightNeutral{true};
using P20EvalFn=void(__fastcall*)(void*,float,int);
using P20PostFn=void(__fastcall*)(void*,float);
P20EvalFn g_p20Eval=nullptr;P20PostFn g_p20Post=nullptr;
void* g_p20Mesh=nullptr;void* g_p20RefMesh=nullptr;void* g_p20Atoms=nullptr;
int g_p20RigCount=0;
p20::Parents g_p20Parents{};
struct P20RigMap {
    int camera=-1,hips=-1,head=-1,neck=-1;
    int leftUpper=-1,leftLower=-1,leftHand=-1;
    int rightUpper=-1,rightLower=-1,rightHand=-1;
    std::array<bool,75> tracked{};
    bool valid=false,dynamicSchool=false;
};
P20RigMap g_p20Rig;
void* g_p20RigPawn=nullptr;
std::atomic<ULONGLONG> g_p20LastAppliedTick{0};
p20::Skeleton g_p20Before{},g_p20After{};
bool g_p20HaveBackup=false,g_p20Calibrated=false;
bool g_p20NativeOwned=true;
ULONGLONG g_p20BlendStarted=0;
bool g_p23HandCalibrated[2]{};
p20::Q g_p20HandOffset[2]{};XrPosef g_p20Center{};
uint64_t g_p20Applied=0,g_p20Skipped=0;
std::array<uint64_t,9> g_p20Rejects{};
void P20Reject(size_t reason,const char* name,long long a=0,long long b=0){
    if(reason>=g_p20Rejects.size())return;
    const auto count=++g_p20Rejects[reason];
    if(count<=3||count%600==0)Log("P41 IK SAFETY: %s count=%llu a=%lld b=%lld",name,
        static_cast<unsigned long long>(count),a,b);
}
void P20Observe(void* controller){
    void* pawn=nullptr;
    if(P19Read(controller,0xc38,pawn)){g_p20Pawn.store(pawn);g_p20PawnTick.store(GetTickCount64());}
}
bool P20Writable(const void* p,size_t size){
    if(!P15Readable(p,size))return false;MEMORY_BASIC_INFORMATION m{};VirtualQuery(p,&m,sizeof(m));
    return (m.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))!=0;
}
bool P20IsPlayerMesh(void* mesh){
    void* candidate=nullptr;const auto pawn=g_p20Pawn.load();const auto now=GetTickCount64();
    if(now-g_p20PawnTick.load()>=250||!P19Read(pawn,0x44c,candidate))return false;
    if(candidate==mesh||(mesh==g_p20Mesh&&pawn==g_p20RigPawn))return true;
    // During a school/real-world transition OLPlayerPawn can retain the retired
    // school component at +44C while its visible Hero component is moved to a
    // different direct pawn field. Once the last verified body has stopped
    // receiving IK, admit another pawn-owned 67/75-bone component for full rig
    // validation below. This never admits an arbitrary world/NPC mesh.
    const auto last=g_p20LastAppliedTick.load(std::memory_order_acquire);
    if(last&&now>=last&&now-last<500)return false;
    P19Skeleton rig;
    if(!P19FindSkeleton(mesh,rig)||(rig.count!=67&&rig.count!=75))return false;
    for(size_t offset=0;offset<0x2000;offset+=4){
        void* owned=nullptr;
        if(!P19Read(pawn,offset,owned))break;
        if(owned==mesh)return true;
    }
    return false;
}
std::string P20NormalizedBoneName(std::string name){
    std::string normalized;normalized.reserve(name.size());
    for(const unsigned char c:name)if(std::isalnum(c))normalized.push_back(static_cast<char>(std::tolower(c)));
    return normalized;
}
bool P20EndsWith(const std::string& value,const char* suffix){
    const size_t length=std::strlen(suffix);
    return value.size()>=length&&!value.compare(value.size()-length,length,suffix);
}
int P20FindMappedBone(const std::vector<std::string>& names,std::initializer_list<const char*> suffixes){
    int best=-1,bestScore=-1;
    bool tied=false;
    for(size_t i=0;i<names.size();++i){
        for(const char* suffix:suffixes)if(P20EndsWith(names[i],suffix)){
            // Prefer the shortest matching name. This selects Hero_L_Hand over
            // auxiliary/finger names while still accepting a school prefix.
            const int score=1000-static_cast<int>(names[i].size()-std::strlen(suffix));
            if(score>bestScore){best=static_cast<int>(i);bestScore=score;tied=false;}
            else if(score==bestScore&&best!=static_cast<int>(i))tied=true;
            break;
        }
    }
    return tied?-1:best;
}
bool P20TrackedArmBone(int bone){
    return bone>=0&&bone<75&&(g_p20Rig.valid?g_p20Rig.tracked[bone]:p20::TrackedArmBone(bone));
}
int P20LeftHandBone(){return g_p20Rig.valid?g_p20Rig.leftHand:13;}
bool P20ValidateRig(void* mesh){
    if(!P20IsPlayerMesh(mesh))return false;
    P19Skeleton sk;if(!P19FindSkeleton(mesh,sk)||(sk.count!=67&&sk.count!=75))return false;
    g_p20Parents.fill(0);
    std::vector<std::string> names(static_cast<size_t>(sk.count));
    for(int i=0;i<sk.count;++i){
        int parent=0;
        if(!P19Read(sk.bones,i*80+64,parent)||(i>0&&(parent<0||parent>=i)))return false;
        g_p20Parents[i]=parent;
        names[i]=P20NormalizedBoneName(P19BoneName(static_cast<const unsigned char*>(sk.bones)+i*80));
    }
    P20RigMap map;
    if(sk.count==75){
        const std::pair<int,const char*> coreNames[]={{0,"Hero_Root"},{1,"Hero_Camera"},{2,"Hero_Hips"},{7,"Hero_Head"},{9,"Hero_L_UpperArm"},{12,"Hero_L_Forearm"},{13,"Hero_L_Hand"},{35,"Hero_R_UpperArm"},{38,"Hero_R_Forearm"},{39,"Hero_R_Hand"}};
        for(const auto& [index,name]:coreNames)
            if(P19BoneName(static_cast<const unsigned char*>(sk.bones)+index*80)!=name)return false;
        const std::pair<int,const char*> strictNames[]={{10,"Hero_L_ForeTwist"},{11,"Hero_L_ForeTwist1"},{33,"Hero_L_UpArmTwist"},{36,"Hero_R_ForeTwist"},{37,"Hero_R_ForeTwist1"},{59,"Hero_R_UpArmTwist"},{72,"Hero_R_Hand_aux"},{73,"Hero_L_Hand_aux"},{40,"Hero_R_Hand_Index0"},{44,"Hero_R_Hand_Middle0"},{52,"Hero_R_Hand_Pinky0"},{56,"Hero_R_Hand_Thumb0"}};
        for(const auto& [index,name]:strictNames)
            if(P19BoneName(static_cast<const unsigned char*>(sk.bones)+index*80)!=name)return false;
        p20::V indexLocal{},pinkyLocal{},thumbLocal{};
        if(!P19Read(sk.bones,40*80+32,indexLocal)||!P19Read(sk.bones,52*80+32,pinkyLocal)||!P19Read(sk.bones,56*80+32,thumbLocal)||
            indexLocal.x<=0||pinkyLocal.x<=0||indexLocal.z<=pinkyLocal.z||thumbLocal.z<=0)return false;
        map.camera=1;map.hips=2;map.head=7;map.neck=6;
        map.leftUpper=9;map.leftLower=12;map.leftHand=13;
        map.rightUpper=35;map.rightLower=38;map.rightHand=39;
        for(int i=0;i<sk.count;++i)map.tracked[i]=p20::TrackedArmBone(i);
    }else{
        // The school mesh has 67 bones but is not index-compatible with Hero.
        // Resolve semantic joints by normalized suffix, then validate the real
        // hierarchy before allowing a single transform write.
        map.camera=P20FindMappedBone(names,{"camera"});
        map.hips=P20FindMappedBone(names,{"hips","pelvis"});
        map.head=P20FindMappedBone(names,{"head"});
        map.leftUpper=P20FindMappedBone(names,{"lupperarm","leftupperarm"});
        map.leftLower=P20FindMappedBone(names,{"lforearm","leftforearm","llowerarm","leftlowerarm"});
        map.leftHand=P20FindMappedBone(names,{"lhand","lefthand"});
        map.rightUpper=P20FindMappedBone(names,{"rupperarm","rightupperarm"});
        map.rightLower=P20FindMappedBone(names,{"rforearm","rightforearm","rlowerarm","rightlowerarm"});
        map.rightHand=P20FindMappedBone(names,{"rhand","righthand"});
        if(map.camera<0||map.hips<0||map.head<0||map.leftUpper<0||map.leftLower<0||map.leftHand<0||
           map.rightUpper<0||map.rightLower<0||map.rightHand<0)return false;
        map.neck=g_p20Parents[map.head];
        if(map.neck<=0||map.neck>=sk.count)return false;
    }
    if(!p20::RigDescendant(map.leftLower,map.leftUpper,g_p20Parents,sk.count)||
       !p20::RigDescendant(map.leftHand,map.leftLower,g_p20Parents,sk.count)||
       !p20::RigDescendant(map.rightLower,map.rightUpper,g_p20Parents,sk.count)||
       !p20::RigDescendant(map.rightHand,map.rightLower,g_p20Parents,sk.count)||
       p20::RigDescendant(map.rightUpper,map.leftUpper,g_p20Parents,sk.count)||
       p20::RigDescendant(map.leftUpper,map.rightUpper,g_p20Parents,sk.count))return false;
    int leftArmBones=0,rightArmBones=0;
    for(int i=0;i<sk.count;++i){
        const bool left=p20::RigDescendant(i,map.leftUpper,g_p20Parents,sk.count);
        const bool right=p20::RigDescendant(i,map.rightUpper,g_p20Parents,sk.count);
        leftArmBones+=left?1:0;rightArmBones+=right?1:0;
        if(sk.count==67)map.tracked[i]=left||right;
    }
    // Both chains must contain at least shoulder, forearm and hand. The upper
    // bound rejects a corrupt hierarchy that would accidentally include torso
    // or lower-body bones in the writable arm set.
    if(leftArmBones<3||rightArmBones<3||leftArmBones>30||rightArmBones>30)return false;
    map.valid=true;map.dynamicSchool=sk.count==67;g_p20Rig=map;g_p20RigPawn=g_p20Pawn.load();
    g_p20Mesh=mesh;g_p20RefMesh=sk.mesh;g_p20RigCount=sk.count;g_p20Calibrated=false;
    g_p20NativeOwned=true;g_p20BlendStarted=0;
    g_p23HandCalibrated[0]=g_p23HandCalibrated[1]=false;
    void* primary=nullptr;P19Read(g_p20RigPawn,0x44c,primary);
    Log("PF9 RIG VERIFIED: bones=%d school=%d alternatePawnComponent=%d camera=%d hips=%d head=%d neck=%d left=%d/%d/%d right=%d/%d/%d writable=%d/%d; all non-arm bones remain native.",
        sk.count,map.dynamicSchool?1:0,primary!=mesh?1:0,map.camera,map.hips,map.head,map.neck,map.leftUpper,map.leftLower,map.leftHand,
        map.rightUpper,map.rightLower,map.rightHand,leftArmBones,rightArmBones);return true;
}
void P20Restore(void* mesh){
    if(!g_p20HaveBackup||mesh!=g_p20Mesh)return;
    void* atoms=nullptr;int count=0;
    if(P19Read(mesh,0x318,atoms)&&P19Read(mesh,0x320,count)&&count==g_p20RigCount&&count>=60&&count<=75&&
       atoms==g_p20Atoms&&P20Writable(atoms,size_t(count)*sizeof(p20::Atom))){
        // Restore only atoms still equal to our output. Never undo an engine update.
        for(int i=0;i<count;++i)if(P20TrackedArmBone(i)){auto* p=static_cast<unsigned char*>(atoms)+i*32;
            if(!std::memcmp(p,&g_p20After[i],32))std::memcpy(p,&g_p20Before[i],32);}
    }
    g_p20HaveBackup=false;
}
#include "p25_holster.inl"
void P20Apply(void* mesh){
    if(!g_p20Enabled.load()||!g_p10GameplayActive.load()||!P15Focused()||!P20IsPlayerMesh(mesh))return;
    unsigned char state=255,cameraState=255;
    const bool readable=P19Read(g_p20Pawn.load(),0x7f0,state)&&P19Read(g_p20Pawn.load(),0x68bd,cameraState);
    const bool ordinary=readable&&p20::OrdinaryMovementState(state);
    const auto now=GetTickCount64();
    const bool nativeAnimation=now<g_p41NativeAnimationUntil.load(std::memory_order_acquire);
    const bool bodyMode=g_p20Enabled.load()&&ordinary&&cameraState==0&&
        !g_nativeHeadLock.load(std::memory_order_relaxed)&&!nativeAnimation;
    if(!bodyMode){
        g_p20NativeOwned=true;
        if(++g_p20Skipped<=3||g_p20Skipped%600==0)
            Log("MC6F native animation retained: movement=%u camera=%u headLock=%d actionHandoff=%d skipped=%llu",
                state,cameraState,g_nativeHeadLock.load()?1:0,nativeAnimation?1:0,static_cast<unsigned long long>(g_p20Skipped));
        return;
    }
    P19Skeleton liveRig;
    if(!P19FindSkeleton(mesh,liveRig))return;
    if(mesh!=g_p20Mesh||liveRig.mesh!=g_p20RefMesh||liveRig.count!=g_p20RigCount){
        if(!P20ValidateRig(mesh)){P20Reject(1,"unsupported-rig",reinterpret_cast<long long>(liveRig.mesh),liveRig.count);return;}
    }
    void* atoms=nullptr;void* master=nullptr;int count=0;
    if(!P19Read(mesh,0x37c,master)||master){P20Reject(0,"master-pose",reinterpret_cast<long long>(master));return;}
    if(!P19Read(mesh,0x318,atoms)||!P19Read(mesh,0x320,count)||count!=g_p20RigCount||
       count<60||count>75||!P20Writable(atoms,size_t(count)*sizeof(p20::Atom))){
        P20Reject(1,"atoms",reinterpret_cast<long long>(atoms),count);return;}
    P20Tracked tracked;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);tracked=g_p20Tracked;}
    if(!tracked.tick||GetTickCount64()-tracked.tick>200){P20Reject(2,"stale-tracking",tracked.tick?GetTickCount64()-tracked.tick:-1);return;}
    {std::lock_guard<std::mutex> lock(g_p22RoomMutex);
        if(tracked.roomGeneration!=g_p22Room.generation){P20Reject(3,"room-generation",tracked.roomGeneration,g_p22Room.generation);return;}
        for(auto& position:tracked.position)position=position-g_p22Room.consumed;}
    p20::Skeleton original{};std::memcpy(&original,atoms,size_t(count)*sizeof(p20::Atom));
    for(int i=0;i<count;++i)if(!p20::Valid(original[i].q)||!p20::Valid(original[i].p)||!std::isfinite(original[i].scale)){
        P20Reject(4,"invalid-bone",static_cast<long long>(i));return;}
    // The captured mesh is upright; leave tilted/scripted root transforms alone.
    float z=0;if(!P19Read(mesh,0xe8,z)||z<.98f||z>1.02f){P20Reject(5,"mesh-upright",std::llround(z*10000));return;}
    auto forward=p20::Rotate(original[g_p20Rig.camera].q,{1,0,0});forward.z=0;
    if(p20::Length(forward)<.05f){P20Reject(6,"camera-forward",std::llround(p20::Length(forward)*10000));return;}
    forward=p20::Unit(forward);const p20::V up{0,0,1};const auto right=p20::Cross(up,forward);
    auto vector=[&](p20::V p){return right*p.x+up*p.y-forward*p.z;};
    // OpenXR right/up/back -> UE right/up/back is a handedness change.
    auto rotation=[&](p20::Q q){auto v=vector({q.x,q.y,q.z})*-1;return p20::Normalize({v.x,v.y,v.z,q.w});};
    const auto basis=p20::UprightHand(forward);
    // Store the grip-to-bone offset in canonical coordinates, not mesh coordinates.
    // Recenter changes tracking coordinates, not the anatomy of either wrist.
    for(int i=0;i<2;++i)if(tracked.handValid[i]&&!g_p23HandCalibrated[i]){
        if(i==0){
            g_p20HandOffset[0]=p28::LeftGripOffset();g_p23HandCalibrated[0]=true;
            Log("P31B left wrist uses fixed reference-bone grip axes; startup pose ignored. Right mapping unchanged.");continue;
        }
        if(i==1&&g_p22RightNeutral.load()){
            g_p20HandOffset[1]=p28::RightGripOffset();g_p23HandCalibrated[1]=true;
            Log("P28 right wrist uses fixed anatomical grip axes; startup controller angle ignored.");continue;
        }
        const auto desired=(i==1&&g_p22RightNeutral.load())?basis:
            original[i?g_p20Rig.rightHand:g_p20Rig.leftHand].q;
        g_p20HandOffset[i]=p20::Mul(p20::Conjugate(p20::GripCanonical(tracked.rotation[i+1])),p20::Mul(p20::Conjugate(basis),desired));
        g_p23HandCalibrated[i]=true;Log("P23 wrist %d initialized once; view recenter will preserve this grip offset.",i);
    }
    p20::Targets targets;
    targets.roomscaleFollow=false;
    if(g_p22BodyYaw.load()){
        auto look=p20::Rotate(rotation(tracked.rotation[0]),forward);look.z=0;
        if(p20::Length(look)>.05f)targets.bodyYaw=std::atan2(p20::Dot(p20::Cross(forward,look),up),p20::Dot(forward,look));
        // P24: use this pose's yaw directly. No torso-only low-pass delay.
    }
    targets.eyeAnchored=true;
    targets.armExtension=1.05f; // Keep near-native proportions; no long-arm stretching.
    const p20::Q yaw{0,0,std::sin(targets.bodyYaw*.5f),std::cos(targets.bodyYaw*.5f)};
    // Rotate the anatomical head/shoulder offset around the camera eye origin.
    // Rotating around the neck while the rendered eye stays fixed moves the
    // shoulders relative to the viewpoint even for an otherwise rigid turn.
    // Hybrid MC2 keeps the native pelvis/legs and uses the HMD only for a
    // bounded upper-spine lean plus shoulder yaw.
    float headCollision=1.0f;
    const auto collisionTick=g_p41CollisionTick.load(std::memory_order_acquire),collisionNow=GetTickCount64();
    if(g_p41BodyCollisionEnabled.load(std::memory_order_acquire)&&collisionTick&&
       collisionNow>=collisionTick&&collisionNow-collisionTick<250){
        headCollision=p41::SafeFraction(g_p41HeadCollisionFraction.load(std::memory_order_acquire));
    }
    const auto trackedHead=vector(tracked.position[0])*tracked.units*headCollision;
    targets.head=original[g_p20Rig.head].p+trackedHead;
    targets.left=original[g_p20Rig.camera].p+trackedHead+
        vector(tracked.position[1]-tracked.position[0])*tracked.units;
    targets.right=original[g_p20Rig.camera].p+trackedHead+
        vector(tracked.position[2]-tracked.position[0])*tracked.units;
    // Controller grip poses may physically enter the headset. A wrist target in
    // that near-camera volume makes the palm disappear at the near clip plane
    // while its fingers fill the view. Keep each wrist outside a head-sized
    // protected sphere, preserving its direction and normal tracking elsewhere.
    const auto eye=original[g_p20Rig.camera].p+trackedHead;
    targets.left=p20::KeepOutsideSphere(targets.left,eye,32.0f,forward-right*.35f-up*.12f);
    targets.right=p20::KeepOutsideSphere(targets.right,eye,32.0f,forward+right*.35f-up*.12f);
    targets.headRotation=p20::Mul(rotation(tracked.rotation[0]),original[g_p20Rig.head].q);
    targets.leftRotation=p20::GripTarget(tracked.rotation[1],basis,g_p20HandOffset[0]);
    targets.rightRotation=p20::GripTarget(tracked.rotation[2],basis,g_p20HandOffset[1]);
    targets.leftTracked=tracked.handValid[0];targets.rightTracked=tracked.handValid[1];
    if(g_p41BodyCollisionEnabled.load(std::memory_order_acquire)){
        // Keep tracked wrists outside a conservative torso capsule.  This only
        // changes IK targets; native collision, interaction logic and animation
        // state stay owned by Outlast.
        if(tracked.handValid[0])targets.left=p41::KeepOutsideCapsule(
            targets.left,original[g_p20Rig.hips].p,original[g_p20Rig.neck].p,13.0f,
            original[g_p20Rig.leftHand].p-original[g_p20Rig.neck].p);
        if(tracked.handValid[1])targets.right=p41::KeepOutsideCapsule(
            targets.right,original[g_p20Rig.hips].p,original[g_p20Rig.neck].p,13.0f,
            original[g_p20Rig.rightHand].p-original[g_p20Rig.neck].p);
    }
    const auto headDelta=targets.head-original[g_p20Rig.head].p;
    if(!tracked.handValid[0]){targets.left=original[g_p20Rig.head].p+
        p20::Rotate(yaw,original[g_p20Rig.leftHand].p-original[g_p20Rig.head].p)+headDelta;
        targets.leftRotation=p20::Mul(yaw,original[g_p20Rig.leftHand].q);}
    if(!tracked.handValid[1]){targets.right=original[g_p20Rig.head].p+
        p20::Rotate(yaw,original[g_p20Rig.rightHand].p-original[g_p20Rig.head].p)+headDelta;
        targets.rightRotation=p20::Mul(yaw,original[g_p20Rig.rightHand].q);}
    const auto leftReach=p20::Length(targets.left-targets.head),rightReach=p20::Length(targets.right-targets.head);
    if(leftReach>200||rightReach>200){P20Reject(7,"target-reach",std::llround(leftReach),std::llround(rightReach));return;}
    auto result=original;
    const bool solved=g_p20Rig.dynamicSchool?p20::TrackedArmsMapped(result,g_p20Parents,targets,count,
        g_p20Rig.leftUpper,g_p20Rig.leftLower,g_p20Rig.leftHand,
        g_p20Rig.rightUpper,g_p20Rig.rightLower,g_p20Rig.rightHand,g_p20Rig.tracked):
        p20::TrackedArms(result,g_p20Parents,targets,count);
    if(!solved){P20Reject(7,"tracked-arm-solver",-1);return;}
    // IK is allowed to rotate and reposition only. Reject the entire result if
    // any solver path ever changes model scale, preventing giant-hand/body
    // regressions even if a future pose helper is modified incorrectly.
    for(int i=0;i<count;++i)if(result[i].scale!=original[i].scale){
        P20Reject(8,"bone-scale",static_cast<long long>(i),
            std::llround((result[i].scale-original[i].scale)*1000000.0f));return;}
    if(g_p20NativeOwned){g_p20NativeOwned=false;g_p20BlendStarted=now;Log("PB1 ARM HANDOFF: native animation ended; blending to tracked controllers over 300 ms.");}
    const float blend=std::clamp(static_cast<float>(now-g_p20BlendStarted)/300.0f,0.0f,1.0f);
    if(blend<1.0f)for(int i=0;i<count;++i)if(P20TrackedArmBone(i))result[i]=p20::BlendAtom(original[i],result[i],blend);
    g_p20Before=original;g_p20After=result;g_p20Atoms=atoms;g_p20HaveBackup=true;
    for(int i=0;i<count;++i)if(P20TrackedArmBone(i)&&std::memcmp(&original[i],&result[i],sizeof(p20::Atom)))
        std::memcpy(static_cast<unsigned char*>(atoms)+i*32,&result[i],sizeof(p20::Atom));
    g_p20LastAppliedTick.store(now,std::memory_order_release);
    if(++g_p20Applied<=3||g_p20Applied%600==0)
        Log("PF9 TRACKED ARMS: applied=%llu rig=%d school=%d state=%u hands=%d/%d blend=%.2f headCollision=%.3f; native torso/head/camera/hips/legs retained.",g_p20Applied,count,g_p20Rig.dynamicSchool?1:0,state,tracked.handValid[0]?1:0,tracked.handValid[1]?1:0,blend,headCollision);
}
void __fastcall P20Eval(void* mesh,float dt,int tick){P20Restore(mesh);g_p20Eval(mesh,dt,tick);}
void __fastcall P20Post(void* mesh,float dt){g_p20Post(mesh,dt);P20Apply(mesh);}
bool P20Install(){
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const unsigned char calls[]={0xe8,0x26,0x96,0,0,0x41,0x0f,0x28,0xce,0x48,0x8b,0xcb,0xe8,0x2a,0x8d,0,0};
    const unsigned char eval[]={0x48,0x8b,0xc4,0x55,0x56,0x57,0x41,0x54};
    const unsigned char post[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10};
    if(!base||!P19RigSignatures()||std::memcmp(base+0x72c105,calls,sizeof(calls))||std::memcmp(base+0x735730,eval,sizeof(eval))||std::memcmp(base+0x734e40,post,sizeof(post)))return false;
    unsigned char* stub=nullptr;const uintptr_t start=reinterpret_cast<uintptr_t>(base)&~uintptr_t(65535);
    for(uintptr_t delta=0x10000;delta<0x40000000&&!stub;delta+=0x10000)
        stub=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(start+delta),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!stub)return false;
    const uintptr_t targets[]={reinterpret_cast<uintptr_t>(&P20Eval),reinterpret_cast<uintptr_t>(&P20Post)};
    for(int i=0;i<2;++i){auto* p=stub+i*16;p[0]=0x48;p[1]=0xb8;std::memcpy(p+2,&targets[i],8);p[10]=0xff;p[11]=0xe0;}
    DWORD old=0;if(!VirtualProtect(stub,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(stub,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),stub,32);
    if(!VirtualProtect(base+0x72c105,sizeof(calls),PAGE_EXECUTE_READWRITE,&old)){VirtualFree(stub,0,MEM_RELEASE);return false;}
    g_p20Eval=reinterpret_cast<P20EvalFn>(base+0x735730);g_p20Post=reinterpret_cast<P20PostFn>(base+0x734e40);
    for(int i=0;i<2;++i){auto* call=base+(i?0x72c111:0x72c105);const int32_t rel=static_cast<int32_t>(reinterpret_cast<intptr_t>(stub+i*16)-reinterpret_cast<intptr_t>(call+5));std::memcpy(call+1,&rel,4);}
    DWORD ignored=0;VirtualProtect(base+0x72c105,sizeof(calls),old,&ignored);FlushInstructionCache(GetCurrentProcess(),base+0x72c105,sizeof(calls));
    Log("P20 IK HOOK: validated normal skeletal tick call pair installed; other call sites unchanged.");return true;
}
