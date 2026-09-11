namespace p39runtime {
void* inventory=nullptr;int pickupOffset=-1;
void* scanArray=nullptr;int scanCount=0,scanIndex=0;
uint64_t nextScan=0;size_t drawDistancesDisabled=0;

inline bool DerivedFrom(void* object,const char* baseName){
    void* cls=p31native::Pointer(object,0x58);
    for(int guard=0;cls&&guard<64;++guard,cls=p31native::Pointer(cls,0x80))if(p31native::Name(cls)==baseName)return true;
    return false;
}
inline void ResolveInventory(){
    if(inventory)return;
    void* cls=p31native::Find("OLInventoryManager","Class");
    for(void* f=p31native::Pointer(cls,0x88);f;f=p31native::Pointer(f,0x68))if(p31native::Name(f)=="LastClosestPickup"){
        int size=0;if(P19Read(f,0x94,pickupOffset)&&P19Read(f,0x74,size)&&size==8&&pickupOffset>=0&&pickupOffset<0x1000)break;
        pickupOffset=-1;break;
    }
    int count=0;void* array=nullptr;
    if(pickupOffset<0||!P19Read(p31native::Base(),0x21e2dd0,count)||!P19Read(p31native::Base(),0x21e2dc8,array))return;
    for(int i=0;i<count;++i){void* o=nullptr;if(!P19Read(array,size_t(i)*8,o)||!o)continue;
        if(p31native::Kind(o)=="OLInventoryManager"&&p31native::Name(o).rfind("Default__",0)!=0){inventory=o;Log("P39 NATIVE PICKUP TARGET: OLInventoryManager instance acquired; LastClosestPickup offset=0x%X.",pickupOffset);return;}}
}
inline void DisableDrawDistanceTick(){
    const auto now=GetTickCount64();if(now<nextScan)return;nextScan=now+16;
    void* current=nullptr;int count=0;
    if(!P19Read(p31native::Base(),0x21e2dc8,current)||!P19Read(p31native::Base(),0x21e2dd0,count)||!current||count<1||count>500000)return;
    if(current!=scanArray||count<scanIndex){scanArray=current;scanCount=count;scanIndex=0;}
    scanCount=count;int steps=0;
    for(;scanIndex<scanCount&&steps<128;++scanIndex,++steps){
        void* o=nullptr;if(!P19Read(scanArray,size_t(scanIndex)*8,o)||!o||!DerivedFrom(o,"PrimitiveComponent"))continue;
        float maximum=0,cached=0;if(!P19Read(o,0x15c,maximum)||!P19Read(o,0x160,cached))continue;
        if(((std::isfinite(maximum)&&maximum>0)||(std::isfinite(cached)&&cached>0))&&P20Writable(static_cast<unsigned char*>(o)+0x15c,8)){
            const float unlimited=0;std::memcpy(static_cast<unsigned char*>(o)+0x15c,&unlimited,4);std::memcpy(static_cast<unsigned char*>(o)+0x160,&unlimited,4);++drawDistancesDisabled;
        }
    }
    if(scanIndex==scanCount&&drawDistancesDisabled){static bool logged=false;if(!logged){logged=true;Log("P39 PRIMITIVE DRAW DISTANCE DISABLED: %zu live components changed.",drawDistancesDisabled);}}
}
inline void Tick(){ResolveInventory();DisableDrawDistanceTick();}
inline bool PickupUseAllowed(){
    Tick();if(!inventory||pickupOffset<0)return true;
    void* target=nullptr;if(!P19Read(inventory,size_t(pickupOffset),target)||!target||!p31native::Object(target))return true;
    const auto kind=p31native::Kind(target),name=p31native::Name(target);
    if(kind!="OLCollectiblePickup"&&kind!="OLPickableObject"&&name.find("Pickup")==std::string::npos)return true;
    auto* pawn=g_p20Pawn.load();void* controller=nullptr;if(!pawn||!P19Read(pawn,0x6680,controller)||!controller)return false;
    p20::V item{},eye{};int32_t yawUnits=0;
    if(!P19Read(target,0x88,item)||!P19Read(controller,0x10a0,eye)||!P19Read(controller,0x10b0,yawUnits))return false;
    P20Tracked tracked;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);tracked=g_p20Tracked;}
    if(!tracked.tick||GetTickCount64()-tracked.tick>250||!tracked.handValid[1])return false;
    const float yaw=float(yawUnits)*(6.28318530718f/65536.0f),c=std::cos(yaw),s=std::sin(yaw);
    const auto local=(tracked.position[2]-tracked.position[0])*tracked.units;
    const p20::V hand{eye.x+(-local.z)*c-local.x*s,eye.y+(-local.z)*s+local.x*c,eye.z+local.y};
    const bool withinReach=p20::Length(item-hand)<=32.0f;
    static bool prior=false;if(withinReach&&!prior)Log("P39 PICKUP IN REACH: %s (%s); Quest X may invoke the native pickup.",name.c_str(),kind.c_str());prior=withinReach;
    return withinReach;
}
}
