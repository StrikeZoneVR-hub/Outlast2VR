// P43: keep the proven culling, battery and body-IK paths, but drive pickups
// through Outlast's own interaction state. P41/P42 only assigned
// OLInventoryManager.LastClosestPickup; PerformedUseAction then returned false
// because EPlayerInteractionType::Pickup was absent from the controller's
// AvailableInteractions array. Resolve both pieces from live reflection and
// publish the pickup type into the engine-owned array. No custom pickup prompt
// is rendered: the native HUD is the only source of pickup UI.
namespace p35runtime {

constexpr size_t kActorLocation = 0x88;
constexpr size_t kMaxDrawDistance = 0x15C;
constexpr size_t kCachedMaxDrawDistance = 0x160;
constexpr size_t kAllowCullDistanceVolume = 0x188; // validated, deliberately left unchanged

// P39 V2 real battery bridge.
// Instead of guessing a hard-coded battery offset, discover Outlast 2's own
// reflected FloatProperty on the live Hero/controller/camcorder class. UE3's
// exact property layout is already validated elsewhere in this mod:
// ElementSize +0x74, Offset_Internal +0x94, field next +0x68, class children +0x88.
enum class BatteryOwner : int { None=0, Pawn=1, Controller=2, Camera=3 };
struct BatteryBinding {
    int offset=-1;
    BatteryOwner owner=BatteryOwner::None;
    void* property=nullptr;
    uint64_t lastAttempt=0;
    bool loggedMiss=false;
} batteryBinding;

std::atomic<float> p39BatteryEnergy{0.0f};
std::atomic<bool> p39BatteryValid{false};

std::atomic<void*> p41ClosestPickup{nullptr};
std::atomic<float> p41PickupDistanceCm{-1.0f};
std::atomic<bool> p41PickupNearby{false};
std::atomic<uint64_t> p41PickupTick{0};
std::atomic<uint64_t> p41QuestXEdges{0};
std::atomic<uint64_t> p41BandageEdges{0};
std::atomic<uint64_t> p41MicrophoneEdges{0};
std::atomic<bool> p41HoldUseAvailable{false};
std::atomic<uint64_t> p41HoldUseTick{0};

inline void QuestXPressed(){p41QuestXEdges.fetch_add(1,std::memory_order_release);}
inline void BandagePressed(){p41BandageEdges.fetch_add(1,std::memory_order_release);}
inline void MicrophonePressed(){p41MicrophoneEdges.fetch_add(1,std::memory_order_release);}
inline bool PickupNearby(){
    const auto tick=p41PickupTick.load(std::memory_order_acquire),now=GetTickCount64();
    return p41PickupNearby.load(std::memory_order_acquire)&&
        p41ClosestPickup.load(std::memory_order_acquire)&&tick&&now>=tick&&now-tick<300;
}
inline bool HoldUseAvailable(){
    const auto tick=p41HoldUseTick.load(std::memory_order_acquire),now=GetTickCount64();
    return p41HoldUseAvailable.load(std::memory_order_acquire)&&tick&&now>=tick&&now-tick<500;
}


struct BatteryGlobalScan {
    void* array=nullptr;
    int count=0;
    int index=0;
    int bestScore=0;
    int bestOffset=-1;
    BatteryOwner bestOwner=BatteryOwner::None;
    void* bestProperty=nullptr;
    bool complete=false;
} batteryScan;

inline bool ContainsNoCase(const std::string& value,const char* needle){
    if(!needle||!*needle)return true;
    const std::string n(needle);
    if(n.size()>value.size())return false;
    for(size_t i=0;i+n.size()<=value.size();++i){
        bool same=true;
        for(size_t j=0;j<n.size();++j){
            if(std::tolower(static_cast<unsigned char>(value[i+j]))!=
               std::tolower(static_cast<unsigned char>(n[j]))){
                same=false;break;
            }
        }
        if(same)return true;
    }
    return false;
}
inline bool HoldUseInteractionName(const std::string& name){
    return name=="PIT_PushObject"||name=="PIT_PushObjectInteraction"||
        name=="PIT_InteractiveDoorPush"||name=="PIT_InteractiveDoorPull"||
        name=="PIT_ExitLocker"||
        name=="PIT_ExitWardrobe"||name=="PIT_ExitWardrobeTutorial"||
        name=="PIT_ExitConfessional"||name=="PIT_ExitHidingBarrel"||
        name=="PIT_ExitCoveredHidingBarrel"||name=="PIT_ExitTreeStump";
}

inline int BatteryPropertyScore(const std::string& name){
    if(!ContainsNoCase(name,"battery"))return 0;
    if(ContainsNoCase(name,"duration")||
       ContainsNoCase(name,"initial")||
       ContainsNoCase(name,"max")||
       ContainsNoCase(name,"warning")||
       ContainsNoCase(name,"threshold")||
       ContainsNoCase(name,"num")||
       ContainsNoCase(name,"count"))return 0;

    int score=100;
    if(ContainsNoCase(name,"energy"))score+=500;
    if(ContainsNoCase(name,"current"))score+=250;
    if(ContainsNoCase(name,"level"))score+=200;
    if(name=="BatteryEnergy"||name=="CurrentBatteryEnergy")score+=1000;
    return score;
}

inline bool BatteryCandidateOnInstance(
    void* instance,
    BatteryOwner owner,
    int& bestScore,
    int& bestOffset,
    BatteryOwner& bestOwner,
    void*& bestProperty){

    if(!instance||!p31native::Object(instance))return false;
    void* cls=p31native::Pointer(instance,0x58);
    if(!cls||!p31native::Object(cls)||p31native::Kind(cls)!="Class")return false;

    // Search the live class's reflected fields only. The Outlast battery
    // configuration/state is expected on the concrete Hero/controller/camcorder
    // class. This stays tiny and avoids any repeated global UObject scan.
    void* field=p31native::Pointer(cls,0x88);
    for(int i=0;field&&i<512;++i,field=p31native::Pointer(field,0x68)){
        if(!p31native::Object(field))break;
        const auto kind=p31native::Kind(field);
        if(kind!="FloatProperty")continue;

        const auto name=p31native::Name(field);
        const int score=BatteryPropertyScore(name);
        if(score<=bestScore)continue;

        int elementSize=0,offset=-1;
        if(!P19Read(field,0x74,elementSize)||elementSize!=4||
           !P19Read(field,0x94,offset)||offset<0x40||offset>0x10000)continue;

        float value=0;
        if(!P19Read(instance,size_t(offset),value)||
           !std::isfinite(value)||value<-0.02f||value>1.02f)continue;

        bestScore=score;
        bestOffset=offset;
        bestOwner=owner;
        bestProperty=field;
    }
    return bestScore>0;
}

inline void* BatteryOwnerInstance(void* controller,BatteryOwner owner){
    auto* pawn=g_p20Pawn.load();
    if(owner==BatteryOwner::Pawn)return pawn;
    if(owner==BatteryOwner::Controller)return controller;
    if(owner==BatteryOwner::Camera){
        void* camera=nullptr;
        return pawn&&P25Camera(pawn,camera)?camera:nullptr;
    }
    return nullptr;
}


inline BatteryOwner BatteryOwnerFromOuter(const std::string& outerName){
    if(ContainsNoCase(outerName,"hero")||ContainsNoCase(outerName,"pawn"))return BatteryOwner::Pawn;
    if(ContainsNoCase(outerName,"controller"))return BatteryOwner::Controller;
    if(ContainsNoCase(outerName,"camcorder")||ContainsNoCase(outerName,"camera"))return BatteryOwner::Camera;
    return BatteryOwner::None;
}

inline bool BindBatteryCandidate(
    void* controller,
    int score,
    int offset,
    BatteryOwner owner,
    void* property){

    if(score<=0||offset<0||owner==BatteryOwner::None||!property)return false;
    void* instance=BatteryOwnerInstance(controller,owner);
    float value=0;
    if(!instance||!P19Read(instance,size_t(offset),value)||
       !std::isfinite(value)||value<-0.02f||value>1.02f)return false;

    batteryBinding.offset=offset;
    batteryBinding.owner=owner;
    batteryBinding.property=property;
    batteryBinding.loggedMiss=false;
    p39BatteryEnergy.store(std::clamp(value,0.0f,1.0f),std::memory_order_release);
    p39BatteryValid.store(true,std::memory_order_release);

    const char* ownerName=
        owner==BatteryOwner::Pawn?"Hero":
        owner==BatteryOwner::Controller?"PlayerController":
        owner==BatteryOwner::Camera?"Camcorder":"Unknown";

    Log("P39 V2 REAL BATTERY BOUND: property=%s owner=%s offset=0x%X energy=%.3f score=%d.",
        p31native::Name(property).c_str(),ownerName,offset,
        p39BatteryEnergy.load(std::memory_order_acquire),score);
    return true;
}

inline void GlobalBatteryScanStep(void* controller){
    if(batteryBinding.offset>=0||batteryScan.complete)return;

    void* array=nullptr;
    int count=0;
    if(!P19Read(p31native::Base(),0x21e2dc8,array)||!array||
       !P19Read(p31native::Base(),0x21e2dd0,count)||count<1||count>500000)return;

    if(!batteryScan.array||batteryScan.array!=array||count<batteryScan.count){
        batteryScan={};
        batteryScan.array=array;
        batteryScan.count=count;
    }else if(count>batteryScan.count){
        batteryScan.count=count;
    }

    const auto started=GetTickCount64();
    for(int steps=0;
        batteryScan.index<batteryScan.count&&steps<128&&GetTickCount64()-started<1;
        ++batteryScan.index,++steps){

        void* property=nullptr;
        if(!P19Read(batteryScan.array,size_t(batteryScan.index)*8,property)||!property)continue;
        if(p31native::Kind(property)!="FloatProperty")continue;

        const auto name=p31native::Name(property);
        const int score=BatteryPropertyScore(name);
        if(score<=batteryScan.bestScore)continue;

        void* outer=p31native::Pointer(property,0x48);
        const BatteryOwner owner=BatteryOwnerFromOuter(p31native::Name(outer));
        if(owner==BatteryOwner::None)continue;

        int elementSize=0,offset=-1;
        if(!P19Read(property,0x74,elementSize)||elementSize!=4||
           !P19Read(property,0x94,offset)||offset<0x40||offset>0x10000)continue;

        void* instance=BatteryOwnerInstance(controller,owner);
        float value=0;
        if(!instance||!P19Read(instance,size_t(offset),value)||
           !std::isfinite(value)||value<-0.02f||value>1.02f)continue;

        batteryScan.bestScore=score;
        batteryScan.bestOffset=offset;
        batteryScan.bestOwner=owner;
        batteryScan.bestProperty=property;

        // An exact BatteryEnergy-style name is strong enough to bind immediately.
        if(score>=1000){
            BindBatteryCandidate(controller,score,offset,owner,property);
            batteryScan.complete=true;
            return;
        }
    }

    if(batteryScan.index>=batteryScan.count){
        batteryScan.complete=true;
        if(batteryScan.bestProperty){
            BindBatteryCandidate(
                controller,
                batteryScan.bestScore,
                batteryScan.bestOffset,
                batteryScan.bestOwner,
                batteryScan.bestProperty);
        }else{
            Log("P39 V2 REAL BATTERY: reflected global property scan completed with no safe BatteryEnergy candidate.");
        }
    }
}

inline void DiscoverBattery(void* controller){
    const auto now=GetTickCount64();
    if(batteryBinding.offset>=0||now-batteryBinding.lastAttempt<1000)return;
    batteryBinding.lastAttempt=now;

    int bestScore=0,bestOffset=-1;
    BatteryOwner bestOwner=BatteryOwner::None;
    void* bestProperty=nullptr;

    auto* pawn=g_p20Pawn.load();
    void* camera=nullptr;
    if(pawn)P25Camera(pawn,camera);

    BatteryCandidateOnInstance(pawn,BatteryOwner::Pawn,bestScore,bestOffset,bestOwner,bestProperty);
    BatteryCandidateOnInstance(controller,BatteryOwner::Controller,bestScore,bestOffset,bestOwner,bestProperty);
    BatteryCandidateOnInstance(camera,BatteryOwner::Camera,bestScore,bestOffset,bestOwner,bestProperty);

    if(bestScore>0&&bestOffset>=0&&bestProperty&&
       BindBatteryCandidate(controller,bestScore,bestOffset,bestOwner,bestProperty)){
        return;
    }

    if(!batteryBinding.loggedMiss&&now>5000){
        batteryBinding.loggedMiss=true;
        Log("P39 V2 battery direct-class lookup did not bind yet; starting bounded reflected global fallback.");
    }
}

inline void UpdateBattery(void* controller){
    DiscoverBattery(controller);
    if(batteryBinding.offset<0)GlobalBatteryScanStep(controller);

    if(batteryBinding.offset<0){
        p39BatteryValid.store(false,std::memory_order_release);
        return;
    }

    void* owner=BatteryOwnerInstance(controller,batteryBinding.owner);
    float value=0;
    if(!owner||!P19Read(owner,size_t(batteryBinding.offset),value)||
       !std::isfinite(value)||value<-0.02f||value>1.02f){
        p39BatteryValid.store(false,std::memory_order_release);
        batteryBinding.offset=-1;
        batteryBinding.owner=BatteryOwner::None;
        batteryBinding.property=nullptr;
        return;
    }

    p39BatteryEnergy.store(std::clamp(value,0.0f,1.0f),std::memory_order_release);
    p39BatteryValid.store(true,std::memory_order_release);
}

inline bool BatteryValid(){
    return p39BatteryValid.load(std::memory_order_acquire);
}
inline float BatteryEnergy(){
    return std::clamp(p39BatteryEnergy.load(std::memory_order_acquire),0.0f,1.0f);
}

struct InteractionState {
    void* array=nullptr;
    int count=0;
    int index=0;
    bool complete=false;
    int inventoryManagerOffset=-1;
    int lastClosestPickupOffset=-1;
    int availableInteractionsOffset=-1;
    void* playerInteractionEnum=nullptr;
    int pickupInteractionValue=-1;
    int enterBedInteractionValue=-1;
    std::array<bool,128> holdUseValues{};
    std::array<std::string,128> interactionNames{};
    void *pressedUseButton=nullptr,*pressedUseBandage=nullptr,*performedUseAction=nullptr,*toggleMicrophone=nullptr,*boneLocation=nullptr;
    // PF17 root-cellar backstop. These are all resolved from the live reflected
    // metadata; no function parameter or SequenceEvent property offset is
    // guessed. The normal native interaction remains authoritative whenever it
    // has a target.
    void *usedBy=nullptr,*checkActivate=nullptr;
    int usedByUserOffset=-1,usedByReturnOffset=-1;
    int checkOriginatorOffset=-1,checkInstigatorOffset=-1;
    int checkTestOffset=-1,checkIndicesOffset=-1,checkPushTopOffset=-1,checkReturnOffset=-1;
    int sequenceOriginatorOffset=-1;
    void *sequenceEventClass=nullptr,*seqEventUsedClass=nullptr;
    size_t eventSuperOffset=0;
    std::vector<void*> sleepUsedEvents;
    std::vector<void*> pickups;
    uint64_t handledQuestXEdges=0;
    uint64_t handledBandageEdges=0;
    uint64_t handledMicrophoneEdges=0;
    uint64_t loggedQuestXEdges=0;
    uint64_t lastNotReadyLog=0;
    void* publishedArray=nullptr;
    int publishedIndex=-1;
    unsigned char publishedOldValue=0;
    void* pendingPickup=nullptr;
    uint64_t pendingPickupStarted=0;
    int pendingPickupAttempts=0;
    bool readyLogged=false;
    void* ethanHouseMarker=nullptr;
    void* sleepFamilyMarker=nullptr;
    void* crash04Marker=nullptr;
} interaction;

struct NativeByteArray {
    void* data=nullptr;
    int32_t count=0;
    int32_t capacity=0;
};

inline bool PickupClassName(const std::string& kind){
    // Factory objects create pickup actors but are not collectible instances.
    // Treating them as nearby items can arm LastClosestPickup with an object the
    // game's inventory code can never consume.
    if(ContainsNoCase(kind,"factory"))return false;
    return ContainsNoCase(kind,"collectiblepickup")||
        ContainsNoCase(kind,"bandagespickup")||
        ContainsNoCase(kind,"batteriespickup")||
        ContainsNoCase(kind,"pickabledocument")||
        ContainsNoCase(kind,"gameplayitempickup");
}

inline bool PickupCompletionDue(uint64_t started,uint64_t now,int attempts){
    // PressedUseButton changes native controller state. Completing the action
    // during that same controller tick returns false in Outlast 2, so wait for
    // the following game tick while keeping the original X press authoritative.
    return started&&now>=started&&now-started>=24&&now-started<=300&&attempts==0;
}

inline void ClearPendingPickup(){
    interaction.pendingPickup=nullptr;
    interaction.pendingPickupStarted=0;
    interaction.pendingPickupAttempts=0;
}

inline void ResetInteractionScan(void* array,int count){
    const auto handled=interaction.handledQuestXEdges;
    const auto handledBandage=interaction.handledBandageEdges;
    const auto handledMicrophone=interaction.handledMicrophoneEdges;
    const auto loggedQuestX=interaction.loggedQuestXEdges;
    interaction={};interaction.array=array;interaction.count=count;
    interaction.handledQuestXEdges=handled;
    interaction.handledBandageEdges=handledBandage;
    interaction.handledMicrophoneEdges=handledMicrophone;
    interaction.loggedQuestXEdges=loggedQuestX;
}

inline bool ReadNativeByteArray(void* controller,NativeByteArray& value){
    value={};
    return interaction.availableInteractionsOffset>=0&&
        P19Read(controller,size_t(interaction.availableInteractionsOffset),value)&&
        value.count>=0&&value.capacity>=value.count&&value.capacity<=256&&
        (!value.count||(value.data&&P15Readable(value.data,size_t(value.count))));
}

inline bool OuterChainContains(void* object,const char* fragment){
    void* seen[32]{};int used=0;
    for(int depth=0;object&&depth<32;++depth){
        if(!p31native::Object(object))return false;
        if(ContainsNoCase(p31native::Name(object),fragment))return true;
        for(int i=0;i<used;++i)if(seen[i]==object)return false;
        seen[used++]=object;
        object=p31native::Pointer(object,0x48);
    }
    return false;
}

inline bool InteractionClassChainContains(void* cls,size_t offset,void* target){
    void* seen[32]{};int used=0;
    for(int depth=0;cls&&depth<32;++depth){
        if(cls==target)return true;
        if(!p31native::Object(cls)||p31native::Kind(cls)!="Class")return false;
        for(int i=0;i<used;++i)if(seen[i]==cls)return false;
        seen[used++]=cls;cls=p31native::Pointer(cls,offset);
    }
    return false;
}

inline void ResolveInteractionEventHierarchy(){
    if(interaction.eventSuperOffset||!interaction.sequenceEventClass||!interaction.seqEventUsedClass)return;
    size_t found=0;int matches=0;
    for(size_t offset=0x68;offset<=0xA0;offset+=8){
        if(InteractionClassChainContains(interaction.seqEventUsedClass,offset,interaction.sequenceEventClass)){
            found=offset;++matches;
        }
    }
    if(matches==1){
        interaction.eventSuperOffset=found;
        Log("PF17 BED METADATA: SeqEvent_Used -> SequenceEvent hierarchy validated at superclass offset 0x%zX.",found);
    }
}

inline bool ResolveUsedByLayout(void* fn){
    if(!p31native::Object(fn)||p31native::Kind(fn)!="Function")return false;
    int user=-1,returned=-1,count=0;void* field=p31native::Pointer(fn,0x88);
    for(int i=0;field&&i<128;++i,field=p31native::Pointer(field,0x68)){
        uint64_t flags=0;int offset=-1,size=0;
        if(!P19Read(field,0x78,flags)||!(flags&0x80))continue;
        if(!P19Read(field,0x94,offset)||!P19Read(field,0x74,size)||offset<0||offset>500)return false;
        const auto name=p31native::Name(field),kind=p31native::Kind(field);++count;
        if(name=="User"&&kind=="ObjectProperty"&&size==8)user=offset;
        else if(name=="ReturnValue"&&kind=="BoolProperty"&&size==4)returned=offset;
        else return false;
    }
    if(field||count!=2||user<0||returned<0||user==returned)return false;
    interaction.usedByUserOffset=user;interaction.usedByReturnOffset=returned;return true;
}

inline bool ResolveCheckActivateLayout(void* fn){
    if(!p31native::Object(fn)||p31native::Kind(fn)!="Function")return false;
    int origin=-1,instigator=-1,test=-1,indices=-1,push=-1,returned=-1,count=0;
    void* field=p31native::Pointer(fn,0x88);
    for(int i=0;field&&i<128;++i,field=p31native::Pointer(field,0x68)){
        uint64_t flags=0;int offset=-1,size=0;
        if(!P19Read(field,0x78,flags)||!(flags&0x80))continue;
        if(!P19Read(field,0x94,offset)||!P19Read(field,0x74,size)||offset<0||offset>496)return false;
        const auto name=p31native::Name(field),kind=p31native::Kind(field);++count;
        if(name=="inOriginator"&&kind=="ObjectProperty"&&size==8)origin=offset;
        else if(name=="inInstigator"&&kind=="ObjectProperty"&&size==8)instigator=offset;
        else if(name=="bTest"&&kind=="BoolProperty"&&size==4)test=offset;
        else if(name=="ActivateIndices"&&kind=="ArrayProperty"&&size==16)indices=offset;
        else if(name=="bPushTop"&&kind=="BoolProperty"&&size==4)push=offset;
        else if(name=="ReturnValue"&&kind=="BoolProperty"&&size==4)returned=offset;
        else return false;
    }
    const int values[]={origin,instigator,test,indices,push,returned};
    if(field||count!=6)return false;
    for(int i=0;i<6;++i){if(values[i]<0)return false;for(int j=0;j<i;++j)if(values[i]==values[j])return false;}
    interaction.checkOriginatorOffset=origin;interaction.checkInstigatorOffset=instigator;
    interaction.checkTestOffset=test;interaction.checkIndicesOffset=indices;
    interaction.checkPushTopOffset=push;interaction.checkReturnOffset=returned;
    return true;
}

inline void ResolvePickupInteractionValue(){
    if((interaction.pickupInteractionValue>=0&&interaction.enterBedInteractionValue>=0)||
       !p31native::Object(interaction.playerInteractionEnum))return;
    NativeByteArray names{};
    // UE3 x64 UEnum = UField (Next at 0x68) followed by TArray<FName> at 0x70.
    if(!P19Read(interaction.playerInteractionEnum,0x70,names)||!names.data||
       names.count<1||names.count>128||names.capacity<names.count||names.capacity>256||
       !P15Readable(names.data,size_t(names.count)*8)){
        Log("P43 NATIVE PICKUP: EPlayerInteractionType name array validation failed; no enum value guessed.");
        return;
    }
    int best=-1,bestScore=0;
    std::string list;
    for(int i=0;i<names.count;++i){
        const auto name=P19BoneName(static_cast<unsigned char*>(names.data)+size_t(i)*8);
        if(!list.empty())list+=",";
        list+=std::to_string(i)+"="+name;
        int score=0;
        if(name=="PIT_Pickup"||name=="Pickup")score=1000;
        else if(ContainsNoCase(name,"pickup")&&!ContainsNoCase(name,"max"))score=100;
        if(score>bestScore){best=i;bestScore=score;}
        if(name=="PIT_EnterBed"||name=="EnterBed")interaction.enterBedInteractionValue=i;
        // These are only the native actions that require X to remain held while
        // the left stick pushes/pulls or peeks from a hiding place. EnterBed is
        // deliberately excluded: the root-cellar story prompt uses that enum
        // for the one-shot Sleep action and must receive a normal X press.
        interaction.holdUseValues[static_cast<size_t>(i)]=HoldUseInteractionName(name);
        interaction.interactionNames[static_cast<size_t>(i)]=name;
    }
    Log("P43 EPlayerInteractionType: %s",list.c_str());
    if(best<0){
        Log("P43 NATIVE PICKUP: reflected enum contains no uniquely named pickup value; no interaction write attempted.");
        return;
    }
    interaction.pickupInteractionValue=best;
    Log("P43 NATIVE PICKUP: resolved EPlayerInteractionType[%d] by name.",best);
    if(interaction.enterBedInteractionValue>=0)
        Log("PF16C NATIVE BED: resolved EPlayerInteractionType[%d]=PIT_EnterBed by exact reflected name.",
            interaction.enterBedInteractionValue);
}

inline void UpdateHoldUseAvailability(void* controller){
    NativeByteArray values{};bool available=false;
    std::string live;
    if(ReadNativeByteArray(controller,values))for(int i=0;i<values.count;++i){
        unsigned char value=0;
        if(!P19Read(values.data,size_t(i),value)||value>=interaction.holdUseValues.size())continue;
        if(!live.empty())live+=",";
        const auto& name=interaction.interactionNames[value];
        live+=name.empty()?std::to_string(unsigned(value)):name;
        if(interaction.holdUseValues[value])available=true;
    }
    p41HoldUseAvailable.store(available,std::memory_order_release);
    p41HoldUseTick.store(GetTickCount64(),std::memory_order_release);
    const auto edges=p41QuestXEdges.load(std::memory_order_acquire);
    if(edges!=interaction.loggedQuestXEdges){
        interaction.loggedQuestXEdges=edges;
        Log("PF12 QUEST X INTERACTION STATE: AvailableInteractions=[%s] HmdPitchDeg=%.1f; X remains the native Outlast Use action.",
            live.c_str(),double(g_nativeHeadPitchRadians.load(std::memory_order_relaxed))*57.29577951308232);
    }
}

inline void ObserveInteractionObject(void* object){
    if(!object||!p31native::Object(object))return;
    const auto kind=p31native::Kind(object),name=p31native::Name(object);
    const auto outer=p31native::Name(p31native::Pointer(object,0x48));

    // Crash-04_LD contains the early Genesis Ethan-house cellar sequence. Use
    // live UObject identity rather than a guessed world coordinate to gate the
    // progression fallback. Object() becomes false after unload/GC.
    if(ContainsNoCase(name,"EthansHouse")||ContainsNoCase(outer,"EthansHouse"))
        interaction.ethanHouseMarker=object;
    if(ContainsNoCase(name,"SleepFami")||ContainsNoCase(outer,"SleepFami"))
        interaction.sleepFamilyMarker=object;
    if(ContainsNoCase(name,"Crash-04_LD")||ContainsNoCase(outer,"Crash-04_LD"))
        interaction.crash04Marker=object;
    // Keep all objects belonging to the tiny SleepFamily sequence until class
    // metadata has been discovered. Runtime filtering below accepts only real
    // SeqEvent_Used instances, including an Outlast subclass if one is used.
    if(name.rfind("Default__",0)!=0&&OuterChainContains(object,"SleepFami"))
        interaction.sleepUsedEvents.push_back(object);

    if(kind=="Class"){
        if(name=="SequenceEvent")interaction.sequenceEventClass=object;
        else if(name=="SeqEvent_Used")interaction.seqEventUsedClass=object;
        ResolveInteractionEventHierarchy();
        return;
    }

    if(kind=="ObjectProperty"){
        int elementSize=0,offset=-1;
        if(!P19Read(object,0x74,elementSize)||elementSize!=8||
           !P19Read(object,0x94,offset)||offset<0x40||offset>0x2000)return;
        if(name=="InventoryManager"&&outer=="OLPlayerController")interaction.inventoryManagerOffset=offset;
        else if(name=="LastClosestPickup"&&outer=="OLInventoryManager")interaction.lastClosestPickupOffset=offset;
        else if(name=="Originator"&&outer=="SequenceEvent")interaction.sequenceOriginatorOffset=offset;
        return;
    }
    if(kind=="ArrayProperty"){
        int elementSize=0,offset=-1;
        if(name=="AvailableInteractions"&&outer=="OLPlayerController"&&
           P19Read(object,0x74,elementSize)&&elementSize==16&&
           P19Read(object,0x94,offset)&&offset>=0x40&&offset<0x2000)
            interaction.availableInteractionsOffset=offset;
        return;
    }
    if(kind=="Enum"&&name=="EPlayerInteractionType"){
        if(!interaction.playerInteractionEnum)interaction.playerInteractionEnum=object;
        return;
    }
    if(kind=="Function"){
        if(name=="PressedUseButton"&&outer=="OLPlayerController"&&p31native::Parameters(object,{}))
            interaction.pressedUseButton=object;
        else if(name=="PressedUseBandage"&&outer=="OLPlayerController"&&p31native::Parameters(object,{}))
            interaction.pressedUseBandage=object;
        else if(name=="PerformedUseAction"&&outer=="OLPlayerController"&&
                p31native::Parameters(object,{{"ReturnValue",0,4}}))
            interaction.performedUseAction=object;
        else if(name=="ToggleMicrophone"&&outer=="OLPlayerController"&&p31native::Parameters(object,{}))
            interaction.toggleMicrophone=object;
        else if(name=="GetBoneLocation"&&outer=="SkeletalMeshComponent"&&
                p31native::Parameters(object,{{"BoneName",0,8},{"Space",8,4},{"ReturnValue",12,12}}))
            interaction.boneLocation=object;
        else if(name=="UsedBy"&&outer=="Actor"&&ResolveUsedByLayout(object))
            interaction.usedBy=object;
        else if(name=="CheckActivate"&&outer=="SequenceEvent"&&ResolveCheckActivateLayout(object))
            interaction.checkActivate=object;
        return;
    }
    if(name.rfind("Default__",0)==0||!PickupClassName(kind))return;
    p20::V location{};
    if(P19Read(object,kActorLocation,location)&&p20::Valid(location))interaction.pickups.push_back(object);
}

inline bool LiveNamedMarker(void* object,const char* fragment){
    if(!object||!p31native::Object(object))return false;
    return ContainsNoCase(p31native::Name(object),fragment)||
        ContainsNoCase(p31native::Name(p31native::Pointer(object,0x48)),fragment);
}

inline bool RootCellarSleepContext(){
    const bool ethan=LiveNamedMarker(interaction.ethanHouseMarker,"EthansHouse");
    const bool sleep=LiveNamedMarker(interaction.sleepFamilyMarker,"SleepFami");
    const bool crash=LiveNamedMarker(interaction.crash04Marker,"Crash-04_LD");
    return sleep&&(ethan||crash);
}

inline bool RootCellarBedFallbackEligible(bool context,int availableCount,float pitchDeg,
                                          unsigned char movementState){
    return context&&availableCount==0&&std::isfinite(pitchDeg)&&pitchDeg<=-15.0f&&
        movementState==0;
}

inline void DiscoverInteraction(){
    void* array=nullptr;int count=0;
    if(!P19Read(p31native::Base(),0x21e2dc8,array)||!array||
       !P19Read(p31native::Base(),0x21e2dd0,count)||count<1||count>500000)return;
    if(array!=interaction.array||count<interaction.count)ResetInteractionScan(array,count);
    else if(count>interaction.count){
        interaction.count=count;
        if(interaction.complete)interaction.complete=false;
    }
    if(interaction.complete)return;
    const auto started=GetTickCount64();
    for(int steps=0;interaction.index<interaction.count&&steps<2048&&GetTickCount64()-started<1;
        ++interaction.index,++steps){
        void* object=nullptr;
        if(P19Read(interaction.array,size_t(interaction.index)*8,object)&&object)ObserveInteractionObject(object);
    }
    if(interaction.index>=interaction.count){
        interaction.complete=true;
        ResolvePickupInteractionValue();
        if(!interaction.readyLogged){
            interaction.readyLogged=true;
            Log("PF4 NATIVE ACTIONS READY: actors=%zu InventoryManager=0x%X LastClosestPickup=0x%X AvailableInteractions=0x%X PickupType=%d PressedUse=%d Bandage=%d PerformedUse=%d handPose=%d.",
                interaction.pickups.size(),interaction.inventoryManagerOffset,interaction.lastClosestPickupOffset,
                interaction.availableInteractionsOffset,interaction.pickupInteractionValue,
                interaction.pressedUseButton?1:0,interaction.pressedUseBandage?1:0,
                interaction.performedUseAction?1:0,interaction.boneLocation?1:0);
        }
    }
}

inline bool LeftHandWorld(p20::V& out){
    out={};void* mesh=g_p20Mesh;
    if(!mesh||!interaction.boneLocation||!P20IsPlayerMesh(mesh))return false;
    P19Skeleton sk;
    if(!P19FindSkeleton(mesh,sk)||(sk.count!=67&&sk.count!=75))return false;
    const int leftHand=P20LeftHandBone();
    if(leftHand<0||leftHand>=sk.count)return false;
    uint64_t boneName=0;
    if(!P19Read(static_cast<unsigned char*>(sk.bones)+leftHand*80,0,boneName))return false;
    p31native::Args args;args.Set(0,boneName);args.Set(8,int32_t{0});
    if(!p31native::Event(mesh,interaction.boneLocation,args))return false;
    out=args.Get<p20::V>(12);return p20::Valid(out);
}

inline float PickupButtonDistanceCm(){
    static const float value=static_cast<float>(std::clamp(static_cast<int>(GetPrivateProfileIntW(
        L"VR",L"PickupButtonDistanceCm",200,(ModuleDir()+L"\\outlast2_vr_p37.ini").c_str())),100,300));
    return value;
}

inline float PickupReachDistance(p20::V delta){
    // Pickup actor pivots can sit well above a floor item while Blake's pawn
    // origin changes height when crawling. Keep horizontal reach strict and
    // down-weight that pivot-height difference so standing and crawling select
    // the same nearby item. The vertical cap prevents cross-floor selection.
    if(!p20::Valid(delta)||std::fabs(delta.z)>250.0f)return std::numeric_limits<float>::infinity();
    delta.z*=0.25f;
    return p20::Length(delta);
}

inline void PublishClosestPickup(void* best,float distance){
    const bool nearby=best!=nullptr;
    void* old=p41ClosestPickup.exchange(best,std::memory_order_acq_rel);
    const bool oldNearby=p41PickupNearby.exchange(nearby,std::memory_order_acq_rel);
    p41PickupDistanceCm.store(nearby?distance:-1.0f,std::memory_order_release);
    p41PickupTick.store(GetTickCount64(),std::memory_order_release);
    if(old!=best||oldNearby!=nearby){
        if(nearby)Log("P43 PICKUP TARGET: %s class=%s playerDistance=%.1fcm; publishing to native interaction state.",
            p31native::Name(best).c_str(),p31native::Kind(best).c_str(),distance);
        else Log("P43 PICKUP TARGET: no live item within %.0fcm.",PickupButtonDistanceCm());
    }
}

inline bool NativeArrayContainsPickup(const NativeByteArray& values){
    if(interaction.pickupInteractionValue<0||interaction.pickupInteractionValue>255)return false;
    for(int i=0;i<values.count;++i){
        unsigned char value=0;
        if(!P19Read(values.data,size_t(i),value))return false;
        if(value==static_cast<unsigned char>(interaction.pickupInteractionValue))return true;
    }
    return false;
}

inline void ClearPublishedInteractionTracking(){
    interaction.publishedArray=nullptr;
    interaction.publishedIndex=-1;
    interaction.publishedOldValue=0;
}

inline void RemovePublishedNativeInteraction(void* controller){
    if(!interaction.publishedArray||interaction.publishedIndex<0){ClearPublishedInteractionTracking();return;}
    NativeByteArray values{};
    const int index=interaction.publishedIndex;
    unsigned char current=0;
    if(ReadNativeByteArray(controller,values)&&values.data==interaction.publishedArray&&
       index<values.count&&P19Read(values.data,size_t(index),current)&&
       current==static_cast<unsigned char>(interaction.pickupInteractionValue)&&
       P20Writable(values.data,size_t(values.count))&&
       P20Writable(static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset+8,4)){
        auto* bytes=static_cast<unsigned char*>(values.data);
        if(index+1<values.count)std::memmove(bytes+index,bytes+index+1,size_t(values.count-index-1));
        else bytes[index]=interaction.publishedOldValue;
        const int32_t newCount=values.count-1;
        std::memcpy(static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset+8,&newCount,4);
        Log("P43 NATIVE PICKUP HUD: removed injected pickup interaction at index=%d.",index);
    }
    ClearPublishedInteractionTracking();
}

inline bool PublishNativePickupInteraction(void* controller,bool nearby){
    if(!nearby){RemovePublishedNativeInteraction(controller);return false;}
    ResolvePickupInteractionValue();
    if(interaction.pickupInteractionValue<0||interaction.availableInteractionsOffset<0)return false;
    NativeByteArray values{};
    if(!ReadNativeByteArray(controller,values))return false;

    if(interaction.publishedArray){
        unsigned char current=0;
        if(values.data==interaction.publishedArray&&interaction.publishedIndex>=0&&
           interaction.publishedIndex<values.count&&
           P19Read(values.data,size_t(interaction.publishedIndex),current)&&
           current==static_cast<unsigned char>(interaction.pickupInteractionValue))return true;
        ClearPublishedInteractionTracking();
    }
    if(NativeArrayContainsPickup(values))return true;
    if(!values.data||values.count>=values.capacity||
       !P20Writable(static_cast<unsigned char*>(values.data)+values.count,1)||
       !P20Writable(static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset+8,4))return false;

    unsigned char oldValue=0;
    P19Read(values.data,size_t(values.count),oldValue);
    const unsigned char pickupValue=static_cast<unsigned char>(interaction.pickupInteractionValue);
    std::memcpy(static_cast<unsigned char*>(values.data)+values.count,&pickupValue,1);
    const int32_t newCount=values.count+1;
    std::memcpy(static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset+8,&newCount,4);
    interaction.publishedArray=values.data;
    interaction.publishedIndex=values.count;
    interaction.publishedOldValue=oldValue;
    Log("P43 NATIVE PICKUP HUD: appended reflected pickup interaction index=%d count=%d/%d.",
        values.count,newCount,values.capacity);
    return true;
}

inline void UpdateClosestPickup(){
    if(!interaction.complete){PublishClosestPickup(nullptr,-1);return;}
    void* pawn=g_p20Pawn.load();p20::V player{};
    if(!pawn||!p31native::Object(pawn)||!P19Read(pawn,kActorLocation,player)||!p20::Valid(player)){
        PublishClosestPickup(nullptr,-1);return;}
    const float limit=PickupButtonDistanceCm();
    void* best=nullptr;float bestDistance=limit+0.001f;
    for(auto* object:interaction.pickups){
        if(!object||!p31native::Object(object))continue;
        p20::V location{};
        if(!P19Read(object,kActorLocation,location)||!p20::Valid(location))continue;
        const float distance=PickupReachDistance(location-player);
        if(std::isfinite(distance)&&distance>=0&&distance<bestDistance){best=object;bestDistance=distance;}
    }
    PublishClosestPickup(best,bestDistance);
}

inline bool ArmNativeInventory(void* controller,void* pickup,void*& inventory){
    inventory=nullptr;
    if(interaction.inventoryManagerOffset<0||interaction.lastClosestPickupOffset<0||
       !p31native::Object(controller)||!p31native::Object(pickup)||
       !P19Read(controller,size_t(interaction.inventoryManagerOffset),inventory)||
       !p31native::Object(inventory)||p31native::Kind(inventory)!="OLInventoryManager")return false;
    auto* slot=static_cast<unsigned char*>(inventory)+interaction.lastClosestPickupOffset;
    if(!P20Writable(slot,sizeof(void*)))return false;
    std::memcpy(slot,&pickup,sizeof(pickup));
    void* verify=nullptr;
    return P19Read(inventory,size_t(interaction.lastClosestPickupOffset),verify)&&verify==pickup;
}

// Some levels leave OLPlayerController.AvailableInteractions as an empty
// TArray while the player is standing, even though the pickup actor and
// InventoryManager are valid.  The stock Use handler then rejects X until the
// pawn crouches into its short trace.  Bind a one-byte reflected Pickup entry
// only for the synchronous native ProcessEvent call, and restore the exact
// engine-owned TArray header immediately afterwards.  No DLL memory survives
// the call and no engine allocation is replaced or freed.
struct TemporaryPickupInteraction {
    void* controller=nullptr;
    NativeByteArray original{};
    int replacedIndex=-1;
    unsigned char replacedValue=0;
    std::array<unsigned char,16> scratch{};
    bool headerReplaced=false;
};

inline bool BeginTemporaryPickupInteraction(void* controller,TemporaryPickupInteraction& temporary){
    temporary={};temporary.controller=controller;
    ResolvePickupInteractionValue();
    if(interaction.pickupInteractionValue<0||interaction.pickupInteractionValue>255||
       interaction.availableInteractionsOffset<0||!ReadNativeByteArray(controller,temporary.original))return false;
    temporary.scratch[0]=static_cast<unsigned char>(interaction.pickupInteractionValue);
    if(NativeArrayContainsPickup(temporary.original))return true;

    if(temporary.original.count>0&&temporary.original.data&&
       P20Writable(temporary.original.data,size_t(temporary.original.count))){
        temporary.replacedIndex=temporary.original.count-1;
        if(!P19Read(temporary.original.data,size_t(temporary.replacedIndex),temporary.replacedValue)){
            temporary.replacedIndex=-1;return false;
        }
        std::memcpy(static_cast<unsigned char*>(temporary.original.data)+temporary.replacedIndex,
            temporary.scratch.data(),1);
        return true;
    }

    auto* header=static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset;
    if(!P20Writable(header,sizeof(NativeByteArray)))return false;
    // Capacity is deliberately bounded but leaves room if PressedUseButton
    // appends another interaction during this synchronous call.
    NativeByteArray one{temporary.scratch.data(),1,static_cast<int32_t>(temporary.scratch.size())};
    std::memcpy(header,&one,sizeof(one));
    NativeByteArray verify{};
    if(!ReadNativeByteArray(controller,verify)||verify.data!=temporary.scratch.data()||
       verify.count!=1||verify.capacity!=static_cast<int32_t>(temporary.scratch.size())){
        std::memcpy(header,&temporary.original,sizeof(temporary.original));
        return false;
    }
    temporary.headerReplaced=true;
    return true;
}

inline void EndTemporaryPickupInteraction(TemporaryPickupInteraction& temporary){
    if(!temporary.controller||interaction.availableInteractionsOffset<0)return;
    if(temporary.headerReplaced){
        auto* header=static_cast<unsigned char*>(temporary.controller)+interaction.availableInteractionsOffset;
        if(P20Writable(header,sizeof(NativeByteArray)))
            std::memcpy(header,&temporary.original,sizeof(temporary.original));
    }else if(temporary.replacedIndex>=0&&temporary.original.data&&
             P20Writable(static_cast<unsigned char*>(temporary.original.data)+temporary.replacedIndex,1)){
        std::memcpy(static_cast<unsigned char*>(temporary.original.data)+temporary.replacedIndex,
            &temporary.replacedValue,1);
    }
    temporary={};
}

inline void ConsumeQuestX(void* controller){
    const auto edges=p41QuestXEdges.load(std::memory_order_acquire);
    if(edges==interaction.handledQuestXEdges)return;
    interaction.handledQuestXEdges=edges;
    void* pickup=p41ClosestPickup.load(std::memory_order_acquire),*inventory=nullptr;
    if(!PickupNearby()||!ArmNativeInventory(controller,pickup,inventory)){
        Log("P43 QUEST X: no fresh pickup or native inventory binding; stock X input retained.");return;
    }
    const auto targetName=p31native::Name(pickup);
    const bool published=PublishNativePickupInteraction(controller,true);
    TemporaryPickupInteraction temporary{};
    const bool temporaryBound=!published&&BeginTemporaryPickupInteraction(controller,temporary);
    bool pressed=false;
    if(interaction.pressedUseButton){
        p31native::Args args;
        pressed=p31native::Event(controller,interaction.pressedUseButton,args);
    }
    EndTemporaryPickupInteraction(temporary);
    interaction.pendingPickup=pickup;
    interaction.pendingPickupStarted=GetTickCount64();
    interaction.pendingPickupAttempts=0;
    Log("PF4 QUEST X -> NATIVE OUTLAST USE START: target=%s inventory=%p interactionPublished=%d temporaryBound=%d PressedUseButton=%d; completion queued for next game tick.",
        targetName.c_str(),inventory,published?1:0,temporaryBound?1:0,pressed?1:0);
}

inline void ContinueQuestX(void* controller){
    auto* pickup=interaction.pendingPickup;
    if(!pickup)return;
    const auto now=GetTickCount64();
    if(now<interaction.pendingPickupStarted||now-interaction.pendingPickupStarted>300){
        Log("PF1 QUEST X completion expired for %s; stock X input remained active.",p31native::Name(pickup).c_str());
        ClearPendingPickup();return;
    }
    if(!p31native::Object(pickup)){
        Log("PF1 QUEST X pickup object completed or left the object table before delayed completion.");
        ClearPendingPickup();return;
    }
    if(!PickupCompletionDue(interaction.pendingPickupStarted,now,interaction.pendingPickupAttempts))return;
    ++interaction.pendingPickupAttempts;

    void* inventory=nullptr;
    if(!ArmNativeInventory(controller,pickup,inventory)){
        Log("PF1 QUEST X delayed completion lost native inventory binding for %s.",p31native::Name(pickup).c_str());
        ClearPendingPickup();return;
    }
    const bool published=PublishNativePickupInteraction(controller,true);
    TemporaryPickupInteraction temporary{};
    const bool temporaryBound=!published&&BeginTemporaryPickupInteraction(controller,temporary);
    bool performed=false;
    if(interaction.performedUseAction){
        p31native::Args args;
        if(p31native::Event(controller,interaction.performedUseAction,args))performed=args.Get<int32_t>(0)!=0;
    }
    EndTemporaryPickupInteraction(temporary);
    Log("PF4 QUEST X -> DELAYED NATIVE PICKUP COMPLETE: target=%s inventory=%p interactionPublished=%d temporaryBound=%d PerformedUseAction=%d.",
        p31native::Name(pickup).c_str(),inventory,published?1:0,temporaryBound?1:0,performed?1:0);
    ClearPendingPickup();
}

// Bind EnterBed only for synchronous native ProcessEvent calls. The original
// engine TArray header is restored before this function returns; no DLL-owned
// pointer survives into a later frame.
struct TemporaryBedInteraction {
    void* controller=nullptr;
    NativeByteArray original{};
    std::array<unsigned char,4> scratch{};
    bool headerReplaced=false;
};

inline bool BeginTemporaryBedInteraction(void* controller,TemporaryBedInteraction& temporary){
    temporary={};temporary.controller=controller;
    ResolvePickupInteractionValue();
    if(interaction.enterBedInteractionValue<0||interaction.enterBedInteractionValue>255||
       interaction.availableInteractionsOffset<0||!ReadNativeByteArray(controller,temporary.original)||
       temporary.original.count!=0)return false;
    auto* header=static_cast<unsigned char*>(controller)+interaction.availableInteractionsOffset;
    if(!P20Writable(header,sizeof(NativeByteArray)))return false;
    temporary.scratch[0]=static_cast<unsigned char>(interaction.enterBedInteractionValue);
    NativeByteArray one{temporary.scratch.data(),1,static_cast<int32_t>(temporary.scratch.size())};
    std::memcpy(header,&one,sizeof(one));
    NativeByteArray verify{};
    if(!ReadNativeByteArray(controller,verify)||verify.data!=temporary.scratch.data()||
       verify.count!=1||verify.capacity!=static_cast<int32_t>(temporary.scratch.size())){
        std::memcpy(header,&temporary.original,sizeof(temporary.original));return false;
    }
    temporary.headerReplaced=true;
    return true;
}

inline void EndTemporaryBedInteraction(TemporaryBedInteraction& temporary){
    if(temporary.controller&&temporary.headerReplaced&&interaction.availableInteractionsOffset>=0){
        auto* header=static_cast<unsigned char*>(temporary.controller)+interaction.availableInteractionsOffset;
        if(P20Writable(header,sizeof(NativeByteArray)))
            std::memcpy(header,&temporary.original,sizeof(temporary.original));
    }
    temporary={};
}

inline bool RootCellarBedFallbackEnabled(){
    static const bool value=GetPrivateProfileIntW(L"VR",L"RootCellarBedFallback",1,
        (ModuleDir()+L"\\outlast2_vr_p35.ini").c_str())!=0;
    return value;
}

inline bool TryRootCellarSleepSequence(void* pawn,void*& activatedEvent,void*& activatedOriginator,
                                      float& activatedDistance,bool& usedByCalled,bool& checked){
    activatedEvent=nullptr;activatedOriginator=nullptr;
    activatedDistance=std::numeric_limits<float>::infinity();usedByCalled=false;checked=false;
    if(!p31native::Object(pawn)||interaction.sequenceOriginatorOffset<0||
       interaction.sleepUsedEvents.empty())return false;

    p20::V player{};
    if(!P19Read(pawn,kActorLocation,player)||!p20::Valid(player))return false;
    struct Candidate{void* event;void* originator;float distance;};
    std::vector<Candidate> candidates;
    for(void* event:interaction.sleepUsedEvents){
        if(!p31native::Object(event)||!OuterChainContains(event,"SleepFami"))continue;
        void* eventClass=p31native::Pointer(event,0x58);
        const bool isUsed=p31native::Kind(event)=="SeqEvent_Used"||
            (interaction.eventSuperOffset&&interaction.seqEventUsedClass&&
             InteractionClassChainContains(eventClass,interaction.eventSuperOffset,interaction.seqEventUsedClass));
        if(!isUsed)continue;
        void* originator=nullptr;p20::V location{};
        if(!P19Read(event,size_t(interaction.sequenceOriginatorOffset),originator)||
           !p31native::Object(originator)||!P19Read(originator,kActorLocation,location)||!p20::Valid(location))continue;
        const float distance=p20::Length(location-player);
        // SeqEvent_Used defaults to 128 cm. Allow room for Outlast's custom bed
        // actor pivot while remaining far too narrow to activate another scene.
        if(std::isfinite(distance)&&distance<=450.0f)candidates.push_back({event,originator,distance});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.distance<b.distance;});
    for(const auto& candidate:candidates){
        bool activated=false;
        if(interaction.usedBy&&interaction.usedByUserOffset>=0&&interaction.usedByReturnOffset>=0){
            p31native::Args args;args.Set(size_t(interaction.usedByUserOffset),pawn);
            usedByCalled=p31native::Event(candidate.originator,interaction.usedBy,args);
            if(usedByCalled)activated=args.Get<int32_t>(size_t(interaction.usedByReturnOffset))!=0;
        }
        if(!activated&&interaction.checkActivate&&interaction.checkOriginatorOffset>=0&&
           interaction.checkInstigatorOffset>=0&&interaction.checkPushTopOffset>=0&&
           interaction.checkReturnOffset>=0){
            p31native::Args args;
            args.Set(size_t(interaction.checkOriginatorOffset),candidate.originator);
            args.Set(size_t(interaction.checkInstigatorOffset),pawn);
            args.Set(size_t(interaction.checkTestOffset),int32_t{0});
            // ActivateIndices stays an empty native TArray, meaning all outputs.
            args.Set(size_t(interaction.checkPushTopOffset),int32_t{1});
            checked=p31native::Event(candidate.event,interaction.checkActivate,args);
            if(checked)activated=args.Get<int32_t>(size_t(interaction.checkReturnOffset))!=0;
        }
        if(activated){
            activatedEvent=candidate.event;activatedOriginator=candidate.originator;
            activatedDistance=candidate.distance;return true;
        }
    }
    if(!candidates.empty()){
        activatedEvent=candidates.front().event;activatedOriginator=candidates.front().originator;
        activatedDistance=candidates.front().distance;
    }
    return false;
}

inline void ConsumeRootCellarBed(void* controller){
    const auto edges=p41QuestXEdges.load(std::memory_order_acquire);
    if(edges==interaction.handledQuestXEdges||!interaction.complete)return;
    interaction.handledQuestXEdges=edges;
    if(!RootCellarBedFallbackEnabled())return;
    const bool nativeReady=interaction.enterBedInteractionValue>=0&&interaction.performedUseAction;
    const bool sequenceReady=interaction.sequenceOriginatorOffset>=0&&!interaction.sleepUsedEvents.empty()&&
        (interaction.usedBy||interaction.checkActivate);
    if(!nativeReady&&!sequenceReady)return;

    NativeByteArray available{};
    void* pawn=nullptr;unsigned char movement=255;
    const float pitchDeg=g_nativeHeadPitchRadians.load(std::memory_order_relaxed)*57.29577951308232f;
    const bool stateValid=ReadNativeByteArray(controller,available)&&
        P19Read(controller,0xc38,pawn)&&p31native::Object(pawn)&&P19Read(pawn,0x7f0,movement);
    const bool context=RootCellarSleepContext();
    if(!stateValid||!RootCellarBedFallbackEligible(context,available.count,pitchDeg,movement)){
        if(context)Log("PF16C BED X: native list count=%d pitch=%.1f movement=%u; fallback not eligible and stock X retained.",
            stateValid?available.count:-1,double(pitchDeg),unsigned(movement));
        return;
    }

    TemporaryBedInteraction temporary{};
    const bool bound=nativeReady&&BeginTemporaryBedInteraction(controller,temporary);
    bool performed=false,pressed=false;
    if(bound){
        // The real Quest X has already travelled through Outlast's input path.
        // Complete that native Use action while its missing EnterBed enum is
        // temporarily visible. If it declines, retry the game's own press
        // handler once under the same binding.
        p31native::Args action;
        if(p31native::Event(controller,interaction.performedUseAction,action))
            performed=action.Get<int32_t>(0)!=0;
    }
    EndTemporaryBedInteraction(temporary);
    void* event=nullptr,*originator=nullptr;float distance=std::numeric_limits<float>::infinity();
    bool usedByCalled=false,checked=false;
    const bool sequenceActivated=!performed&&TryRootCellarSleepSequence(
        pawn,event,originator,distance,usedByCalled,checked);
    if(!performed&&!sequenceActivated&&bound&&interaction.pressedUseButton){
        p31native::Args press;pressed=p31native::Event(controller,interaction.pressedUseButton,press);
    }
    Log("PF17 ROOT-CELLAR SLEEP: context=1 enum=%d temporaryBound=%d PerformedUseAction=%d sequenceEvent=%s originator=%s distance=%.1fcm UsedByCalled=%d CheckActivateCalled=%d SequenceActivated=%d PressedUseFallback=%d; no camera/movement/rotation writes.",
        interaction.enterBedInteractionValue,bound?1:0,performed?1:0,
        event?p31native::Name(event).c_str():"none",originator?p31native::Name(originator).c_str():"none",
        std::isfinite(distance)?double(distance):-1.0,usedByCalled?1:0,checked?1:0,
        sequenceActivated?1:0,pressed?1:0);
}

inline void ConsumeBandage(void* controller){
    const auto edges=p41BandageEdges.load(std::memory_order_acquire);
    if(edges==interaction.handledBandageEdges)return;
    if(!interaction.pressedUseBandage){
        if(interaction.complete){
            interaction.handledBandageEdges=edges;
            Log("PB5 LEFT GRIP BANDAGE unavailable: reflected OLPlayerController.PressedUseBandage was not validated.");
        }
        return;
    }
    interaction.handledBandageEdges=edges;
    p31native::Args args;
    const bool called=p31native::Event(controller,interaction.pressedUseBandage,args);
    Log("PB6 LEFT GRIP -> NATIVE BANDAGE: ProcessEvent=%d; one call for this squeeze, Outlast owns inventory checks and healing.",called?1:0);
}

inline void ConsumeMicrophone(void* controller){
    const auto edges=p41MicrophoneEdges.load(std::memory_order_acquire);
    if(edges==interaction.handledMicrophoneEdges)return;
    if(!interaction.toggleMicrophone){
        if(interaction.complete){
            interaction.handledMicrophoneEdges=edges;
            Log("PF6 MICROPHONE unavailable: reflected OLPlayerController.ToggleMicrophone was not validated.");
        }
        return;
    }
    interaction.handledMicrophoneEdges=edges;
    p31native::Args args;
    const bool called=p31native::Event(controller,interaction.toggleMicrophone,args);
    Log("PF6 MICROPHONE -> native ToggleMicrophone ProcessEvent=%d.",called?1:0);
}

struct PrimitiveRef {
    void* object=nullptr;
    int slot=-1;
};

struct CullState {
    void* array=nullptr;
    int count=0;
    int index=0;
    int phase=0; // 0=find validating classes, 1=initial patch, 2=disabled, 3=maintenance
    void *primitiveClass=nullptr,*skeletalClass=nullptr,*staticClass=nullptr,*textureClass=nullptr;
    size_t superOffset=0;
    std::vector<void*> slots;
    std::vector<PrimitiveRef> primitives;
    std::vector<int> primitiveBySlot;
    int watchIndex=0;
    size_t refreshIndex=0;
    uint64_t patched=0,scanned=0,repatched=0,slotChanges=0,refreshPasses=0,watchPasses=0,lastLog=0;
} cull;

inline bool CullFixEnabled(){
    static const bool value=GetPrivateProfileIntW(
        L"VR",L"PrimitiveDrawDistanceFix",0,(ModuleDir()+L"\\outlast2_vr_p35.ini").c_str())!=0;
    return value;
}

inline bool ClassChainContains(void* cls,size_t offset,void* target){
    void* seen[32]{};int used=0;
    for(int depth=0;cls&&depth<32;++depth){
        if(cls==target)return true;
        if(!p31native::Object(cls)||p31native::Kind(cls)!="Class")return false;
        for(int i=0;i<used;++i)if(seen[i]==cls)return false;
        seen[used++]=cls;
        cls=p31native::Pointer(cls,offset);
    }
    return false;
}
inline bool ResolveHierarchy(){
    if(!cull.primitiveClass||!cull.skeletalClass||!cull.staticClass||!cull.textureClass)return false;
    size_t found=0;int matches=0;
    for(size_t off=0x68;off<=0xA0;off+=8){
        const bool sk=ClassChainContains(cull.skeletalClass,off,cull.primitiveClass);
        const bool st=ClassChainContains(cull.staticClass,off,cull.primitiveClass);
        const bool no=ClassChainContains(cull.textureClass,off,cull.primitiveClass);
        if(sk&&st&&!no){found=off;++matches;}
    }
    if(matches!=1){
        Log("P36 PrimitiveComponent hierarchy validation failed: %d candidate superclass offsets; draw-distance writes disabled.",matches);
        return false;
    }
    cull.superOffset=found;
    Log("P36 CULLING/LOD ONLY: PrimitiveComponent hierarchy validated: superclass=0x%zX MaxDrawDistance=0x15C Cached=0x160; flag 0x188 untouched.",found);
    return true;
}
inline bool IsPrimitiveInstance(void* object){
    if(!object||!p31native::Object(object)||!cull.superOffset||!cull.primitiveClass)return false;
    auto* cls=p31native::Pointer(object,0x58);
    return ClassChainContains(cls,cull.superOffset,cull.primitiveClass);
}
inline bool ZeroDrawDistanceFields(void* object){
    if(!object||!P20Writable(static_cast<unsigned char*>(object)+kMaxDrawDistance,8))return false;
    float maxDistance=0,cached=0;
    if(!P19Read(object,kMaxDrawDistance,maxDistance)||!P19Read(object,kCachedMaxDrawDistance,cached)||
       !std::isfinite(maxDistance)||!std::isfinite(cached)||maxDistance<0||cached<0||
       maxDistance>1.0e9f||cached>1.0e9f)return false;
    if(maxDistance==0.0f&&cached==0.0f)return false;
    const float zero=0.0f;
    std::memcpy(static_cast<unsigned char*>(object)+kMaxDrawDistance,&zero,4);
    std::memcpy(static_cast<unsigned char*>(object)+kCachedMaxDrawDistance,&zero,4);
    return true;
}
inline bool TrackAndZeroPrimitive(void* object,int slot){
    if(slot<0||slot>=cull.count||!IsPrimitiveInstance(object))return false;
    int& trackedIndex=cull.primitiveBySlot[static_cast<size_t>(slot)];
    if(trackedIndex<0){
        trackedIndex=static_cast<int>(cull.primitives.size());
        cull.primitives.push_back({object,slot});
    }else cull.primitives[static_cast<size_t>(trackedIndex)]={object,slot};
    return ZeroDrawDistanceFields(object);
}
inline void ClearTrackedPrimitive(int slot){
    if(slot<0||slot>=static_cast<int>(cull.primitiveBySlot.size()))return;
    const int trackedIndex=cull.primitiveBySlot[static_cast<size_t>(slot)];
    if(trackedIndex>=0&&trackedIndex<static_cast<int>(cull.primitives.size()))
        cull.primitives[static_cast<size_t>(trackedIndex)].object=nullptr;
}
inline void ResetCull(void* array,int count){
    cull.array=array;cull.count=count;cull.index=0;cull.phase=0;
    cull.primitiveClass=cull.skeletalClass=cull.staticClass=cull.textureClass=nullptr;
    cull.superOffset=0;
    cull.slots.assign(static_cast<size_t>(count),nullptr);
    cull.primitiveBySlot.assign(static_cast<size_t>(count),-1);
    cull.primitives.clear();cull.watchIndex=0;cull.refreshIndex=0;
    cull.patched=cull.scanned=cull.repatched=cull.slotChanges=0;
    cull.refreshPasses=cull.watchPasses=0;cull.lastLog=0;
}
inline void MaintainDrawDistances(){
    // The UObject table is one contiguous allocation. Validate it once per
    // maintenance tick, then read stable pointer slots directly. PF10 called
    // VirtualQuery for every unchanged slot, stretching a full churn scan to
    // tens of seconds in later levels and letting newly streamed objects pop.
    if(cull.count<1||!P15Readable(cull.array,static_cast<size_t>(cull.count)*sizeof(void*)))return;
    auto* slotObjects=static_cast<void**>(cull.array);

    // First revisit components already proven to be PrimitiveComponents. This
    // catches CachedMaxDrawDistance being recalculated by level/volume updates.
    const auto refreshStarted=GetTickCount64();
    for(int steps=0;!cull.primitives.empty()&&steps<192&&GetTickCount64()-refreshStarted<1;++steps){
        if(cull.refreshIndex>=cull.primitives.size()){
            cull.refreshIndex=0;++cull.refreshPasses;
        }
        const auto ref=cull.primitives[cull.refreshIndex++];
        if(ref.slot<0||ref.slot>=cull.count)continue;
        void* current=slotObjects[ref.slot];
        if(!ref.object||current!=ref.object||!IsPrimitiveInstance(current))continue;
        if(ZeroDrawDistanceFields(current)){++cull.patched;++cull.repatched;}
    }

    // Also watch the UObject slots themselves. UE3 can reuse a stable table
    // slot while streaming levels, so array growth alone is not sufficient.
    const auto watchStarted=GetTickCount64();
    for(int steps=0;cull.count>0&&steps<4096&&GetTickCount64()-watchStarted<1;++steps){
        if(cull.watchIndex>=cull.count){cull.watchIndex=0;++cull.watchPasses;}
        const int slot=cull.watchIndex++;
        void* object=slotObjects[slot];
        if(cull.slots[static_cast<size_t>(slot)]==object)continue;
        ClearTrackedPrimitive(slot);
        cull.slots[static_cast<size_t>(slot)]=object;++cull.slotChanges;
        if(object&&TrackAndZeroPrimitive(object,slot))++cull.patched;
    }
}
inline void TickCull(){
    if(!CullFixEnabled())return;
    void* array=nullptr;int count=0;
    if(!P19Read(p31native::Base(),0x21e2dc8,array)||!array||
       !P19Read(p31native::Base(),0x21e2dd0,count)||count<1||count>500000)return;
    if(array!=cull.array||count<cull.count)ResetCull(array,count);
    else if(count>cull.count){
        const int oldCount=cull.count;cull.count=count;
        cull.slots.resize(static_cast<size_t>(count),nullptr);
        cull.primitiveBySlot.resize(static_cast<size_t>(count),-1);
        if(cull.phase==3)cull.watchIndex=oldCount; // prioritize freshly appended streaming objects
    }
    if(cull.phase==2)return;

    // P36 keeps the culling fix but caps discovery/patch work aggressively so
    // Quest Link does not pay the old ~2 ms scan budget every frame.
    const auto started=GetTickCount64();
    if(cull.phase==0){
        for(int steps=0;cull.index<cull.count&&steps<256&&GetTickCount64()-started<1;++cull.index,++steps){
            void* object=nullptr;
            if(!P19Read(cull.array,size_t(cull.index)*8,object)||!object||p31native::Kind(object)!="Class")continue;
            const auto name=p31native::Name(object);const auto outer=p31native::Name(p31native::Pointer(object,0x48));
            if(outer!="Engine")continue;
            if(name=="PrimitiveComponent")cull.primitiveClass=object;
            else if(name=="SkeletalMeshComponent")cull.skeletalClass=object;
            else if(name=="StaticMeshComponent")cull.staticClass=object;
            else if(name=="TextureRenderTarget2D")cull.textureClass=object;
            if(cull.primitiveClass&&cull.skeletalClass&&cull.staticClass&&cull.textureClass){
                if(!ResolveHierarchy()){cull.phase=2;return;}
                cull.phase=1;cull.index=0;break;
            }
        }
        if(cull.phase==0){
            if(cull.index<cull.count)return;
            Log("P36 PrimitiveComponent validation classes not found; draw-distance writes disabled.");
            cull.phase=2;return;
        }
    }

    const auto patchStarted=GetTickCount64();
    for(int steps=0;cull.phase==1&&cull.index<cull.count&&steps<256&&GetTickCount64()-patchStarted<1;++cull.index,++steps){
        void* object=nullptr;
        if(!P19Read(cull.array,size_t(cull.index)*8,object))continue;
        cull.slots[static_cast<size_t>(cull.index)]=object;++cull.scanned;
        if(object&&TrackAndZeroPrimitive(object,cull.index))++cull.patched;
    }
    if(cull.phase==1&&cull.index>=cull.count){
        cull.phase=3;cull.watchIndex=0;cull.refreshIndex=0;
        Log("PF10 PERSISTENT CULLING: initial scan complete; tracked=%zu patched=%llu. Rolling streamed-slot and cached-distance maintenance active.",
            cull.primitives.size(),static_cast<unsigned long long>(cull.patched));
    }
    // Refresh already discovered primitives while the initial scan is still
    // running; streamed levels can restore cached distances before it ends.
    if(cull.phase==1||cull.phase==3)MaintainDrawDistances();
    const auto now=GetTickCount64();
    if((cull.phase==1||cull.phase==3)&&now-cull.lastLog>5000){
        cull.lastLog=now;
        Log("PF10 PERSISTENT CULLING: phase=%d initial=%d/%d tracked=%zu patched=%llu reapplied=%llu slotChanges=%llu refreshPasses=%llu watchPasses=%llu.",
            cull.phase,cull.index,cull.count,cull.primitives.size(),
            static_cast<unsigned long long>(cull.patched),static_cast<unsigned long long>(cull.repatched),
            static_cast<unsigned long long>(cull.slotChanges),static_cast<unsigned long long>(cull.refreshPasses),
            static_cast<unsigned long long>(cull.watchPasses));
    }
}

inline bool PhysicalInteractionsConfigured(){
    static const bool value=GetPrivateProfileIntW(L"VR",L"PhysicalInteractions",1,
        (ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;
    return value;
}
inline bool BodyCollisionConfigured(){
    static const bool value=GetPrivateProfileIntW(L"VR",L"BodyCollision",1,
        (ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;
    return value;
}
inline bool PhysicalInteractionReady(){
    if(!PhysicalInteractionsConfigured())return true;
    const auto tick=g_p41InteractionReachTick.load(std::memory_order_acquire),now=GetTickCount64();
    return tick&&now>=tick&&now-tick<250&&g_p41InteractionReachReady.load(std::memory_order_acquire);
}

struct P41CollisionApi {
    void* array=nullptr;
    int count=0,index=0;
    void* fastTrace=nullptr;
    bool complete=false,failed=false,signatureValid=false,logged=false;
} p41CollisionApi;

inline void P41DiscoverCollisionApi(){
    auto& api=p41CollisionApi;
    if(api.complete||api.failed)return;
    void* array=nullptr;int count=0;
    if(!P19Read(p31native::Base(),0x21e2dc8,array)||!array||
       !P19Read(p31native::Base(),0x21e2dd0,count)||count<1||count>500000)return;
    if(!api.array){api.array=array;api.count=count;Log("MC5 COLLISION API discovery started: native Actor.FastTrace is optional and signature-gated.");}
    if(array!=api.array||count<api.count){api.failed=true;Log("MC5 COLLISION API: UObject table changed during discovery; wall probes disabled safely.");return;}
    api.count=count;
    const auto started=GetTickCount64();
    for(int steps=0;api.index<api.count&&steps<512&&GetTickCount64()-started<1;++api.index,++steps){
        void* object=nullptr;
        if(!P19Read(api.array,size_t(api.index)*8,object)||!object||
           p31native::Name(object)!="FastTrace"||p31native::Kind(object)!="Function"||
           p31native::Name(p31native::Pointer(object,0x48))!="Actor")continue;
        if(api.fastTrace&&api.fastTrace!=object){api.failed=true;Log("MC5 COLLISION API: duplicate Actor.FastTrace metadata; wall probes disabled safely.");return;}
        api.fastTrace=object;
    }
    if(api.index<api.count)return;
    api.complete=true;
    api.signatureValid=api.fastTrace&&p31native::Parameters(api.fastTrace,{
        {"TraceEnd",0,12},{"TraceStart",12,12},{"BoxExtent",24,12},
        {"bTraceBullet",36,4},{"ReturnValue",40,4}});
    Log(api.signatureValid
        ?"MC5 COLLISION API READY: exact Actor.FastTrace parameter layout verified; no game object state will be modified."
        :"MC5 COLLISION API unavailable or signature mismatch; native gameplay remains active and wall probes fail open.");
}

inline bool P41FastTrace(void* pawn,const p20::V& start,const p20::V& end,float extent,bool& clear){
    clear=true;
    if(!p41CollisionApi.signatureValid||!p31native::Object(pawn))return false;
    p31native::Args args;
    args.Set(0,end);args.Set(12,start);args.Set(24,p20::V{extent,extent,extent});
    args.Set(36,int32_t{0});
    if(!p31native::Event(pawn,p41CollisionApi.fastTrace,args))return false;
    clear=args.Get<int32_t>(40)!=0;
    return true;
}

inline p20::V P41TrackingToWorld(const p20::V& local,float units,float yaw){
    const float c=std::cos(yaw),s=std::sin(yaw);
    const auto scaled=local*units;
    return {(-scaled.z)*c-scaled.x*s,(-scaled.z)*s+scaled.x*c,scaled.y};
}

inline void P41PublishPhysicalState(void* controller){
    const auto now=GetTickCount64();
    g_p41BodyCollisionEnabled.store(BodyCollisionConfigured(),std::memory_order_release);
    P20Tracked tracked;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);tracked=g_p20Tracked;}
    const bool fresh=tracked.tick&&now>=tracked.tick&&now-tracked.tick<250;
    const bool physicalInteractions=PhysicalInteractionsConfigured();
    const bool reach=physicalInteractions&&fresh&&p41::ReachGesture(tracked.position[0],tracked.position[1],
        tracked.handValid[0],tracked.units);
    g_p41InteractionReachReady.store(reach,std::memory_order_release);
    g_p41InteractionReachTick.store(now,std::memory_order_release);

    static bool previousReach=false;static uint64_t reachLogs=0;
    if(physicalInteractions&&reach!=previousReach){
        if(++reachLogs<=12||reachLogs%100==0)
            Log("LEGACY LEFT-HAND REACH %s.",reach?"READY":"OUT OF RANGE");
        previousReach=reach;
    }
    if(!physicalInteractions)previousReach=false;

    if(!BodyCollisionConfigured()||!fresh){
        g_p41HeadCollisionFraction.store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[0].store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[1].store(1.0f,std::memory_order_release);
        g_p41CollisionTick.store(now,std::memory_order_release);
        return;
    }
    P41DiscoverCollisionApi();
    if(!p41CollisionApi.signatureValid)return;
    static uint64_t nextProbe=0;
    if(now<nextProbe)return;
    nextProbe=now+33;

    p20::V engineEye{};int32_t yawUnits=0;
    void* pawn=g_p20Pawn.load(std::memory_order_acquire);
    if(!controller||!pawn||!P19Read(controller,0x10a0,engineEye)||
       !P19Read(controller,0x10b0,yawUnits)||!p20::Valid(engineEye))return;
    const float yaw=float(yawUnits)*(6.28318530718f/65536.0f);
    const auto headDelta=P41TrackingToWorld(tracked.position[0],tracked.units,yaw);
    bool traceOk=true;
    auto fraction=[&](const p20::V& start,const p20::V& delta,float extent){
        bool callOk=true;
        const float result=p41::ClearFraction([&](float amount){
            bool clear=true;
            if(!P41FastTrace(pawn,start,start+delta*amount,extent,clear)){callOk=false;return true;}
            return clear;
        });
        if(!callOk)traceOk=false;
        return result;
    };
    const float headMeasured=fraction(engineEye,headDelta,9.0f);
    const auto visualEye=engineEye+headDelta*headMeasured;
    // MC6F retains MC6E: do not ray-test wrists from the eye. Actor.FastTrace can hit the
    // player's own body and return zero, which collapses both tracked targets
    // to the eye and makes the arms appear frozen straight ahead. The headset
    // trace above remains active; hand safety is handled by the near-eye sphere
    // and conservative torso capsule in the arm solver.
    if(!traceOk){
        if(!p41CollisionApi.logged){p41CollisionApi.logged=true;Log("MC5 COLLISION API call rejected at runtime; all collision fractions restored to native fail-open values.");}
        g_p41HeadCollisionFraction.store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[0].store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[1].store(1.0f,std::memory_order_release);
    }else{
        const float oldHead=g_p41HeadCollisionFraction.load(std::memory_order_relaxed);
        g_p41HeadCollisionFraction.store(p41::SmoothedFraction(oldHead,headMeasured),std::memory_order_release);
        g_p41HandCollisionFraction[0].store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[1].store(1.0f,std::memory_order_release);
    }
    g_p41CollisionTick.store(now,std::memory_order_release);
}

inline void Tick(void* controller){
    p46ui::Tick(controller);
    if(!g_p10GameplayActive.load()||!P15Focused()){
        ClearPendingPickup();
        p39BatteryValid.store(false,std::memory_order_release);
        g_p41InteractionReachReady.store(false,std::memory_order_release);
        g_p41InteractionReachTick.store(GetTickCount64(),std::memory_order_release);
        g_p41HeadCollisionFraction.store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[0].store(1.0f,std::memory_order_release);
        g_p41HandCollisionFraction[1].store(1.0f,std::memory_order_release);
        p41HoldUseAvailable.store(false,std::memory_order_release);
        p41HoldUseTick.store(GetTickCount64(),std::memory_order_release);
        return;
    }

    P41PublishPhysicalState(controller);
    // PF6 no longer injects Pickup into AvailableInteractions or calls a partial
    // reflected completion path. That path was rejected by PerformedUseAction
    // and repeatedly fought the engine-owned array. The launcher expands the
    // native OLHero pickup radius/aim cone instead, so stock X remains the one
    // authoritative path for pickups, doors and movable objects.
    DiscoverInteraction();
    ConsumeRootCellarBed(controller);
    UpdateHoldUseAvailability(controller);
    ConsumeBandage(controller);
    ConsumeMicrophone(controller);
    TickCull();
    UpdateBattery(controller);
}

} // namespace p35runtime
