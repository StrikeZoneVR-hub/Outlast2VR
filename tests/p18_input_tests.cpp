#include "../src/dinput8_proxy.cpp"
#include <stdexcept>
static void Check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static XrVector2f stick[2]{};
static bool button[7]{},inactive=false,failSync=false;
static float analog[4]{};
static int pulses=0,stops=0;
static int created=0,attached=0,destroyed=0,spaces=0;static bool rejectBindings=false;
static XrResult XRAPI_PTR CreateSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out){*out=reinterpret_cast<XrActionSet>(1);return XR_SUCCESS;}
static XrResult XRAPI_PTR DestroySet(XrActionSet){++destroyed;return XR_SUCCESS;}
static XrResult XRAPI_PTR CreateAction(XrActionSet,const XrActionCreateInfo* info,XrAction* out){
    if(created==13)Check(info->actionType==XR_ACTION_TYPE_VIBRATION_OUTPUT&&info->countSubactionPaths==2,"haptic subactions");
    if(created==14)Check(info->actionType==XR_ACTION_TYPE_POSE_INPUT&&info->countSubactionPaths==2,"tracked grip pose subactions");
    *out=reinterpret_cast<XrAction>(uintptr_t(++created));return XR_SUCCESS;
}
static XrResult XRAPI_PTR Path(XrInstance,const char* name,XrPath* out){Check(name[0]=='/',"absolute action path");static XrPath next=1;*out=next++;return XR_SUCCESS;}
static XrResult XRAPI_PTR Suggest(XrInstance,const XrInteractionProfileSuggestedBinding* info){Check(info->countSuggestedBindings==17,"Touch, haptic and both grip-pose bindings");return rejectBindings?XR_ERROR_PATH_UNSUPPORTED:XR_SUCCESS;}
static XrResult XRAPI_PTR Attach(XrSession,const XrSessionActionSetsAttachInfo* info){Check(info->countActionSets==1,"one attached set");++attached;return XR_SUCCESS;}
static XrResult XRAPI_PTR CreateSpace(XrSession,const XrActionSpaceCreateInfo* info,XrSpace* out){
    Check(info->action&&info->subactionPath&&info->poseInActionSpace.orientation.w==1,"valid grip action space");
    *out=reinterpret_cast<XrSpace>(uintptr_t(++spaces));return XR_SUCCESS;
}
static XrResult XRAPI_PTR StatePose(XrSession,const XrActionStateGetInfo*,XrActionStatePose* out){out->isActive=XR_TRUE;return XR_SUCCESS;}
static XrResult XRAPI_PTR Sync(XrSession,const XrActionsSyncInfo* info){Check(info->countActiveActionSets==1,"active set");return failSync?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;}
static XrResult XRAPI_PTR Vec(XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* out){out->isActive=!inactive;out->currentState=stick[reinterpret_cast<uintptr_t>(info->action)-1];return XR_SUCCESS;}
static XrResult XRAPI_PTR Bool(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out){out->isActive=!inactive;out->currentState=button[reinterpret_cast<uintptr_t>(info->action)-3];return XR_SUCCESS;}
static XrResult XRAPI_PTR Float(XrSession,const XrActionStateGetInfo* info,XrActionStateFloat* out){out->isActive=!inactive;out->currentState=analog[reinterpret_cast<uintptr_t>(info->action)-10];return XR_SUCCESS;}
static XrResult XRAPI_PTR Apply(XrSession,const XrHapticActionInfo*,const XrHapticBaseHeader* h){auto* v=reinterpret_cast<const XrHapticVibration*>(h);Check(v->amplitude>0&&v->amplitude<=.65f,"bounded rumble");Check(v->duration==60000000,"bounded pulse");++pulses;return XR_SUCCESS;}
static XrResult XRAPI_PTR Stop(XrSession,const XrHapticActionInfo*){++stops;return XR_SUCCESS;}
static XrResult XRAPI_PTR Proc(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define FN(n,f) if(!strcmp(name,n)){*out=reinterpret_cast<PFN_xrVoidFunction>(&f);return XR_SUCCESS;}
    FN("xrCreateActionSet",CreateSet) FN("xrDestroyActionSet",DestroySet) FN("xrCreateAction",CreateAction)
    FN("xrStringToPath",Path) FN("xrSuggestInteractionProfileBindings",Suggest) FN("xrAttachSessionActionSets",Attach)
    FN("xrSyncActions",Sync) FN("xrGetActionStateVector2f",Vec) FN("xrGetActionStateBoolean",Bool)
    FN("xrGetActionStateFloat",Float) FN("xrApplyHapticFeedback",Apply) FN("xrStopHapticFeedback",Stop)
    FN("xrCreateActionSpace",CreateSpace) FN("xrGetActionStatePose",StatePose)
#undef FN
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
static DWORD WINAPI PhysicalGet(DWORD index,XINPUT_STATE* out){
    if(index!=0||!out)return ERROR_DEVICE_NOT_CONNECTED;
    out->dwPacketNumber=41;out->Gamepad.wButtons=XINPUT_GAMEPAD_A;out->Gamepad.sThumbLX=1200;return ERROR_SUCCESS;
}
static DWORD WINAPI PhysicalSet(DWORD index,XINPUT_VIBRATION* value){
    return index==0&&value?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
}
int main()try{
    Check(!kP16ContinuousAimInjectionEnabled,
        "absolute HMD angles must never be injected into frame-local turn/look axes");
    OpenXRQuad init;init.xrGetInstanceProcAddr=Proc;
    Check(!init.P18Init(0,0)&&!init.p18Ready&&!init.p18VrInputEnabled,
        "explicitly disabled VR actions leave the stable gamepad path alone");
    Check(init.p19PoseAction==XR_NULL_HANDLE&&!init.p19PoseBound&&!init.p19Spaces[0]&&!init.p19Spaces[1],
        "disabled VR controls create no controller pose action or spaces");

    OpenXRQuad motion;motion.xrGetInstanceProcAddr=Proc;
    Check(motion.P18Init(1,1)&&motion.p18Ready&&motion.p18VrInputEnabled,
        "MC1 Touch action set attaches");
    Check(created==15&&attached==1&&motion.p19PoseBound&&spaces==2&&motion.p19Spaces[0]&&motion.p19Spaces[1],
        "MC1 creates the grip pose action and both action spaces");
    Check(g_p20Enabled.load()&&!g_p20PhysicalCamcorder.load(),
        "MC6D enables tracked arms while the physical camcorder arm stays disabled");

    // Pure mapping and MC3 gesture composition remain covered independently of
    // runtime focus and the game's native input poll.
    Check(OpenXRQuad::P18Axis(NAN)==0&&OpenXRQuad::P18Axis(2)==32767,"axis validation");
    Check(OpenXRQuad::P18Trigger(NAN)==0&&OpenXRQuad::P18Trigger(2)==255,"trigger validation");
    stick[0]={1,1};auto p=OpenXRQuad::P18Map(stick,button,analog,false);
    Check(p.sThumbLX==0&&p.sThumbLY==0&&(p.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT),"menu mapping helper");
    analog[1]=1.0f;p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(!(p.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),"held right grip is not continuously mapped");
    OpenXRQuad::P18ApplyCamcorderGrip(p,true);
    Check(!(p.wButtons&XINPUT_GAMEPAD_X)&&(p.wButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER),
        "right grip controls the camcorder without mapping left grip to Use");
    button[6]=true;stick[1].y=.8f;p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check((p.wButtons&XINPUT_GAMEPAD_RIGHT_THUMB)&&(p.wButtons&XINPUT_GAMEPAD_DPAD_UP),
        "right thumb click toggles native night vision and stick up maps native zoom in");
    stick[1].y=-.8f;p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(p.wButtons&XINPUT_GAMEPAD_DPAD_DOWN,"right stick down maps native zoom out");
    button[6]=false;stick[1].y=0.0f;
    button[2]=button[3]=true;p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(!OpenXRQuad::P18ApplyQuestChords(p,button),"recordings chord does not trigger microphone");
    Check((p.wButtons&XINPUT_GAMEPAD_BACK)&&!(p.wButtons&(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y)),
        "Quest X+Y chord maps recordings without leaking Use/Reload");
    button[3]=false;p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(!OpenXRQuad::P18ApplyQuestChords(p,button),"X alone does not trigger microphone");
    Check((p.wButtons&XINPUT_GAMEPAD_X)&&!(p.wButtons&XINPUT_GAMEPAD_BACK),
        "Quest X alone keeps native prompted Use action");
    Check(p35runtime::HoldUseInteractionName("PIT_PushObject")&&
          p35runtime::HoldUseInteractionName("PIT_PushObjectInteraction")&&
          p35runtime::HoldUseInteractionName("PIT_InteractiveDoorPull")&&
          p35runtime::HoldUseInteractionName("PIT_ExitLocker")&&
          p35runtime::HoldUseInteractionName("PIT_ExitWardrobe")&&
          !p35runtime::HoldUseInteractionName("PIT_EnterBed")&&
          !p35runtime::HoldUseInteractionName("PIT_PickupObject")&&
          !p35runtime::HoldUseInteractionName("PIT_OpenDoor"),
        "only native push/pull and hiding-peek interactions qualify for X latch; story Sleep remains one-shot");
    Check(p35runtime::RootCellarBedFallbackEligible(true,0,-15.0f,0)&&
          !p35runtime::RootCellarBedFallbackEligible(false,0,-45.0f,0)&&
          !p35runtime::RootCellarBedFallbackEligible(true,1,-45.0f,0)&&
          !p35runtime::RootCellarBedFallbackEligible(true,0,-14.9f,0)&&
          !p35runtime::RootCellarBedFallbackEligible(true,0,-45.0f,3),
        "root-cellar sleep fallback is limited to its live story context, empty list, downward view and ordinary movement");
    motion.p18UseLatched=false;motion.p18PreviousRawX=false;
    motion.p18UseLastAvailable=0;
    XINPUT_GAMEPAD latched{};latched.wButtons=XINPUT_GAMEPAD_X;
    Check(motion.P18ApplyUseLatch(latched,true,false,true,100)&&
          motion.p18UseLatched&&(latched.wButtons&XINPUT_GAMEPAD_X),
        "one X press starts a native hold-style interaction latch");
    latched={};Check(!motion.P18ApplyUseLatch(latched,false,false,true,110)&&
          (latched.wButtons&XINPUT_GAMEPAD_X),
        "releasing physical X keeps virtual X held for left-stick control");
    latched={};latched.wButtons=XINPUT_GAMEPAD_X;
    Check(motion.P18ApplyUseLatch(latched,true,false,true,120)&&
          !motion.p18UseLatched,
        "a second X press requests release of the hold interaction");
    latched={};Check(!motion.P18ApplyUseLatch(latched,false,false,false,130)&&
          !(latched.wButtons&XINPUT_GAMEPAD_X),
        "second X release reaches the native game");
    latched={};latched.wButtons=XINPUT_GAMEPAD_X;
    Check(!motion.P18ApplyUseLatch(latched,true,false,false,200)&&
          !motion.p18UseLatched,
        "pickup and ordinary-door X presses never latch");
    motion.p18PreviousRawX=false;
    Check(p35runtime::p41QuestXEdges.load()==0,
        "Quest X is not diverted into the rejected reflected pickup bridge");
    button[2]=false;button[3]=true;button[6]=true;
    p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(OpenXRQuad::P18ApplyQuestChords(p,button)&&
          !(p.wButtons&(XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_RIGHT_THUMB|XINPUT_GAMEPAD_DPAD_LEFT)),
        "Y plus right-stick-click queues one direct microphone action without duplicate D-pad input");
    const auto bandageEdgesBefore=p35runtime::p41BandageEdges.load();
    OpenXRQuad::P18QueueNativeBandageUse(true);
    Check(p35runtime::p41BandageEdges.load()==bandageEdgesBefore+1,
        "fresh left-grip pulse queues the native bandage action");
    OpenXRQuad::P18QueueNativeBandageUse(false);
    Check(p35runtime::p41BandageEdges.load()==bandageEdgesBefore+1,
        "released left grip does not queue a bandage action");
    Check(OpenXRQuad::P18NativeAnimationHandoffMs(XINPUT_GAMEPAD_X,false,false)==180,
        "Quest X yields only for the native movement-state transition");
    Check(OpenXRQuad::P18NativeAnimationHandoffMs(0,true,false)==180,
        "refused bandage actions cannot hide tracked hands for seconds");
    Check(OpenXRQuad::P18NativeAnimationHandoffMs(0,false,true)==1100&&
          OpenXRQuad::P18NativeAnimationHandoffMs(0,false,false)==0,
        "camcorder handoff is preserved and unrelated controls do not seize arm ownership");
    Check(p35runtime::PickupClassName("OLBandagesPickup")&&
          p35runtime::PickupClassName("OLBatteriesPickup")&&
          p35runtime::PickupClassName("OLPickableDocument")&&
          p35runtime::PickupClassName("OLCollectiblePickup")&&
          p35runtime::PickupClassName("OLGameplayItemPickup"),
        "native pickup bridge covers bandages, batteries, notes and gameplay items");
    Check(!p35runtime::PickupClassName("OLDoor"),
        "non-pickup interactions remain on the stock Use path");
    Check(!p35runtime::PickupClassName("OLBatteriesPickupFactory")&&
          !p35runtime::PickupClassName("OLBandagesPickupFactory"),
        "pickup factories cannot be mistaken for collectible item instances");
    Check(!p35runtime::PickupCompletionDue(1000,1023,0)&&
          p35runtime::PickupCompletionDue(1000,1024,0)&&
          !p35runtime::PickupCompletionDue(1000,1024,1)&&
          !p35runtime::PickupCompletionDue(1000,1301,0),
        "native pickup completion runs once on a later game tick within its bounded window");
    Check(std::fabs(p35runtime::PickupReachDistance({0,0,200})-50.0f)<.01f,
        "floor-item actor pivot height no longer requires crawling");
    Check(std::isinf(p35runtime::PickupReachDistance({0,0,251})),
        "pickup reach cannot cross floors");
    std::array<unsigned char,512> fakeController{};
    p35runtime::interaction.availableInteractionsOffset=128;
    p35runtime::interaction.pickupInteractionValue=7;
    p35runtime::NativeByteArray emptyInteractions{};
    std::memcpy(fakeController.data()+128,&emptyInteractions,sizeof(emptyInteractions));
    p35runtime::TemporaryPickupInteraction temporaryPickup{};
    Check(p35runtime::BeginTemporaryPickupInteraction(fakeController.data(),temporaryPickup),
        "empty native interaction array accepts a synchronous reflected pickup binding");
    p35runtime::NativeByteArray boundInteractions{};
    std::memcpy(&boundInteractions,fakeController.data()+128,sizeof(boundInteractions));
    Check(boundInteractions.data==temporaryPickup.scratch.data()&&boundInteractions.count==1&&
          boundInteractions.capacity==16&&temporaryPickup.scratch[0]==7,
        "temporary pickup binding publishes one validated enum byte in a bounded scratch array");
    p35runtime::EndTemporaryPickupInteraction(temporaryPickup);
    p35runtime::NativeByteArray restoredInteractions{};
    std::memcpy(&restoredInteractions,fakeController.data()+128,sizeof(restoredInteractions));
    Check(!restoredInteractions.data&&restoredInteractions.count==0&&restoredInteractions.capacity==0,
        "temporary pickup binding restores the exact empty engine array header");
    p35runtime::interaction.enterBedInteractionValue=3;
    p35runtime::TemporaryBedInteraction temporaryBed{};
    Check(p35runtime::BeginTemporaryBedInteraction(fakeController.data(),temporaryBed),
        "empty native interaction array accepts one synchronous EnterBed binding");
    std::memcpy(&boundInteractions,fakeController.data()+128,sizeof(boundInteractions));
    Check(boundInteractions.data==temporaryBed.scratch.data()&&boundInteractions.count==1&&
          boundInteractions.capacity==4&&temporaryBed.scratch[0]==3,
        "EnterBed binding publishes only the reflected story enum byte");
    p35runtime::EndTemporaryBedInteraction(temporaryBed);
    std::memcpy(&restoredInteractions,fakeController.data()+128,sizeof(restoredInteractions));
    Check(!restoredInteractions.data&&restoredInteractions.count==0&&restoredInteractions.capacity==0,
        "EnterBed binding restores the exact native array header before returning");
    button[2]=false;
    button[0]=button[1]=true;p=OpenXRQuad::P18Map(stick,button,analog,true);
    OpenXRQuad::P18ApplyQuestChords(p,button);
    Check((p.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)&&!(p.wButtons&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)),
        "Quest A+B chord maps native Look Back");
    button[0]=button[1]=false;button[3]=button[4]=true;
    p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(OpenXRQuad::P18ApplyQuestChords(p,button)&&
          !(p.wButtons&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_Y)),
        "Quest Menu+Y chord queues direct native microphone toggle");
    button[4]=false;button[6]=true;
    p=OpenXRQuad::P18Map(stick,button,analog,true);
    Check(OpenXRQuad::P18ApplyQuestChords(p,button)&&
          !(p.wButtons&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_RIGHT_THUMB)),
        "Quest Y+right-stick-click queues microphone without duplicate gamepad input");
    button[3]=button[6]=false;
    p26::GripGate left,right;
    Check(!left.Update(1.0f,true,100),"held startup left grip is blocked");
    Check(!left.Update(0.0f,true,110),"released left grip arms bandage control");
    Check(left.Update(1.0f,true,120)&&left.Update(1.0f,true,150),"fresh left grip creates one bounded bandage pulse");
    Check(!left.Update(1.0f,true,221),"held left grip cannot retrigger healing");
    Check(!right.Update(1.0f,true,100),"held startup right grip is blocked");
    Check(!right.Update(0.0f,true,110),"released right grip arms camcorder control");
    Check(right.Update(1.0f,true,120)&&right.Update(1.0f,true,150),"fresh right grip creates one bounded camcorder pulse");
    Check(!right.Update(1.0f,true,221),"held right grip cannot retrigger");
    analog[1]=0.0f;

    // The native XInput device is passed through regardless of XR focus or
    // whether a virtual action state exists.
    g_p18Get=&PhysicalGet;g_p18Set=&PhysicalSet;P18Publish({},false);
    XINPUT_STATE out{};Check(P18GetState(0,&out)==ERROR_SUCCESS&&
        (out.Gamepad.wButtons&XINPUT_GAMEPAD_A)&&out.Gamepad.sThumbLX==1200,
        "physical XInput state pass-through");
    XINPUT_VIBRATION rumble{123,456};Check(P18SetState(0,&rumble)==ERROR_SUCCESS,
        "physical XInput rumble pass-through");
    Check(P18GetState(0,nullptr)==ERROR_BAD_ARGUMENTS&&P18SetState(0,nullptr)==ERROR_BAD_ARGUMENTS,
        "invalid arguments");
    Check(P18GetState(3,&out)==ERROR_DEVICE_NOT_CONNECTED,"other slots fallback");
    puts("PASS PB5 X-only interactions, left-grip native bandage, school IK, animation handoff, pickups, Touch/haptic/pose actions and physical XInput pass-through.");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
