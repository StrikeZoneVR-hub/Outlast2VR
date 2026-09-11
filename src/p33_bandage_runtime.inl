#include "p33_bandage.h"

namespace p33runtime {
struct Candidate {
    void* property=nullptr;
    std::string name,owner;
    int offset=-1,size=0,score=0;
};
std::atomic<int> bandages{-1};
std::atomic<bool> equipped{false};
std::atomic<float> wrapProgress{0};
std::atomic<uint64_t> healPulseUntil{0};
std::atomic<uint64_t> poseTick{0};
std::mutex stateMutex;
p33::WrapTracker wrap;
bool triggerDown=false;
bool enabled=true;
float grabRadius=.20f,wrapTurns=1.45f;
p20::V hipOffset{.23f,-.58f,.045f};

void* scanArray=nullptr;int scanCount=0,scanIndex=0;
bool scanDone=false,scanFailed=false;
std::vector<Candidate> candidates;
void* countBase=nullptr;int countOffset=-1,countSize=0;
uint64_t lastCountLog=0;

inline std::wstring Config(){return ModuleDir()+L"\\outlast2_vr_p33.ini";}
inline std::string Lower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
inline bool Contains(const std::string& s,const char* token){return Lower(s).find(token)!=std::string::npos;}
inline int Score(const std::string& name,const std::string& owner){
    const auto n=Lower(name),o=Lower(owner);int score=0;
    if(n=="numbandages"||n=="bandagecount"||n=="bandages")score+=100;
    if(n.find("bandage")!=std::string::npos)score+=60;
    if(n.find("count")!=std::string::npos||n.find("num")!=std::string::npos)score+=15;
    if(o.find("hero")!=std::string::npos||o.find("player")!=std::string::npos)score+=25;
    if(o.find("inventory")!=std::string::npos)score+=35;
    return score;
}
inline bool ReadCount(void* base,int offset,int size,int& value){
    value=-1;if(!base||offset<0||offset>0x4000)return false;
    if(size==1){unsigned char v=255;if(!P19Read(base,size_t(offset),v))return false;value=v;}
    else if(size==4){int v=-1;if(!P19Read(base,size_t(offset),v))return false;value=v;}
    else return false;
    return value>=0&&value<=3;
}
inline void* FindOwnedBase(void* controller,void* pawn,const std::string& owner){
    if(controller&&p31native::Object(controller)&&p31native::Kind(controller)==owner)return controller;
    if(pawn&&p31native::Object(pawn)&&p31native::Kind(pawn)==owner)return pawn;
    for(void* host:{controller,pawn}){
        if(!host)continue;
        for(size_t off=0;off<0x1400;off+=8){
            void* p=nullptr;if(!P19Read(host,off,p)||!p||!p31native::Object(p))continue;
            if(p31native::Kind(p)==owner)return p;
        }
    }
    return nullptr;
}
inline void LoadConfig(){
    static bool once=false;if(once)return;once=true;
    const auto path=Config();
    enabled=GetPrivateProfileIntW(L"VR",L"PhysicalBandages",1,path.c_str())!=0;
    auto f=[&](const wchar_t* key,const wchar_t* def,float lo,float hi){
        wchar_t text[32]{};GetPrivateProfileStringW(L"VR",key,def,text,32,path.c_str());
        wchar_t* end=nullptr;float v=std::wcstof(text,&end);if(end==text||!std::isfinite(v))v=std::wcstof(def,nullptr);
        return std::clamp(v,lo,hi);
    };
    grabRadius=f(L"BandageGrabRadius",L"0.20",.10f,.35f);
    wrapTurns=f(L"BandageWrapTurns",L"1.45",.75f,2.5f);
    hipOffset={f(L"BandageHipX",L"0.23",.10f,.45f),f(L"BandageHipY",L"-0.58",-1.0f,-.25f),f(L"BandageHipZ",L"0.045",-0.25f,.25f)};
    Log("P33 PHYSICAL BANDAGES: %s grabRadius=%.2fm wrapTurns=%.2f; native inventory discovery armed.",enabled?"enabled":"disabled",grabRadius,wrapTurns);
}
inline void Discover(void* controller,void* pawn){
    if(scanDone||scanFailed||countBase)return;
    const auto now=GetTickCount64();
    if(!scanArray){
        if(!P19Read(p31native::Base(),0x21e2dd0,scanCount)||scanCount<1||scanCount>500000||
           !P19Read(p31native::Base(),0x21e2dc8,scanArray)||!scanArray){scanFailed=true;return;}
        Log("P33 BANDAGE INVENTORY DISCOVERY: scanning %d UObject entries incrementally.",scanCount);
    }
    p31::ScanBudget budget{now};
    for(int steps=0;scanIndex<scanCount&&steps<512&&budget.Take(GetTickCount64());++scanIndex,++steps){
        void* o=nullptr;if(!P19Read(scanArray,size_t(scanIndex)*8,o)||!o)continue;
        const auto kind=p31native::Kind(o);if(kind!="IntProperty"&&kind!="ByteProperty")continue;
        const auto name=p31native::Name(o);if(!Contains(name,"bandage"))continue;
        int off=-1,size=0;if(!P19Read(o,0x94,off)||!P19Read(o,0x74,size)||off<0||off>0x4000||(size!=1&&size!=4))continue;
        const auto owner=p31native::Name(p31native::Pointer(o,0x48));const int score=Score(name,owner);
        if(score>=60)candidates.push_back({o,name,owner,off,size,score});
    }
    if(scanIndex<scanCount)return;scanDone=true;
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.score>b.score;});
    for(const auto& c:candidates){
        void* base=FindOwnedBase(controller,pawn,c.owner);int value=-1;
        if(base&&ReadCount(base,c.offset,c.size,value)){
            countBase=base;countOffset=c.offset;countSize=c.size;bandages.store(value);
            Log("P33 BANDAGE INVENTORY FOUND: %s owner=%s offset=0x%X size=%d value=%d.",c.name.c_str(),c.owner.c_str(),c.offset,c.size,value);
            return;
        }
    }
    Log("P33 BANDAGE INVENTORY NOT FOUND: %zu named properties matched but none mapped safely to the live player. Physical healing stays disabled rather than guessing inventory memory.",candidates.size());
}
inline P20Tracked Tracking(){
    P20Tracked t;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);t=g_p20Tracked;}return t;
}
inline p20::V Hip(const P20Tracked& t){return t.position[0]+hipOffset;}
inline bool TrackingFresh(const P20Tracked& t,uint64_t now){return t.tick&&now>=t.tick&&now-t.tick<250&&t.handValid[0]&&t.handValid[1];}
inline void Tick(void* controller){
    LoadConfig();if(!enabled)return;
    auto* pawn=g_p20Pawn.load();if(!pawn)return;
    Discover(controller,pawn);
    if(countBase){
        int value=-1;if(ReadCount(countBase,countOffset,countSize,value)){
            const int old=bandages.exchange(value);
            if(old!=value){
                Log("P33 BANDAGE COUNT: %d -> %d%s",old,value,value>old?" (pickup detected)":"");
                if(value<=0){equipped.store(false);std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();wrapProgress.store(0);}
            }
        }
    }
    const auto now=GetTickCount64();auto t=Tracking();poseTick.store(t.tick);
    if(!TrackingFresh(t,now)){equipped.store(false);std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();wrapProgress.store(0);return;}
    if(!equipped.load()){std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();wrapProgress.store(0);return;}
    const int count=bandages.load();if(count<=0){equipped.store(false);return;}
    const auto elbow=p33::EstimatedElbow(t.position[0],t.position[1]);
    bool complete=false;float progress=0;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        complete=wrap.Update(elbow,t.position[1],t.position[2],now,wrapTurns);
        progress=wrap.Progress(wrapTurns);
    }
    wrapProgress.store(progress);
    if(complete){
        equipped.store(false);wrapProgress.store(1.0f);healPulseUntil.store(now+150);
        {std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();}
        Log("P33 PHYSICAL HEAL: wrap gesture complete; sending one native Y/heal press. Game remains authoritative for whether a bandage is consumed.");
    }
}
inline bool InputRightTrigger(float trigger,uint64_t now){
    LoadConfig();if(!enabled)return false;
    const bool down=std::isfinite(trigger)&&trigger>.72f;const bool rising=down&&!triggerDown;triggerDown=down;
    auto t=Tracking();if(!TrackingFresh(t,now))return false;
    if(equipped.load()){
        if(rising&&p33::Near(t.position[2],Hip(t),grabRadius)){
            equipped.store(false);std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();wrapProgress.store(0);
            Log("P33 BANDAGE HOLSTERED: right trigger at hip.");return true;
        }
        return true; // while holding a bandage, do not also lean-right in the flat game's mapping
    }
    if(rising&&bandages.load()>0&&p33::Near(t.position[2],Hip(t),grabRadius)){
        equipped.store(true);std::lock_guard<std::mutex> lock(stateMutex);wrap.Reset();wrapProgress.store(0);
        Log("P33 BANDAGE EQUIPPED: right trigger at hip; %d native bandage(s) available.",bandages.load());return true;
    }
    return false;
}
inline bool HealPulse(uint64_t now){const auto until=healPulseUntil.load();return until&&now<until;}
inline int Count(){return bandages.load();}
inline bool Equipped(){return equipped.load();}
inline float Progress(){return wrapProgress.load();}
inline p20::V HipPosition(const P20Tracked& t){return Hip(t);}
}
