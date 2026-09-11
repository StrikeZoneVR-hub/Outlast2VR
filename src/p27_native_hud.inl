#include "p27_native_hud.h"
bool P27InstallNativeHud(){
    if(!GetPrivateProfileIntW(L"VR",L"NativeScreenHUD",1,(ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())){
        Log("P27 native screen HUD disabled by configuration.");return true;
    }
    auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const unsigned char zeroEsi[]={0x33,0xf6,0x83,0xca,0xff,0x48,0x8b,0xd9};
    const unsigned char mode2[]={0x48,0x8b,0x87,0xfc,0x07,0,0,0x4c,0x8b,0xb7,0xdc,0x07,0,0,0x49,0x89,0x86,0x20,0x01,0,0};
    if(!base||!P15Readable(base+p27::RaiseRva,sizeof(p27::RaiseBytes))||
       !p27::MatchesRaise(base+p27::RaiseRva,sizeof(p27::RaiseBytes))||
       !P15Readable(base+0xeabdb7,sizeof(zeroEsi))||std::memcmp(base+0xeabdb7,zeroEsi,sizeof(zeroEsi))||
       !P15Readable(base+0xf0ee4d,sizeof(mode2))||std::memcmp(base+0xf0ee4d,mode2,sizeof(mode2)))return false;
    auto* byte=base+p27::RaiseRva+p27::ModeByte;DWORD old=0;
    if(!VirtualProtect(byte,1,PAGE_EXECUTE_READWRITE,&old))return false;
    *byte=2;
    DWORD ignored=0;const bool protectedAgain=VirtualProtect(byte,1,old,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),byte,1);
    Log("P27 NATIVE HUD ROUTE: camera-raise mode 1 -> 2, verified native CameraHudPro-tex path; battery/gameplay unchanged. Runtime visibility and battery drain require testing; screen-only NV NOT implemented. Page protection restored=%d",protectedAgain?1:0);
    return true;
}




