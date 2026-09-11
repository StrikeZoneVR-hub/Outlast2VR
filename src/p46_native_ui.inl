// Observe real script calls; never manufacture an FFrame or invoke game code
// from the render thread. Entry addresses and prologues are from this retail exe.
namespace p46ui {
using Fn=void(__fastcall*)(void*,void*,void*);
std::atomic<bool> mainMenu{false},mainKnown{false},paused{false},loading{false};
std::atomic<bool> loadingSeen{false},loadingCompleted{false};
std::atomic<unsigned> installed{0};
Fn originals[6]{};
void* mainGetter=nullptr;void* scanArray=nullptr;int scanCount=0,scanIndex=0;
bool scanFinished=false;uint64_t lastQuery=0;
inline void SetCameraDiscovery(bool enabled){
    const bool previous=g_p46GameplayCameraDiscoveryEnabled.exchange(enabled,std::memory_order_acq_rel);
    if(enabled&&!previous){
        g_p46GameplayCameraDiscoveryStartFrame.store(g_probeFrameCounter.load(std::memory_order_relaxed),std::memory_order_release);
        g_p46SceneFrame.store(~0ull,std::memory_order_release);
        Log("P46 GAMEPLAY CAMERA DISCOVERY ARMED: menu/loading candidates excluded; waiting for a native full-resolution scene pass.");
    }
}
inline void MainResult(void* result){
    if(!result||!P15Readable(result,4))return;
    uint32_t v=0;std::memcpy(&v,result,4);if(v>1)return;
    const bool known=mainKnown.exchange(true);
    if(mainMenu.exchange(v!=0)!=(v!=0)||!known)Log("P46 NATIVE UI: mainMenu=%u",v);
    SetCameraDiscovery(v==0&&!paused.load()&&!loading.load());
}
void __fastcall Hero(void* a,void* b,void* c){originals[0](a,b,c);MainResult(c);}
void __fastcall Utils(void* a,void* b,void* c){originals[1](a,b,c);MainResult(c);}
void __fastcall Pause(void* a,void* b,void* c){paused=true;SetCameraDiscovery(false);Log("P46 NATIVE UI: pause entered");originals[2](a,b,c);}
void __fastcall Resume(void* a,void* b,void* c){originals[3](a,b,c);paused=false;SetCameraDiscovery(mainKnown.load()&&!mainMenu.load()&&!loading.load());Log("P46 NATIVE UI: pause left");}
void __fastcall Loading(void* a,void* b,void* c){loadingSeen=true;loading=true;SetCameraDiscovery(false);Log("P46 NATIVE UI: loading overlay entered");originals[4](a,b,c);}
void __fastcall Loaded(void* a,void* b,void* c){originals[5](a,b,c);loading=false;if(loadingSeen.load())loadingCompleted=true;SetCameraDiscovery(mainKnown.load()&&!mainMenu.load()&&!paused.load());Log("P46 NATIVE UI: loading overlay left");}
inline bool GameplayAllowed(){return mainKnown.load()?!mainMenu.load():loadingCompleted.load();}
inline bool Screen(){return paused.load()||loading.load()||!GameplayAllowed();}
inline bool StateKnown(){return mainKnown.load();}
inline bool SceneReady(bool gameplayRequested,bool sceneObserved,uint32_t& missing){
    if(!gameplayRequested){missing=0;return false;}
    missing=sceneObserved?0:std::min(120u,missing+1);
    return sceneObserved||missing<3;
}
inline const char* Reason(){return loading.load()?"native-loading":paused.load()?"native-pause":mainKnown.load()?(mainMenu.load()?"native-main-menu":"native-gameplay"):"awaiting-native-menu-state";}
inline void Tick(void* controller){
    if(!P15Focused())return;
    if(!mainGetter&&!scanFinished){
        if(!scanArray){
            if(!P19Read(p31native::Base(),0x21e2dc8,scanArray)||!P19Read(p31native::Base(),0x21e2dd0,scanCount)||
               !scanArray||scanCount<1||scanCount>500000){scanFinished=true;Log("P46 main-menu state discovery rejected: invalid object table");return;}
        }
        for(int steps=0;scanIndex<scanCount&&steps<1024;++scanIndex,++steps){
            void* object=nullptr;if(!P19Read(scanArray,size_t(scanIndex)*8,object)||!object)continue;
            if(p31native::Kind(object)!="Function"||p31native::Name(object)!="IsInMainMenu"||
               p31native::Name(p31native::Pointer(object,0x48))!="OLHero")continue;
            if(!p31native::Parameters(object,{{"ReturnValue",0,4}})){scanFinished=true;Log("P46 main-menu getter rejected: parameter layout differs");return;}
            mainGetter=object;scanFinished=true;Log("P46 main-menu getter validated; screen/gameplay routing now uses the original Hero state");break;
        }
        if(scanIndex>=scanCount&&!mainGetter){scanFinished=true;Log("P46 main-menu getter not found; loading-cycle fallback retained");}
    }
    const auto now=GetTickCount64();if(!mainGetter||now-lastQuery<100)return;lastQuery=now;
    void* pawn=nullptr;if(!controller||!P19Read(controller,0xc38,pawn)||!p31native::Object(pawn))return;
    p31native::Args args;
    if(p31native::Event(pawn,mainGetter,args))MainResult(args.bytes);
}
inline void Install(){
    struct Entry {size_t pair,name,fn;const char* text;Fn hook;};
    const Entry entries[]={
        {0x1FCAED0,0x19CD4A0,0xFAD870,"AOLHeroexecIsInMainMenu",Hero},
        {0x1FC9A90,0x19CE478,0xFB4070,"UOLUtilsexecIsInMainMenu",Utils},
        {0x1FC9190,0x19CCD50,0xFAAB30,"AOLGameexecHandlePaused",Pause},
        {0x1FC9180,0x19CCD30,0xFAAB70,"AOLGameexecHandleUnpaused",Resume},
        {0x1FCA660,0x19CDCA0,0xFAE070,"AOLPlayerControllerexecShowLoadingOverlay",Loading},
        {0x1FCA650,0x19CDC70,0xFAAE40,"AOLPlayerControllerexecHideLoadingOverlay",Loaded}};
    const unsigned char heroSig[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0xff,0x42,0x24};
    const unsigned char otherSig[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0xff,0x42,0x24,0x48,0x8b,0x42,0x24};
    auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    for(size_t i=0;i<std::size(entries);++i){
        const auto& e=entries[i];auto pair=reinterpret_cast<void**>(base+e.pair);
        if(!P15Readable(pair,16)||!P15Readable(base+e.name,std::strlen(e.text)+1)||!P15Readable(base+e.fn,14)||
           pair[0]!=base+e.name||pair[1]!=base+e.fn||std::strcmp(reinterpret_cast<char*>(base+e.name),e.text)||
           std::memcmp(base+e.fn,i?otherSig:heroSig,14)){
            Log("P46 native UI hook rejected: %s (signature); cursor fallback retained",e.text);continue;
        }
        DWORD protect=0;if(!VirtualProtect(pair+1,8,PAGE_READWRITE,&protect))continue;
        originals[i]=reinterpret_cast<Fn>(pair[1]);pair[1]=reinterpret_cast<void*>(e.hook);
        DWORD ignored=0;VirtualProtect(pair+1,8,protect,&ignored);++installed;
    }
    Log("P46 native UI observers installed: %u/6; original game functions and input preserved",installed.load());
}
}
