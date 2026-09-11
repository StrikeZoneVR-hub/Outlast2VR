// Process-local bridge; no virtual device driver or desktop input injection.
using P18GetFn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using P18SetFn=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
P18GetFn g_p18Get=nullptr;
P18SetFn g_p18Set=nullptr;
std::mutex g_p18Mutex;
XINPUT_STATE g_p18State{};
XINPUT_VIBRATION g_p18Rumble{};
ULONGLONG g_p18Tick=0,g_p18RumbleTick=0;
bool g_p18Connected=false;
std::atomic<uint64_t> g_p18Polls{0},g_p18Rumbles{0};
bool P18Fresh(ULONGLONG now){return g_p18Connected&&now-g_p18Tick<250;}
void P18Publish(const XINPUT_GAMEPAD& pad,bool connected){
    std::lock_guard<std::mutex> lock(g_p18Mutex);
    if(std::memcmp(&pad,&g_p18State.Gamepad,sizeof(pad)))++g_p18State.dwPacketNumber;
    g_p18State.Gamepad=pad;g_p18Connected=connected;g_p18Tick=GetTickCount64();
    if(!connected){g_p18Rumble={};g_p18RumbleTick=0;}
}
XINPUT_GAMEPAD P18MergeGamepad(const XINPUT_GAMEPAD& physical,const XINPUT_GAMEPAD& vr){
    XINPUT_GAMEPAD merged=physical;
    merged.wButtons=static_cast<WORD>(physical.wButtons|vr.wButtons);
    merged.bLeftTrigger=std::max(physical.bLeftTrigger,vr.bLeftTrigger);
    merged.bRightTrigger=std::max(physical.bRightTrigger,vr.bRightTrigger);
    merged.sThumbLX=std::abs(vr.sThumbLX)>std::abs(physical.sThumbLX)?vr.sThumbLX:physical.sThumbLX;
    merged.sThumbLY=std::abs(vr.sThumbLY)>std::abs(physical.sThumbLY)?vr.sThumbLY:physical.sThumbLY;
    merged.sThumbRX=std::abs(vr.sThumbRX)>std::abs(physical.sThumbRX)?vr.sThumbRX:physical.sThumbRX;
    merged.sThumbRY=std::abs(vr.sThumbRY)>std::abs(physical.sThumbRY)?vr.sThumbRY:physical.sThumbRY;
    return merged;
}
DWORD WINAPI P18GetState(DWORD index,XINPUT_STATE* out){
    if(!out)return ERROR_BAD_ARGUMENTS;
    XINPUT_STATE physical{};
    const DWORD physicalResult=g_p18Get?g_p18Get(index,&physical):ERROR_DEVICE_NOT_CONNECTED;
    if(index==0){
        std::lock_guard<std::mutex> lock(g_p18Mutex);
        if(P18Fresh(GetTickCount64())){
            if(physicalResult==ERROR_SUCCESS){
                *out=physical;
                out->Gamepad=P18MergeGamepad(physical.Gamepad,g_p18State.Gamepad);
                out->dwPacketNumber=physical.dwPacketNumber+g_p18State.dwPacketNumber;
            }else *out=g_p18State;
            ++g_p18Polls;
            return ERROR_SUCCESS;
        }
    }
    if(physicalResult==ERROR_SUCCESS)*out=physical;
    return physicalResult;
}
DWORD WINAPI P18SetState(DWORD index,XINPUT_VIBRATION* value){
    if(!value)return ERROR_BAD_ARGUMENTS;
    const DWORD physicalResult=g_p18Set?g_p18Set(index,value):ERROR_DEVICE_NOT_CONNECTED;
    if(index==0){
        std::lock_guard<std::mutex> lock(g_p18Mutex);
        if(P18Fresh(GetTickCount64())){g_p18Rumble=*value;g_p18RumbleTick=GetTickCount64();++g_p18Rumbles;return ERROR_SUCCESS;}
    }
    return physicalResult;
}
bool P18InstallXInput(){
    // Match resolved exports, including the shipping executable's ordinal imports.
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress)return false;
    int hooked=0;
    for(auto* d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d){
        const char* name=reinterpret_cast<char*>(base+d->Name);
        if(_stricmp(name,"xinput1_3.dll"))continue;
        HMODULE dll=GetModuleHandleA(name);if(!dll)continue;
        auto get=reinterpret_cast<P18GetFn>(::GetProcAddress(dll,"XInputGetState"));
        auto set=reinterpret_cast<P18SetFn>(::GetProcAddress(dll,"XInputSetState"));
        for(auto* t=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+d->FirstThunk);t->u1.Function;++t){
            ULONGLONG target=0;
            if(get&&t->u1.Function==reinterpret_cast<ULONGLONG>(get)){g_p18Get=get;target=reinterpret_cast<ULONGLONG>(&P18GetState);}
            if(set&&t->u1.Function==reinterpret_cast<ULONGLONG>(set)){g_p18Set=set;target=reinterpret_cast<ULONGLONG>(&P18SetState);}
            if(!target)continue;
            DWORD old=0;if(!VirtualProtect(&t->u1.Function,8,PAGE_READWRITE,&old))continue;
            t->u1.Function=target;DWORD ignored=0;VirtualProtect(&t->u1.Function,8,old,&ignored);++hooked;
        }
    }
    Log("P18 XINPUT BRIDGE: %d/2 imports hooked (state and native game rumble).",hooked);
    return hooked==2;
}
