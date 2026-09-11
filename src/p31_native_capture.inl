#pragma once
#include "p31_capture_math.h"
#include "p31_activation.h"
// Game-thread native capture support. Never invoke reflected functions merely
// because a name matched: validate the complete parameter list first.
namespace p31native {
inline bool Configured(){static const bool value=GetPrivateProfileIntW(L"VR",L"NativeLensCapture",0,(ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;return value;}
std::atomic<uint64_t> compositeTick{0},captureTick{0},generation{1};
std::atomic<bool> runtimeEnabled{false};
p31::Activation activation;
std::atomic<void*> publishedHud{nullptr};
inline unsigned char* Base(){return reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));}
inline std::string Name(void* o){return o?P19BoneName(static_cast<unsigned char*>(o)+0x50):std::string{};}
inline void* Pointer(void* o,size_t off){void* p=nullptr;P19Read(o,off,p);return p;}
inline bool Object(void* o){
    int index=-1,count=0;void* array=nullptr;void* entry=nullptr;
    return P19Read(o,0x40,index)&&P19Read(Base(),0x21e2dd0,count)&&index>=0&&index<count&&count<500000&&
        P19Read(Base(),0x21e2dc8,array)&&P19Read(array,size_t(index)*8,entry)&&entry==o;
}
inline std::string Kind(void* o){return Name(Pointer(o,0x58));}
struct Parameter {const char* name;int offset,size;};
inline bool Parameters(void* fn,std::initializer_list<Parameter> expected){
    if(!Object(fn)||Kind(fn)!="Function")return false;
    if(expected.size()>64)return false;
    void* field=Pointer(fn,0x88);size_t matched=0;uint64_t seen=0;
    for(int i=0;field&&i<128;++i,field=Pointer(field,0x68)){
        uint64_t flags=0;int off=-1,size=0;
        if(!P19Read(field,0x78,flags)||!(flags&0x80))continue;
        if(!P19Read(field,0x94,off)||!P19Read(field,0x74,size))return false;
        bool found=false;size_t index=0;
        for(const auto& p:expected){
            if(Name(field)==p.name&&off==p.offset&&size==p.size){
                const uint64_t bit=uint64_t{1}<<index;if(seen&bit)return false;
                seen|=bit;found=true;break;
            }
            ++index;
        }
        if(!found)return false;++matched;
    }
    return !field&&matched==expected.size();
}
inline void* Find(const char* name,const char* kind,const char* outer=nullptr){
    int count=0;void* array=nullptr;
    if(!P19Read(Base(),0x21e2dd0,count)||count<1||count>500000||!P19Read(Base(),0x21e2dc8,array))return nullptr;
    void* found=nullptr;
    for(int i=0;i<count;++i){
        void* o=nullptr;if(!P19Read(array,size_t(i)*8,o)||!o||Kind(o)!=kind||Name(o)!=name)continue;
        if(outer&&Name(Pointer(o,0x48))!=outer)continue;
        if(found)return nullptr;found=o;
    }
    return found;
}
struct Args {
    alignas(16) unsigned char bytes[512]{};
    template<class T> void Set(size_t at,const T& value){std::memcpy(bytes+at,&value,sizeof(value));}
    template<class T> T Get(size_t at)const{T value{};std::memcpy(&value,bytes+at,sizeof(value));return value;}
};
inline bool Event(void* object,void* fn,Args& args){
    if(!Object(object)||!Object(fn))return false;
    void* method=Pointer(Pointer(object,0),0x220);
    // UObject::ProcessEvent and AActor's forwarding override in this executable.
    const unsigned char objectSig[]={0x40,0x55,0x41,0x54,0x41,0x55,0x41,0x56};
    const unsigned char actorSig[]={0x48,0x8b,0x05,0x19,0xeb,0xdf,0x01,0x4c,0x8b,0xd2};
    if(method==Base()+0x76fb0){if(std::memcmp(method,objectSig,sizeof(objectSig)))return false;}
    else if(method==Base()+0x39d870){if(std::memcmp(method,actorSig,sizeof(actorSig)))return false;}
    else return false;
    using Fn=void(__fastcall*)(void*,void*,void*);
    reinterpret_cast<Fn>(method)(object,fn,args.bytes);return true;
}
struct Api {
    void *captureClass{},*spawn{},*create{},*setParameters{},*setView{},*boneLocation{},*boneRotation{},*destroy{},*setEnabled{},*setFrameRate{},*setMaterial{};
    void* scanArray=nullptr;int scanCount=0,scanIndex=0;
    bool scanDone=false,scanFailed=false;
    uint64_t scanStarted=0,scanLogged=0;
    // One pass, bounded work per game tick. Never perform repeated full UObject
    // scans synchronously from NativePlayerMove while the renderer waits.
    bool Prepare(){
        if(scanDone||scanFailed)return true;
        const auto now=GetTickCount64();
        if(!scanArray){
            if(!P19Read(Base(),0x21e2dd0,scanCount)||scanCount<1||scanCount>500000||!P19Read(Base(),0x21e2dc8,scanArray)||!scanArray){scanFailed=true;return true;}
            scanStarted=scanLogged=now;Log("P31B API discovery started: %d objects, at most 128 objects/2ms per game tick.",scanCount);
        }
        if(now-scanStarted>120000){scanFailed=true;Log("P31B API discovery timed out; capture remains inactive.");return true;}
        void* currentArray=nullptr;int currentCount=0;
        if(!P19Read(Base(),0x21e2dc8,currentArray)||currentArray!=scanArray||!P19Read(Base(),0x21e2dd0,currentCount)||currentCount<scanCount){scanFailed=true;Log("P31B object table changed; refusing stale API discovery.");return true;}
        struct Request{const char* name;const char* kind;const char* outer;void** result;};
        Request requests[]={
            {"SceneCapture2DActor","Class","Engine",&captureClass},{"Spawn","Function","Actor",&spawn},
            {"Create","Function","TextureRenderTarget2D",&create},{"SetCaptureParameters","Function","SceneCapture2DComponent",&setParameters},
            {"SetView","Function","SceneCapture2DComponent",&setView},{"GetBoneLocation","Function","SkeletalMeshComponent",&boneLocation},
            {"GetBoneQuaternion","Function","SkeletalMeshComponent",&boneRotation},{"Destroy","Function","Actor",&destroy},
            {"SetEnabled","Function","SceneCaptureComponent",&setEnabled},{"SetFrameRate","Function","SceneCaptureComponent",&setFrameRate},
            {"SetMaterial","Function","SkeletalMeshComponent",&setMaterial}};
        p31::ScanBudget budget{now};
        for(;scanIndex<scanCount&&budget.Take(GetTickCount64());++scanIndex){
            void* object=nullptr;if(!P19Read(scanArray,size_t(scanIndex)*8,object)||!object)continue;
            const auto name=Name(object);
            for(auto& request:requests){
                if(name!=request.name||Kind(object)!=request.kind||Name(Pointer(object,0x48))!=request.outer)continue;
                if(*request.result){scanFailed=true;Log("P31B duplicate API %s; capture disabled.",request.name);return true;}
                *request.result=object;
            }
        }
        if(GetTickCount64()-scanLogged>=1000){scanLogged=GetTickCount64();Log("P31B API discovery progress: %d/%d",scanIndex,scanCount);}
        scanDone=scanIndex==scanCount;
        if(scanDone)Log("P31B API discovery complete in %llu ms; validating signatures next.",static_cast<unsigned long long>(GetTickCount64()-scanStarted));
        return scanDone;
    }
    bool Load(){
        if(!scanDone||scanFailed)return false;
        return captureClass&&Parameters(spawn,{{"SpawnClass",0,8},{"SpawnOwner",8,8},{"SpawnTag",16,8},
            {"SpawnLocation",24,12},{"SpawnRotation",36,12},{"ActorTemplate",48,8},{"bNoCollisionFail",56,4},{"bNoFail",60,4},{"ReturnValue",64,8}})&&
            Parameters(create,{{"InSizeX",0,4},{"InSizeY",4,4},{"InFormat",8,1},{"InClearColor",12,16},{"bOnlyRenderOnce",28,4},{"ReturnValue",32,8}})&&
            Parameters(setParameters,{{"NewTextureTarget",0,8},{"NewFOV",8,4},{"NewNearPlane",12,4},{"NewFarPlane",16,4}})&&
            Parameters(setView,{{"NewLocation",0,12},{"NewRotation",12,12}})&&
            Parameters(boneLocation,{{"BoneName",0,8},{"Space",8,4},{"ReturnValue",12,12}})&&
            Parameters(boneRotation,{{"BoneName",0,8},{"Space",8,4},{"ReturnValue",16,16}})&&
            Parameters(destroy,{{"ReturnValue",0,4}})&&
            Parameters(setEnabled,{{"bEnable",0,4}})&&
            Parameters(setFrameRate,{{"NewFrameRate",0,4}})&&Parameters(setMaterial,{{"ElementIndex",0,4},{"Material",4,8}});
    }
};
Api api;
inline bool NativeActorSignatures(){
    auto* base=Base();
    return p31::SpawnSignatures(base+0x569510,base+0x56a1e0,base+0x6a6fed,base+0x6a706e,base+0x6a6fa4)&&
        p31::LifecycleSignatures(base+0x5696fe,base+0x569711,base+0x56977c,base+0x56a2d3);
}
void* captureWorld=nullptr;
void *owner=nullptr,*actor=nullptr,*component=nullptr,*texture=nullptr;
bool attempted=false,loaded=false;
// Published only after the engine owns the texture via TextureTarget. Renderer
// must independently validate its resource and never call unknown COM pointers.
std::atomic<void*> publishedTexture{nullptr};
void *materialCamera=nullptr,*originalMaterial=nullptr,*opaqueMaterial=nullptr;
inline void* Material(void* camera){
    int count=0;void* array=nullptr;
    if(P19Read(camera,0x270,count)&&count>2&&count<64&&P19Read(camera,0x268,array)){
        void* value=Pointer(array,16);if(value)return value;
    }
    void* mesh=Pointer(camera,0x278);
    if(P19Read(mesh,0x8c,count)&&count>2&&count<64)return Pointer(Pointer(mesh,0x84),16);
    return nullptr;
}
inline bool SetMaterial(void* camera,void* material){
    if(!Object(camera)||!Object(material))return false;
    Args args;args.Set(0,int32_t{2});args.Set(4,material);return Event(camera,api.setMaterial,args)&&Material(camera)==material;
}
inline void RestoreMaterial(){
    if(Object(materialCamera)&&Material(materialCamera)==opaqueMaterial)SetMaterial(materialCamera,originalMaterial);
    materialCamera=originalMaterial=opaqueMaterial=nullptr;
}
inline void ApplyMaterial(void* camera,void* hud){
    const auto tick=compositeTick.load();
    if(!tick||GetTickCount64()-tick>250){RestoreMaterial();return;}
    if(materialCamera==camera&&Material(camera)==opaqueMaterial)return;
    RestoreMaterial();int slot=-1;void* opaque=Pointer(hud,0x81c);void* prior=Material(camera);
    if(!P19Read(hud,0x824,slot)||slot!=2||!Object(prior)||!Object(opaque)||Name(opaque)!="PlayerCameraHudOpaque-Mat-01")return;
    if(SetMaterial(camera,opaque)){materialCamera=camera;originalMaterial=prior;opaqueMaterial=opaque;Log("P31C opaque LCD material applied after fresh composite.");}
}
inline bool Enabled(bool enabled){
    uint32_t flags=0;if(!Object(component)||!P19Read(component,0x90,flags))return false;
    if(bool(flags&1u)==enabled)return true;
    // Use the native setter so its scene/render-thread state changes along with
    // the UObject flag. Writing bEnabled directly can leave a stale capture proxy.
    Args args;args.Set(0,int32_t{enabled?1:0});
    return Event(component,api.setEnabled,args)&&P19Read(component,0x90,flags)&&bool(flags&1u)==enabled;
}
inline void Stop(){
    captureTick.store(0);compositeTick.store(0);publishedHud.store(nullptr);publishedTexture.store(nullptr);++generation;
    RestoreMaterial();Enabled(false);
    if(Object(actor)&&Object(captureWorld)&&Kind(captureWorld)=="World"&&NativeActorSignatures()){
        using DestroyFn=int32_t(__fastcall*)(void*,void*,int32_t);
        const auto result=reinterpret_cast<DestroyFn>(Base()+0x56a1e0)(captureWorld,actor,0);
        Log("P31C native capture cleanup: result=%d",result);
    }
    captureWorld=nullptr;
    actor=component=texture=nullptr;
}
inline bool Pose(void* camera,p20::V& position,p31::Rotator& rotation){
    void* mesh=Pointer(camera,0x278);void* bones=Pointer(mesh,0xcc);uint64_t boneName=0;
    if(!P19Read(bones,0,boneName)||P19BoneName(bones)!="bn_cam")return false;
    Args loc,quat;loc.Set(0,boneName);quat.Set(0,boneName); // Space=0: world.
    if(!Event(camera,api.boneLocation,loc)||!Event(camera,api.boneRotation,quat))return false;
    return p31::LensPose(loc.Get<p20::V>(12),quat.Get<p20::Q>(16),position,rotation);
}
inline bool Start(void* pawn,void* camera,void* hud){
    Log("P31A stage: validating reflected capture API.");
    if(!loaded){loaded=api.Load();if(!loaded){Log("P31 capture disabled: reflected API parameter validation failed.");return false;}}
    Log("P31A stage: reading native lens pose.");
    p20::V location;p31::Rotator rotation;if(!Pose(camera,location,rotation))return false;
    if(!NativeActorSignatures()){Log("P31C spawn rejected: native call/signature mismatch.");return false;}
    void* world=Pointer(Base(),0x219c390);void* instigator=Pointer(pawn,0x11c);
    if(!Object(world)||Kind(world)!="World"||!Object(api.captureClass)||Kind(api.captureClass)!="Class"||Name(api.captureClass)!="SceneCapture2DActor"){
        Log("P31C spawn rejected: invalid world or capture class.");return false;
    }
    if(instigator&&!Object(instigator)){Log("P31C spawn rejected: invalid instigator.");return false;}
    Log("P31C stage: native UWorld::SpawnActor world=%p class=%p owner=%p",world,api.captureClass,pawn);
    using SpawnFn=void*(__fastcall*)(void*,void*,uint64_t,const p20::V*,const p31::Rotator*,void*,int32_t,int32_t,void*,void*,int32_t);
    actor=p31::SpawnLens(reinterpret_cast<SpawnFn>(Base()+0x569510),world,api.captureClass,pawn,instigator,location,rotation);
    if(!actor){Log("P31C native SpawnActor returned null; engine rejected creation.");return false;}
    captureWorld=world;
    Log("P31C native SpawnActor returned actor=%p",actor);
    // Never edit class defaults or any pre-existing actor. Object identity, class,
    // owner and instance flags must all match the newly returned runtime actor.
    uint32_t actorFlags=0;
    if(!Object(actor)||Pointer(actor,0x58)!=api.captureClass||actor==pawn||actor==api.captureClass||
       !P19Read(actor,0xf0,actorFlags)){
        Log("P31D capture instance validation failed; capture not enabled.");Stop();return false;
    }
    // Object RF_ClassDefaultObject bit 9 is used by AActor::ProcessEvent as well.
    uint32_t objectFlags=0;MEMORY_BASIC_INFORMATION region{};
    auto* flagAddress=static_cast<unsigned char*>(actor)+0xf0;
    if(!P19Read(actor,0x18,objectFlags)||(objectFlags&(1u<<9))||
       !VirtualQuery(flagAddress,&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))){
        Log("P31D capture instance flags are not safely writable; capture not enabled.");Stop();return false;
    }
    const auto dynamicFlags=p31::DynamicCaptureFlags(actorFlags);
    std::memcpy(flagAddress,&dynamicFlags,sizeof(dynamicFlags));
    Log("P31D owned capture is dynamic/deletable: flags %08x -> %08x; shared class unchanged.",actorFlags,dynamicFlags);
    Log("P31A stage: configuring capture component.");
    component=Pointer(actor,0x248);
    if(!Object(component)||Kind(component)!="SceneCapture2DComponent"){Stop();return false;}
    if(!Enabled(false)){Stop();return false;}
    Args rate;rate.Set(0,30.f);
    if(!Event(component,api.setFrameRate,rate)){Stop();return false;}
    // Calling the static factory on the existing RT object avoids guessed CDO offsets.
    void* existing=Pointer(hud,0x7fc);Args create;
    create.Set(0,int32_t{1024});create.Set(4,int32_t{576});create.Set(8,uint8_t{2}); // PF_A8R8G8B8.
    const std::array<float,4> clear{0,0,0,1};create.Set(12,clear);
    Log("P31A stage: creating lens render target.");
    if(!Object(existing)||Kind(existing)!="TextureRenderTarget2D"||!Event(existing,api.create,create)||
       !(texture=create.Get<void*>(32))||!Object(texture)){Stop();return false;}
    // SetCaptureParameters performs the engine's projection update and reattach.
    Args params;params.Set(0,texture);params.Set(8,70.f);params.Set(12,2.f);params.Set(16,0.f);
    Log("P31A stage: assigning lens render target.");
    if(!Event(component,api.setParameters,params)||Pointer(component,0xd8)!=texture){Stop();return false;}
    publishedTexture.store(texture);
    Log("P31A independent lens capture created: actor=%p component=%p texture=%p 1024x576 FOV70; awaiting a fresh GPU composite.",actor,component,texture);
    return true;
}
inline void Tick(void* controller){
    // Kept opt-in until capture/compositor/main-view isolation are tested together.
    if(!Configured())return;
    static thread_local bool inTick=false;
    if(inTick)return;
    struct Guard{bool& flag;Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(inTick);
    void* pawn=Pointer(controller,0xc38);void* hud=Pointer(controller,0xc40);void* camera=nullptr;unsigned char state=255;
    unsigned char movement=255;
    const bool eligible=g_p10GameplayActive.load()&&P15Focused()&&Object(pawn)&&P19Read(pawn,0x7f0,movement)&&movement==0;
    const bool enabled=activation.Update(reinterpret_cast<uintptr_t>(pawn),eligible,(GetAsyncKeyState(VK_F6)&0x8000)!=0,GetTickCount64());
    const bool wasEnabled=runtimeEnabled.exchange(enabled);
    if(wasEnabled!=enabled){
        Log("P31A lens trial %s by gameplay/F6 gate (movement=%u).",enabled?"ENABLED":"DISABLED",unsigned(movement));
        if(!enabled){Stop();attempted=false;}
    }
    static bool waitingLogged=false;
    if(!enabled){
        if(eligible&&!waitingLogged){Log("P31A safe startup: lens path inactive. After two seconds of ordinary gameplay press F6 to test; F6 again disables it.");waitingLogged=true;}
        return;
    }
    if(pawn!=owner){Stop();owner=pawn;attempted=false;}
    if(!Object(pawn)||!P15Focused()||!g_p10GameplayActive.load()||!P25Camera(pawn,camera)||!P19Read(pawn,0x68bd,state)||state!=1){captureTick.store(0);RestoreMaterial();Enabled(false);return;}
    if(!actor&&!attempted){
        if(!api.Prepare())return;
        attempted=true;if(!Start(pawn,camera,hud))Log("P31 lens initialization failed; native HUD and renderer left unchanged.");
    }
    if(!Object(component)||!Object(texture)){captureTick.store(0);RestoreMaterial();return;}
    p20::V location;p31::Rotator rotation;
    if(!Pose(camera,location,rotation)){captureTick.store(0);RestoreMaterial();Enabled(false);return;}
    Args view;view.Set(0,location);view.Set(12,rotation);
    if(!Event(component,api.setView,view)){captureTick.store(0);RestoreMaterial();Enabled(false);return;}
    if(Enabled(true)){publishedHud.store(Pointer(hud,0x7fc));captureTick.store(GetTickCount64());ApplyMaterial(camera,hud);}
    else{captureTick.store(0);RestoreMaterial();}
}
}




