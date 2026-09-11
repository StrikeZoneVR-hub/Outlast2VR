// Validated from AOLPlayerController constructor and NativePlayerMove disassembly.
// Input is rotated only during native movement; controller/camera yaw is not written.
using P15MoveFn=void(__fastcall*)(void*,float);
P15MoveFn g_p15OriginalMove=nullptr;
std::atomic<float> g_p15MovementYaw{0};
std::atomic<ULONGLONG> g_p15PoseTick{0};
std::atomic<bool> g_p15WalkingEnabled{true};
std::atomic<uint64_t> g_p15MovementCalls{0};
constexpr bool kP16ContinuousAimInjectionEnabled=false;
bool P15Readable(const void* p,size_t bytes){
    MEMORY_BASIC_INFORMATION info{};
    if(!p||!VirtualQuery(p,&info,sizeof(info))||info.State!=MEM_COMMIT||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto a=reinterpret_cast<uintptr_t>(p),b=reinterpret_cast<uintptr_t>(info.BaseAddress);
    return a>=b&&bytes<=info.RegionSize&&a-b<=info.RegionSize-bytes;
}
bool P15Focused(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
void __fastcall P15NativeMove(void* self,float dt){
    if(self&&P15Readable(self,0xc40)){
        g_nativeControllerSelf.store(self,std::memory_order_release);
        static std::atomic<bool> controllerLogged{false};
        if(!controllerLogged.exchange(true,std::memory_order_acq_rel))
            Log("BETA1 CINEMATIC CAMERA: controller captured from validated native movement hook; pawn-state handoff enabled.");
    }
    // The native move/interaction pass can run before the generated script
    // wrapper is called for this frame. Once that wrapper has proven the live
    // Hero identity, keep its real GetViewRotation virtual patched here too.
    if(self&&P15Readable(self,0xc40)){
        void* pawn=nullptr;std::memcpy(&pawn,static_cast<unsigned char*>(self)+0xc38,8);
        if(pawn&&pawn==g_nativeHeroSelf.load(std::memory_order_acquire))
            P47EnsureNativeViewVirtual(pawn);
    }
    float* f=nullptr;float* r=nullptr;float oldF=0,oldR=0,newF=0,newR=0;
    if(g_p15WalkingEnabled.load()&&g_p10GameplayActive.load()&&P15Focused()&&
       GetTickCount64()-g_p15PoseTick.load()<250&&P15Readable(self,0xc40)){
        auto* object=static_cast<unsigned char*>(self);
        void* input=nullptr;void* pawn=nullptr;
        std::memcpy(&input,object+0x580,8);std::memcpy(&pawn,object+0xc38,8);
        if(P15Readable(input,0x1e0)&&P15Readable(pawn,0x7f1)){
            const auto state=*(static_cast<unsigned char*>(pawn)+0x7f0);
            // Leave the native ladder, scripted constrained movement and traversal branches unchanged.
            if(state!=3&&state!=4&&state!=5&&state!=6&&state!=16&&state!=17){
                f=reinterpret_cast<float*>(static_cast<unsigned char*>(input)+0x1d4);
                r=reinterpret_cast<float*>(static_cast<unsigned char*>(input)+0x1dc);
                std::memcpy(&oldF,f,4);std::memcpy(&oldR,r,4);newF=oldF;newR=oldR;
                if(p15::RotateInput(newF,newR,g_p15MovementYaw.load())){
                    std::memcpy(f,&newF,4);std::memcpy(r,&newR,4);
                    if(g_p15MovementCalls.fetch_add(1)==0)Log("P15 HMD WALKING ACTIVE: native forward/strafe rotated; WASD/gamepad share path; controller rotation untouched.");
                }else{f=r=nullptr;}
            }
        }
    }
    // PF16B: PlayerInput+1D8/+1E4 are frame-local turn/look deltas, not an
    // absolute view rotator. Feeding absolute HMD angles here accumulates yaw
    // every tick and spins the pawn. Leave both axes entirely native.
    g_p15OriginalMove(self,dt);
    p35runtime::Tick(self);
    // P23: disabled experimental capsule sweep after reported visual/trigger mismatch.
    // Preserve deliberate engine input resets/changes; undo only our temporary values.
    if(f&&r&&P15Readable(f,4)&&P15Readable(r,4)){if(*f==newF)std::memcpy(f,&oldF,4);if(*r==newR)std::memcpy(r,&oldR,4);}
}
bool P15InstallMovement(){
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!base)return false;
    // Constructor assigns vtable 1BD6720; exec wrapper calls slot A40.
    const unsigned char signature[]={0x48,0x8b,0xc4,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57};
    auto** slot=reinterpret_cast<void**>(base+0x1bd6720+0xa40);
    if(!P15Readable(slot,8)||*slot!=base+0xf3e260||std::memcmp(base+0xf3e260,signature,sizeof(signature)))return false;
    // Verify exact field loads used by this implementation, not generic UE3 offsets.
    const unsigned char inputLoad[]={0x48,0x8b,0x83,0x80,0x05,0,0,0xf3,0x0f,0x10,0x80,0xdc,0x01,0,0};
    if(std::memcmp(base+0xf3e6bb,inputLoad,sizeof(inputLoad)))return false;
    DWORD old=0;if(!VirtualProtect(slot,8,PAGE_READWRITE,&old))return false;
    g_p15OriginalMove=reinterpret_cast<P15MoveFn>(*slot);*slot=reinterpret_cast<void*>(&P15NativeMove);
    DWORD ignored=0;VirtualProtect(slot,8,old,&ignored);
    Log("P15 NATIVE MOVEMENT HOOK installed: exact vtable/function/field signatures verified.");
    return true;
}
