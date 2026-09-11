#include "p25_holster.h"
// Reuse the original camera component for gameplay/HUD state, but P34 removes
// its geometry from the player attachment record so it can never flash into a
// VR hand. No camera gameplay/battery state is changed.
void* g_p25Pawn=nullptr;void* g_p25Camera=nullptr;
void* g_p25Parent=nullptr;
p25::Attachment g_p25Before{},g_p25After{};
bool g_p25HaveRecord=false,g_p25OwnVisibility=false,g_p25SeenCamera=false;
std::atomic<uint64_t> g_p26HolsterSnapshot{0};
uint64_t g_p26StatusCalls=0;
bool g_p25Enabled=true,g_p25ConfigRead=false,g_p25Logged=false;
bool g_p32HideCamcorder=true;
std::atomic<bool> g_p32CameraRaised{false};
bool g_p34CamcorderRemovedLogged=false;
using P25HiddenFn=void(__fastcall*)(void*,int);
bool P25Signatures(){
    auto* b=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    const unsigned char camera[]={0x48,0x8b,0x8b,0xac,0x66,0,0};
    const unsigned char array[]={0x49,0x8d,0x9e,0x5c,0x04,0,0};
    const unsigned char stride[]={0x4d,0x6b,0xff,0x34};
    const unsigned char hidden[]={0x44,0x8b,0x81,0x88,0x01,0,0,0x41,0x8b,0xc0,0xc1,0xe8,0x02};
    return b&&!std::memcmp(b+0xef20aa,camera,sizeof(camera))&&!std::memcmp(b+0x72c866,array,sizeof(array))&&
        !std::memcmp(b+0x72c89d,stride,sizeof(stride))&&!std::memcmp(b+0x29f850,hidden,sizeof(hidden));
}
bool P25Camera(void* pawn,void*& camera){
    void* model=nullptr;void* bones=nullptr;int count=0,capacity=0;
    if(!P19Read(pawn,0x66ac,camera)||!camera||!P19Read(camera,0x278,model)||
       !P19Read(model,0xcc,bones)||!P19Read(model,0xd4,count)||!P19Read(model,0xd8,capacity)||
       count!=2||capacity<2||capacity>32||!P15Readable(bones,160))return false;
    return P19BoneName(bones)=="bn_cam"&&P19BoneName(static_cast<unsigned char*>(bones)+80)=="bn_screen";
}
p25::Attachment* P25Record(void* parent,void* camera){
    void* records=nullptr;int count=0,capacity=0;
    if(!P19Read(parent,0x45c,records)||!P19Read(parent,0x464,count)||!P19Read(parent,0x468,capacity)||
       count<1||count>256||capacity<count||capacity>512||!P20Writable(records,count*sizeof(p25::Attachment)))return nullptr;
    p25::Attachment* found=nullptr;
    for(int i=0;i<count;++i){auto* r=static_cast<p25::Attachment*>(records)+i;
        if(r->component==reinterpret_cast<uint64_t>(camera)){if(found)return nullptr;found=r;}}
    return found;
}
bool P25SetHidden(void* camera,bool hidden){
    void* table=nullptr;void* function=nullptr;
    auto* b=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if(!P19Read(camera,0,table)||!P19Read(table,0x4d0,function)||function!=b+0x29f850)return false;
    reinterpret_cast<P25HiddenFn>(function)(camera,hidden?1:0);return true;
}
// P34: "removed" means the native camera UObject remains alive for the game's
// own battery/HUD/state machine, while its skeletal attachment has zero scale.
// That prevents the one-frame hand flash even if the game briefly unhides the
// component during the native toggle transition.
bool P34RemoveCamcorderGeometry(void* mesh,void* camera){
    if(!mesh||!camera)return false;
    auto* record=P25Record(mesh,camera);if(!record)return false;
    if(record->scale.x!=0.0f||record->scale.y!=0.0f||record->scale.z!=0.0f){
        record->scale={0,0,0};
    }
    P25SetHidden(camera,true);
    if(!g_p34CamcorderRemovedLogged){
        g_p34CamcorderRemovedLogged=true;
        Log("P34 CAMCORDER GEOMETRY REMOVED: native camera UObject retained for gameplay/HUD, player attachment scale forced to zero; transition flash blocked.");
    }
    return true;
}
void P25ForceCamcorderGoneNow(){
    if(!g_p32HideCamcorder)return;
    auto* pawn=g_p20Pawn.load();auto* mesh=g_p20Mesh;void* camera=nullptr;
    if(pawn&&mesh&&P25Camera(pawn,camera)){g_p25Camera=camera;P34RemoveCamcorderGeometry(mesh,camera);}
}
void P25RestoreRecord(void* mesh){
    if(!g_p25HaveRecord||mesh!=g_p25Parent)return;
    // When P34 removes the camera, deliberately do not restore its scale while
    // the mod is active. The object is recreated by the game on a new session.
    if(g_p32HideCamcorder){g_p25HaveRecord=false;return;}
    auto* record=P25Record(mesh,g_p25Camera);
    if(record&&!std::memcmp(record,&g_p25After,sizeof(*record)))std::memcpy(record,&g_p25Before,sizeof(*record));
    g_p25HaveRecord=false;
}
void P25Stop(){
    g_p26HolsterSnapshot.store(0);
    P25RestoreRecord(g_p25Parent);
    void* camera=nullptr;unsigned char state=255;uint32_t flags=0;
    if(!g_p32HideCamcorder&&g_p25OwnVisibility&&g_p25Pawn==g_p20Pawn.load()&&P25Camera(g_p25Pawn,camera)&&camera==g_p25Camera&&
       P19Read(g_p25Pawn,0x68bd,state)&&state<=1&&P19Read(camera,0x188,flags)&&!(flags&4))P25SetHidden(camera,true);
    g_p25OwnVisibility=false;
}
void P25Apply(void* mesh,bool bodyUpdated){
    if(!P20IsPlayerMesh(mesh))return;
    g_p26HolsterSnapshot.store(0);
    if(!g_p25ConfigRead){g_p25ConfigRead=true;
        const auto config=ModuleDir()+L"\\outlast2_vr_p32.ini";
        g_p25Enabled=GetPrivateProfileIntW(L"VR",L"CamcorderHolster",0,config.c_str())!=0&&P25Signatures();
        g_p32HideCamcorder=GetPrivateProfileIntW(L"VR",L"HideCamcorderModel",1,config.c_str())!=0;
        Log("P34 camera presentation: physical camera geometry=%s holster=%s; right-grip remains native camera button.",
            g_p32HideCamcorder?"removed":"native",g_p25Enabled?"enabled":"disabled");}
    auto* pawn=g_p20Pawn.load();void* camera=nullptr;
    if(pawn!=g_p25Pawn){P25Stop();g_p25Pawn=pawn;g_p25Camera=nullptr;g_p25SeenCamera=false;g_p25Logged=false;g_p34CamcorderRemovedLogged=false;}
    unsigned char cameraState=255,movementState=255;uint32_t flags=0;
    if(!P25Camera(pawn,camera)||!P19Read(pawn,0x68bd,cameraState)||
       !P19Read(pawn,0x7f0,movementState)||!P19Read(camera,0x188,flags)){g_p32CameraRaised.store(false);P25Stop();return;}
    // Treat state 1 as raised, but P34 HUD no longer depends on this byte.
    g_p32CameraRaised.store(cameraState==1);
    if(g_p25Camera&&camera!=g_p25Camera){P25Stop();g_p25SeenCamera=false;g_p34CamcorderRemovedLogged=false;}
    g_p25Camera=camera;

    if(g_p32HideCamcorder){
        // Keep this suppression after animation evaluation on every player-body
        // update. We intentionally return before any old holster code can put the
        // camera back at the right hip, leaving the hip exclusively for bandages.
        P34RemoveCamcorderGeometry(mesh,camera);
        g_p26HolsterSnapshot.store(0);g_p25OwnVisibility=false;return;
    }

    if(!g_p25Enabled){P25Stop();return;}
    if(cameraState==1)g_p25SeenCamera=true;
    if(!bodyUpdated||cameraState>1||movementState!=0||!P15Focused()){P25Stop();return;}
    auto* record=P25Record(mesh,camera);P19Skeleton rig;uint64_t rootName=0;
    if(!record||!P19FindSkeleton(mesh,rig)||rig.count!=75||!P19Read(rig.bones,0,rootName)){P25Stop();return;}
    P20Tracked tracked;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);tracked=g_p20Tracked;}
    const float units=tracked.units;
    auto holster=*record;
    if(!p25::Holster(holster,g_p20Before,g_p20After,units,rootName)){P25Stop();return;}
    auto forward=p20::Rotate(g_p20Before[1].q,{1,0,0});forward.z=0;forward=p20::Unit(forward);
    auto right=p20::Cross({0,0,1},forward);p20::V position=tracked.position[2];
    {std::lock_guard<std::mutex> lock(g_p22RoomMutex);
        if(tracked.roomGeneration!=g_p22Room.generation){P25Stop();return;}
        position=position-g_p22Room.consumed;}
    const auto hand=g_p20Before[1].p+(right*position.x+p20::V{0,0,position.y}-forward*position.z)*units;
    const auto hip=g_p20After[0].p+p20::Rotate(g_p20After[0].q,holster.translation);
    const bool withinHip=tracked.handValid[1]&&GetTickCount64()-tracked.tick<200&&p20::Length(hand-hip)<.25f*units;
    g_p26HolsterSnapshot.store((GetTickCount64()<<2)|1|(withinHip?2:0));
    auto desired=cameraState==0?holster:*record;
    if(++g_p26StatusCalls<=3||g_p26StatusCalls%300==0)Log("P26 CAMCORDER: state=%u hidden=%d nearHip=%d tracked=%d; native hand/hip attachment ready.",cameraState,(flags&4)?1:0,withinHip?1:0,tracked.handValid[1]?1:0);
    g_p25Parent=mesh;g_p25Before=*record;g_p25After=desired;
    std::memcpy(record,&desired,sizeof(desired));g_p25HaveRecord=true;
    if(flags&4){if(!P25SetHidden(camera,false)){P25Stop();return;}g_p25OwnVisibility=true;}
    if(!g_p25Logged){g_p25Logged=true;Log("P26 HOLSTER ACTIVE: native bn_cam/bn_screen mesh at right hip; original material slots retained; no Hero bone changes.");}
}
