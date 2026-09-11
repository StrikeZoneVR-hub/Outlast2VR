// AActor::execMove call and Actor location/rotation fields validated in this EXE.
// Use the engine's sweep; never write Actor.Location or bypass collision flags.
// The argument is the native controller captured by the validated view hook.
// The pawn is reached through the same controller->pawn field used by the
// validated movement and rig-capture paths below.
std::mutex g_p22RoomMutex;
struct P22RoomPose {p20::V head{},consumed{};float units=100;ULONGLONG tick=0;uint64_t generation=0;} g_p22Room;
std::atomic<bool> g_p22RoomEnabled{true};
std::atomic<bool> g_p22RoomBasisValid{true};
using P22MoveActorFn=int(__fastcall*)(void*,void*,const p20::V*,const int*,unsigned,void*);
P22MoveActorFn g_p22MoveActor=nullptr;
bool g_p22MovementChecked=false;
uint32_t g_p22LockOnFrames=0,g_p22LockOffFrames=0,g_p22StateMissingFrames=0;
uint64_t g_p22StateSamples=0;
void P22ResetRoom(){std::lock_guard<std::mutex> lock(g_p22RoomMutex);auto generation=g_p22Room.generation+1;g_p22Room={};g_p22Room.generation=generation;}
P22RoomPose P22PublishRoom(p20::V head,float units){
    std::lock_guard<std::mutex> lock(g_p22RoomMutex);
    g_p22Room.head=head;g_p22Room.units=units;g_p22Room.tick=GetTickCount64();return g_p22Room;
}
void P22HoldHeadAt(p20::V head){
    std::lock_guard<std::mutex> lock(g_p22RoomMutex);
    if(p20::Valid(head))g_p22Room.consumed=head;
}
void* P22ResolvePawn(void* controller){
    if(!controller)return nullptr;
    // AOLPlayerController::Pawn is the exact field already used by P15 and
    // P19. Only accept the candidate when the complete validated pawn prefix
    // is readable; otherwise allow the direct-pawn fallback for callers that
    // already captured a pawn instance.
    void* pawn=nullptr;
    if(P19Read(controller,0xc38,pawn))
        return P15Readable(pawn,0x800)?pawn:nullptr;
    return P15Readable(controller,0x800)?controller:nullptr;
}
bool P22ScriptedMovementState(unsigned char state){
    // State 9 is the opening helicopter sequence in the captured public-beta
    // trace. Normal 0/1/2 gameplay retains body-anchored 6DoF and tracked arms.
    return p22::ScriptedMovementState(state);
}
void P22UpdateHeadLock(void* controller){
    const auto pawn=P22ResolvePawn(controller);
    unsigned char cameraState=255,movementState=255;
    const bool readable=pawn&&P19Read(pawn,0x68bd,cameraState)&&P19Read(pawn,0x7f0,movementState);
    if(!readable){
        ++g_p22StateMissingFrames;
        if(g_p22StateMissingFrames<=3||g_p22StateMissingFrames%300==0)
            Log("BETA1 CINEMATIC CAMERA: pawn state unavailable; controller=%p resolvedPawn=%p; lock remains %s.",controller,pawn,g_nativeHeadLock.load()?"ON":"OFF");
        return;
    }
    g_p22StateMissingFrames=0;
    const bool scripted=P22ScriptedMovementState(movementState)||cameraState==1;
    // Require a short run of matching samples to avoid toggling the camera on
    // one transition byte. Release more slowly so a scripted shot cannot
    // briefly regain headset-driven rotation while its movement state settles.
    if(scripted){g_p22LockOffFrames=0;if(g_p22LockOnFrames<3)++g_p22LockOnFrames;}
    else{g_p22LockOnFrames=0;if(g_p22LockOffFrames<6)++g_p22LockOffFrames;}
    const bool was=g_nativeHeadLock.load(std::memory_order_relaxed);
    if(!was&&scripted&&g_p22LockOnFrames>=2){
        g_nativeHeadLock.store(true,std::memory_order_release);
        Log("BETA1 CINEMATIC CAMERA: head lock ON (camcorderState=%u movementState=%u); native scripted camera owns the view until ordinary movement resumes.",static_cast<unsigned>(cameraState),static_cast<unsigned>(movementState));
    }else if(was&&!scripted&&g_p22LockOffFrames>=6){
        g_nativeHeadLock.store(false,std::memory_order_release);
        Log("BETA1 CINEMATIC CAMERA: head lock OFF (camcorderState=%u movementState=%u); native gameplay camera restored.",static_cast<unsigned>(cameraState),static_cast<unsigned>(movementState));
    }
    ++g_p22StateSamples;
    if(g_p22StateSamples<=3||g_p22StateSamples%900==0)
        Log("BETA1 CINEMATIC STATE: controller=%p pawn=%p camcorderState=%u movementState=%u scripted=%d lock=%d.",controller,pawn,static_cast<unsigned>(cameraState),static_cast<unsigned>(movementState),scripted?1:0,g_nativeHeadLock.load()?1:0);
}
bool P22MovementSignatures(){
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));if(!base)return false;
    const unsigned char call[]={0x48,0x8b,0x0d,0x17,0xe7,0xcb,0x01,0x4c,0x8d,0x8f,0x94,0,0,0};
    const unsigned char target[]={0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x48,0x08};
    const unsigned char location[]={0xf3,0x0f,0x58,0x81,0x88,0,0,0,0xf3,0x0f,0x11,0x81,0x88,0,0,0};
    const unsigned char direct[]={0xe8,0xa7,0xdd,0x08,0};
    return !std::memcmp(base+0x4ddc72,call,sizeof(call))&&!std::memcmp(base+0x56ba50,target,sizeof(target))&&
        !std::memcmp(base+0x65b355,location,sizeof(location))&&!std::memcmp(base+0x4ddca4,direct,sizeof(direct));
}
void P22NativeRoomMove(void* controller,float dt){
    if(g_nativeHeadLock.load(std::memory_order_relaxed)||!g_p22RoomEnabled.load()||!g_p22RoomBasisValid.load()||!g_p10GameplayActive.load()||!g_p10ViewProjectionValidated.load()||!P15Focused())return;
    const auto pawn=P22ResolvePawn(controller);if(!pawn)return;
    P22RoomPose pose;{std::lock_guard<std::mutex> lock(g_p22RoomMutex);pose=g_p22Room;}
    if(!pose.tick||GetTickCount64()-pose.tick>150)return;
    auto requested=p22::Request(pose.head,pose.consumed,dt,pose.units);if(p20::Length(requested)<.0001f)return;
    unsigned char state=255,physics=255;
    if(!P15Readable(pawn,0x800)||!P19Read(pawn,0x7f0,state)||!P19Read(pawn,0xc8,physics))return;
    if(state>2||physics!=1){static unsigned skipped=0;if(++skipped<=3)Log("P22 ROOM retained native movement: state=%u physics=%u (requires ordinary walking).",state,physics);return;}
    if(!g_p22MovementChecked){
        g_p22MovementChecked=true;
        if(P22MovementSignatures())g_p22MoveActor=reinterpret_cast<P22MoveActorFn>(reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr))+0x56ba50);
        Log("P22 ROOM MOVEMENT: native sweep signatures %s.",g_p22MoveActor?"verified":"rejected; capsule movement disabled");
    }
    if(!g_p22MoveActor)return;
    float camera[40]{};
    {std::lock_guard<std::mutex> lock(g_cbProbeMutex);auto it=g_cbProbeStates.find(g_p10ViewProjectionBuffer.load());
        if(it==g_cbProbeStates.end()||it->second.bytes.size()!=160||it->second.lastFrame+2<g_probeFrameCounter.load())return;
        std::memcpy(camera,it->second.bytes.data(),160);}
    // Match the flattened source-camera basis used by p12::PatchCameraBytes.
    p20::V forward{camera[3],camera[7],0},right{camera[0],camera[4],0};
    if(!p20::Valid(forward)||!p20::Valid(right)||p20::Length(forward)<.05f||p20::Length(right)<.05f)return;
    forward=p20::Unit(forward);const float sign=right.x*forward.y-right.y*forward.x>=0?1.f:-1.f;
    right={sign*forward.y,-sign*forward.x,0};
    const auto delta=p22::ToWorld(requested,right,forward,pose.units);
    p20::V before{},after{};int rotation[3]{};void* world=nullptr;
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!P19Read(base,0x219c390,world)||!P15Readable(world,0x200)||!P19Read(pawn,0x88,before)||!p20::Valid(before)||!P19Read(pawn,0x94,rotation))return;
    alignas(16) unsigned char hit[112]{};const float one=1;const int none=-1;
    std::memcpy(hit+0x28,&one,4);std::memcpy(hit+0x2c,&none,4);std::memcpy(hit+0x60,&none,4);
    g_p22MoveActor(world,pawn,&delta,rotation,0,hit);
    if(!P19Read(pawn,0x88,after)||!p20::Valid(after))return;
    const auto actual=after-before;
    if(p20::Length(actual)>p20::Length(delta)+2.f){g_p22RoomEnabled.store(false);Log("P22 ROOM MOVEMENT disabled: unexpected displacement; recenter required.");return;}
    const auto consumed=p22::ToLocal(actual,right,forward,pose.units);
    {std::lock_guard<std::mutex> lock(g_p22RoomMutex);if(g_p22Room.generation==pose.generation)g_p22Room.consumed=g_p22Room.consumed+consumed;}
    static unsigned count=0;if(++count<=3||count%600==0)Log("P22 HEAD-BODY FOLLOW: requested=%.3f actual=%.3f game units; collision respected, headset offset consumed once.",p20::Length(delta),p20::Length(actual));
}
