// Exact 2018 executable: NativePlayerMove calls F3F0E0 at F3ED0F.
// F3F20E passes hero+7E80 to the native FRotator-to-matrix routine.
// Scope this cache only during native interaction evaluation; do not move the
// pawn, alter its actor rotation, force an interaction, or advance a story flag.
using PF15InteractionFn=void(__fastcall*)(void*,float);
bool P20Writable(const void* p,size_t size);
PF15InteractionFn g_pf15InteractionOriginal=nullptr;
struct PF15InteractionViewScope {
    unsigned char* field=nullptr;
    int32_t old[3]{},applied[3]{};
    bool previous=false;
    PF15InteractionViewScope(void* pawn,float yaw,float pitch){
        if(!pawn||!std::isfinite(yaw)||!std::isfinite(pitch)||
           !P15Readable(pawn,0x7e8c))return;
        const auto state=*(static_cast<unsigned char*>(pawn)+0x7f0);
        if(state!=0&&state!=1&&state!=2)return;
        auto* candidate=static_cast<unsigned char*>(pawn)+0x7e80;
        if(!P20Writable(candidate,12))return;
        field=candidate;
        std::memcpy(old,field,12);std::memcpy(applied,old,12);
        applied[0]=p15::AddRotatorUnits(old[0],p15::RotatorUnits(pitch));
        applied[1]=p15::AddRotatorUnits(old[1],p15::RotatorUnits(yaw));
        std::memcpy(field,applied,12);
        previous=g_nativeControllerPitchSynchronized;
        g_nativeControllerPitchSynchronized=true;
    }
    ~PF15InteractionViewScope(){
        if(!field)return;
        // Do not subtract from a new scripted rotation authored by the game.
        // Restore only components that still contain our temporary value.
        if(P20Writable(field,12))for(int i=0;i<3;++i){
            int32_t current=0;std::memcpy(&current,field+i*4,4);
            if(current==applied[i])std::memcpy(field+i*4,&old[i],4);
        }
        g_nativeControllerPitchSynchronized=previous;
    }
};
void __fastcall PF15NativeInteraction(void* controller,float dt){
    if(!g_pf15InteractionOriginal)return;
    void* pawn=nullptr;
    if(g_p10GameplayActive.load()&&P15Focused()&&!g_nativeHeadLock.load()&&
       GetTickCount64()-g_p15PoseTick.load()<250&&P15Readable(controller,0xc40))
        std::memcpy(&pawn,static_cast<unsigned char*>(controller)+0xc38,8);
    PF15InteractionViewScope scope(pawn,g_nativeHeadYawRadians.load(),g_nativeHeadPitchRadians.load());
    if(scope.field){
        static bool logged=false;
        if(!logged){logged=true;Log("PF15 INTERACTION VIEW ACTIVE: native scan uses scoped hero cached HMD yaw/pitch; actor location, native movement and story eligibility unchanged.");}
    }
    g_pf15InteractionOriginal(controller,dt);
}
bool PF15InstallInteractionView(){
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const unsigned char call[]={0xe8,0xcc,0x03,0,0};
    const unsigned char read[]={0x48,0x8d,0x93,0x80,0x7e,0,0};
    const unsigned char entry[]={0x48,0x8b,0xc4,0xf3,0x0f,0x11,0x48,0x10};
    if(!base||!P15Readable(base+0xf3ed0f,5)||!P15Readable(base+0xf3f20e,7)||
       !P15Readable(base+0xf3f0e0,8)||std::memcmp(base+0xf3ed0f,call,5)||
       std::memcmp(base+0xf3f20e,read,7)||std::memcmp(base+0xf3f0e0,entry,8)){
        Log("PF15 INTERACTION VIEW not installed: executable signatures differ.");return false;
    }
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const uintptr_t gran=info.dwAllocationGranularity;
    const uintptr_t center=reinterpret_cast<uintptr_t>(base+0xf3ed0f)&~(gran-1);
    void* relay=nullptr;
    for(uintptr_t distance=gran;distance<0x70000000&&!relay;distance+=gran){
        for(int side=0;side<2&&!relay;++side){
            if(side&&center<distance)continue;
            const uintptr_t address=side?center-distance:center+distance;
            MEMORY_BASIC_INFORMATION memory{};
            if(VirtualQuery(reinterpret_cast<void*>(address),&memory,sizeof(memory))&&
               memory.State==MEM_FREE&&memory.RegionSize>=gran)
                relay=VirtualAlloc(reinterpret_cast<void*>(address),gran,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        }
    }
    if(!relay){Log("PF15 INTERACTION VIEW not installed: no near relay allocation.");return false;}
    unsigned char jump[12]={0x48,0xb8};
    const auto target=reinterpret_cast<uintptr_t>(&PF15NativeInteraction);
    std::memcpy(jump+2,&target,8);jump[10]=0xff;jump[11]=0xe0;
    std::memcpy(relay,jump,12);
    DWORD ignored=0;
    if(!VirtualProtect(relay,gran,PAGE_EXECUTE_READ,&ignored)){VirtualFree(relay,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),relay,12);
    const auto delta=reinterpret_cast<intptr_t>(relay)-reinterpret_cast<intptr_t>(base+0xf3ed14);
    if(delta<INT32_MIN||delta>INT32_MAX){VirtualFree(relay,0,MEM_RELEASE);return false;}
    DWORD oldProtect=0;
    if(!VirtualProtect(base+0xf3ed0f,5,PAGE_EXECUTE_READWRITE,&oldProtect)){VirtualFree(relay,0,MEM_RELEASE);return false;}
    g_pf15InteractionOriginal=reinterpret_cast<PF15InteractionFn>(base+0xf3f0e0);
    const int32_t rel=static_cast<int32_t>(delta);
    std::memcpy(base+0xf3ed10,&rel,4);
    FlushInstructionCache(GetCurrentProcess(),base+0xf3ed0f,5);
    VirtualProtect(base+0xf3ed0f,5,oldProtect,&ignored);
    Log("PF15 INTERACTION VIEW hook installed: exact call, function entry and hero cached-rotation read verified.");
    return true;
}
