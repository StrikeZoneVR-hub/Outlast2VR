// Members of OpenXRQuad. All XR action/haptic calls stay on the render thread.
PFN_xrCreateActionSet p18CreateSet=nullptr;
PFN_xrDestroyActionSet p18DestroySet=nullptr;
PFN_xrCreateAction p18CreateAction=nullptr;
PFN_xrStringToPath p18Path=nullptr;
PFN_xrSuggestInteractionProfileBindings p18Suggest=nullptr;
PFN_xrAttachSessionActionSets p18Attach=nullptr;
PFN_xrSyncActions p18Sync=nullptr;
PFN_xrGetActionStateBoolean p18Bool=nullptr;
PFN_xrGetActionStateFloat p18Float=nullptr;
PFN_xrGetActionStateVector2f p18Vector=nullptr;
PFN_xrApplyHapticFeedback p18Apply=nullptr;
PFN_xrStopHapticFeedback p18Stop=nullptr;
XrActionSet p18Set=XR_NULL_HANDLE;
XrAction p18Actions[13]{}; // sticks, A/B/X/Y/menu/clicks, grips, triggers
XrAction p18Haptic=XR_NULL_HANDLE;
XrPath p18Hands[2]{};
bool p18Ready=false,p18Armed=false,p18WasActive=false,p18VrInputEnabled=false;
bool p18PreviousBandagePulse=false,p18PreviousMicrophoneChord=false;
WORD p18PreviousButtons=0;
bool p18UseLatched=false,p18PreviousRawX=false;
ULONGLONG p18UseLastAvailable=0;
p26::GripGate p18BandageGrip,p18CamcorderGrip;
ULONGLONG p18HapticTick=0;
uint64_t p18HapticCalls=0,p18HapticErrors=0;

bool P18Init(int forcedVrInput=-1,int forcedTrackedPoses=-1){
    const auto config=ModuleDir()+L"\\outlast2_vr_p32.ini";
    p18VrInputEnabled=forcedVrInput>=0?forcedVrInput!=0:
        GetPrivateProfileIntW(L"VR",L"VrInput",0,config.c_str())!=0;
    p19TrackingEnabled=forcedTrackedPoses>=0?forcedTrackedPoses!=0:
        GetPrivateProfileIntW(L"VR",L"TrackedControllerPoses",1,config.c_str())!=0;
    if(!p18VrInputEnabled){
        p19TrackingEnabled=false;P19ClearTracked();
        Log("P18 VR ACTION CONTROLS DISABLED: physical Xbox/XInput gamepads retained; no OpenXR controller actions or VR button bridge will be published.");
        return false;
    }
#define P18_LOAD(field,name) if(!LoadInstanceProc(name,field))return false
    P18_LOAD(p18CreateSet,"xrCreateActionSet");P18_LOAD(p18DestroySet,"xrDestroyActionSet");
    P18_LOAD(p18CreateAction,"xrCreateAction");P18_LOAD(p18Path,"xrStringToPath");
    P18_LOAD(p18Suggest,"xrSuggestInteractionProfileBindings");P18_LOAD(p18Attach,"xrAttachSessionActionSets");
    P18_LOAD(p18Sync,"xrSyncActions");P18_LOAD(p18Bool,"xrGetActionStateBoolean");
    P18_LOAD(p18Float,"xrGetActionStateFloat");P18_LOAD(p18Vector,"xrGetActionStateVector2f");
    P18_LOAD(p18Apply,"xrApplyHapticFeedback");P18_LOAD(p18Stop,"xrStopHapticFeedback");
#undef P18_LOAD
    XrActionSetCreateInfo ci{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(ci.actionSetName,"quest_controls");strcpy_s(ci.localizedActionSetName,"Outlast Quest Controls");
    if(XR_FAILED(p18CreateSet(instance,&ci,&p18Set)))return false;
    auto fail=[&](){p18DestroySet(p18Set);p18Set=XR_NULL_HANDLE;return false;};
    const char* names[]={"move","turn","jump","crouch","use","reload","menu","run","nightvision","lookback","camera","leanleft","leanright"};
    const char* paths[]={"/user/hand/left/input/thumbstick","/user/hand/right/input/thumbstick",
        "/user/hand/right/input/a/click","/user/hand/right/input/b/click","/user/hand/left/input/x/click","/user/hand/left/input/y/click",
        "/user/hand/left/input/menu/click","/user/hand/left/input/thumbstick/click","/user/hand/right/input/thumbstick/click",
        "/user/hand/left/input/squeeze/value","/user/hand/right/input/squeeze/value","/user/hand/left/input/trigger/value","/user/hand/right/input/trigger/value"};
    std::vector<XrActionSuggestedBinding> bindings;
    for(int i=0;i<13;++i){
        XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(a.actionName,names[i]);strcpy_s(a.localizedActionName,names[i]);
        a.actionType=i<2?XR_ACTION_TYPE_VECTOR2F_INPUT:(i<9?XR_ACTION_TYPE_BOOLEAN_INPUT:XR_ACTION_TYPE_FLOAT_INPUT);
        XrPath path=0;
        if(XR_FAILED(p18CreateAction(p18Set,&a,&p18Actions[i]))||XR_FAILED(p18Path(instance,paths[i],&path)))return fail();
        bindings.push_back({p18Actions[i],path});
    }
    if(XR_FAILED(p18Path(instance,"/user/hand/left",&p18Hands[0]))||XR_FAILED(p18Path(instance,"/user/hand/right",&p18Hands[1])))return fail();
    XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(a.actionName,"rumble");strcpy_s(a.localizedActionName,"Game rumble");
    a.actionType=XR_ACTION_TYPE_VIBRATION_OUTPUT;a.countSubactionPaths=2;a.subactionPaths=p18Hands;
    if(XR_FAILED(p18CreateAction(p18Set,&a,&p18Haptic)))return fail();
    for(int i=0;i<2;++i){XrPath p=0;if(XR_FAILED(p18Path(instance,i?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic",&p)))return fail();bindings.push_back({p18Haptic,p});}
    P19AddPoseBindings(bindings);
    XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    if(XR_FAILED(p18Path(instance,"/interaction_profiles/oculus/touch_controller",&suggest.interactionProfile)))return fail();
    suggest.countSuggestedBindings=static_cast<uint32_t>(bindings.size());suggest.suggestedBindings=bindings.data();
    if(XR_FAILED(p18Suggest(instance,&suggest)))return fail();
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&p18Set;
    if(XR_FAILED(p18Attach(session,&attach)))return fail();
    P19CreateSpaces();
    p18Ready=true;Log("PF6 QUEST ACTIONS READY: tracked arms=%s nativeQuestX=1 leftGripBandage=1 directMicrophone=1 nativePickupReach=1 transitionHandoff=1 physicalCamcorder=0 handWallTrace=0 bodyAnchor=1.",p19PoseBound?"ready":"unavailable");return true;
}
void P18Clear(){
    p18BandageGrip.Reset();p18CamcorderGrip.Reset();p18PreviousBandagePulse=false;p18PreviousMicrophoneChord=false;
    P19ClearTracked();
    g_p37CameraActive.store(false,std::memory_order_relaxed);
    g_p41CamcorderRaised.store(false,std::memory_order_release);
    g_p41CamcorderProjectionBaselineValid.store(false,std::memory_order_release);
    P18Publish({},false);p18Armed=false;p18PreviousButtons=0;
    p18UseLatched=false;p18PreviousRawX=false;p18UseLastAvailable=0;
    if(p18Ready&&p18WasActive)for(int i=0;i<2;++i){XrHapticActionInfo h{XR_TYPE_HAPTIC_ACTION_INFO};h.action=p18Haptic;h.subactionPath=p18Hands[i];p18Stop(session,&h);}
    p18WasActive=false;
}
static SHORT P18Axis(float value){return std::isfinite(value)?static_cast<SHORT>(std::clamp(value,-1.0f,1.0f)*32767):0;}
static BYTE P18Trigger(float value){return std::isfinite(value)?static_cast<BYTE>(std::clamp(value,0.0f,1.0f)*255):0;}
static XINPUT_GAMEPAD P18Map(const XrVector2f sticks[2],const bool b[7],const float f[4],bool gameplay){
    XINPUT_GAMEPAD pad{};pad.sThumbLX=P18Axis(sticks[0].x);pad.sThumbLY=P18Axis(sticks[0].y);pad.sThumbRX=P18Axis(sticks[1].x);
    const WORD bits[]={XINPUT_GAMEPAD_A,XINPUT_GAMEPAD_B,XINPUT_GAMEPAD_X,XINPUT_GAMEPAD_Y,XINPUT_GAMEPAD_START,XINPUT_GAMEPAD_LEFT_THUMB,XINPUT_GAMEPAD_RIGHT_THUMB};
    for(int i=0;i<7;++i)if(b[i])pad.wButtons|=bits[i];
    // MC3 grip gestures are added as short, edge-gated native button presses in
    // P18Update. Do not map grip pressure continuously here: a held grip must
    // never repeat Use or toggle the camcorder more than once.
    pad.bLeftTrigger=P18Trigger(f[2]);pad.bRightTrigger=P18Trigger(f[3]);
    if(sticks[1].y>.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_UP;
    if(sticks[1].y<-.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_DOWN;
    if(!gameplay){ // Explicit digital menu navigation; no hidden vertical camera rotation.
        if(sticks[0].y>.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_UP;
        if(sticks[0].y<-.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_DOWN;
        if(sticks[0].x>.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_RIGHT;
        if(sticks[0].x<-.65f)pad.wButtons|=XINPUT_GAMEPAD_DPAD_LEFT;
        pad.sThumbLX=pad.sThumbLY=pad.sThumbRX=0;
    }
    return pad;
}
static bool P18ApplyQuestChords(XINPUT_GAMEPAD& pad,const bool b[7]){
    bool microphone=false;
    // Touch has no application-visible Back/View button. X+Y is deliberate,
    // available on every supported Touch layout, and leaves each button's
    // native Outlast meaning unchanged when pressed by itself.
    if(b[2]&&b[3]){
        pad.wButtons&=static_cast<WORD>(~(XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y));
        pad.wButtons|=XINPUT_GAMEPAD_BACK;
    }
    // Restore the two remaining native mechanics without stealing a normal
    // face-button press: A+B is Look Back. Menu+Y remains a compatibility
    // microphone chord, while Y+right-stick-click is the primary mapping
    // because Quest reserves the left Menu button on some runtimes.
    if(b[0]&&b[1]){
        pad.wButtons&=static_cast<WORD>(~(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B));
        pad.wButtons|=XINPUT_GAMEPAD_LEFT_SHOULDER;
    }
    if(b[4]&&b[3]&&!b[2]){
        pad.wButtons&=static_cast<WORD>(~(XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_Y));
        microphone=true;
    }
    if(b[3]&&b[6]&&!b[2]){
        pad.wButtons&=static_cast<WORD>(~(XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_RIGHT_THUMB));
        microphone=true;
    }
    return microphone;
}
bool P18ApplyUseLatch(XINPUT_GAMEPAD& pad,bool rawX,bool recordingsChord,
    bool holdUseAvailable,ULONGLONG now){
    if(holdUseAvailable)p18UseLastAvailable=now;
    const bool rising=rawX&&!p18PreviousRawX;
    p18PreviousRawX=rawX;
    if(recordingsChord){
        p18UseLatched=false;p18UseLastAvailable=0;
        pad.wButtons&=static_cast<WORD>(~XINPUT_GAMEPAD_X);
        return false;
    }
    bool changed=false;
    if(rising){
        if(p18UseLatched){p18UseLatched=false;p18UseLastAvailable=0;changed=true;}
        else if(holdUseAvailable){p18UseLatched=true;p18UseLastAvailable=now;changed=true;}
    }
    // Fail open if the engine no longer advertises a hold interaction. This
    // prevents a stale virtual X from carrying into ordinary gameplay.
    if(p18UseLatched&&!holdUseAvailable&&p18UseLastAvailable&&
       now>=p18UseLastAvailable&&now-p18UseLastAvailable>750){
        p18UseLatched=false;p18UseLastAvailable=0;changed=true;
    }
    if(p18UseLatched)pad.wButtons|=XINPUT_GAMEPAD_X;
    return changed;
}
static void P18ApplyCamcorderGrip(XINPUT_GAMEPAD& pad,bool nativeCamcorderPulse){
    // Outlast's stock gamepad bindings remain authoritative. This preserves its
    // interaction traces, state checks, hand animations, camcorder HUD, night
    // vision, batteries and scripted transitions.
    if(nativeCamcorderPulse)pad.wButtons|=XINPUT_GAMEPAD_RIGHT_SHOULDER;
}
static void P18QueueNativeBandageUse(bool bandagePulse){
    if(bandagePulse)p35runtime::BandagePressed();
}
static uint64_t P18NativeAnimationHandoffMs(WORD pressed,bool bandagePulse,bool nativeCamcorderPulse){
    // Yield for only the short state-transition window. Once Outlast actually
    // begins an interaction/heal, its non-ordinary movement state keeps native
    // ownership for the full authored animation. A refused action therefore no
    // longer makes tracked hands disappear for several seconds.
    if(pressed&XINPUT_GAMEPAD_X)return 180;
    if(bandagePulse)return 180;
    if(nativeCamcorderPulse)return 1100;
    return 0;
}
static void P18ApplyNativeAnimationHandoff(uint64_t now,WORD pressed,bool bandagePulse,bool nativeCamcorderPulse){
    const auto duration=P18NativeAnimationHandoffMs(pressed,bandagePulse,nativeCamcorderPulse);
    if(!duration)return;
    const uint64_t requestedUntil=now+duration;
    const auto current=g_p41NativeAnimationUntil.load(std::memory_order_relaxed);
    if(requestedUntil>current)g_p41NativeAnimationUntil.store(requestedUntil,std::memory_order_release);
}
void P18SyncNativeCamcorderState(){
    unsigned char cameraState=255;
    const auto* pawn=g_p20Pawn.load(std::memory_order_relaxed);
    const bool readable=gameplayVr&&pawn&&P19Read(pawn,0x68bd,cameraState);
    const bool active=readable&&cameraState==1;
    const bool previous=g_p37CameraActive.exchange(active,std::memory_order_relaxed);
    const bool priorProjectionState=g_p41CamcorderRaised.exchange(active,std::memory_order_acq_rel);
    if(priorProjectionState!=active)g_p41CamcorderProjectionBaselineValid.store(false,std::memory_order_release);
    if(previous!=active)Log("MC5 NATIVE CAMCORDER STATE -> %s (state=%u); VR HUD and zoom follow the game, not grip prediction.",active?"RAISED":"LOWERED",static_cast<unsigned>(cameraState));
}
void P18Update(bool focused=OutlastHasFocus()){
    if(!p18VrInputEnabled||!p18Ready||!sessionRunning||state!=XR_SESSION_STATE_FOCUSED||!focused){P18Clear();return;}
    XrActiveActionSet active{p18Set,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
    if(p18Sync(session,&sync)!=XR_SUCCESS){P18Clear();return;}
    XrVector2f sticks[2]{};bool b[7]{};float f[4]{};bool any=false;
    for(int i=0;i<13;++i){
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=p18Actions[i];
        if(i<2){XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};if(XR_FAILED(p18Vector(session,&get,&s))){P18Clear();return;}if(s.isActive){sticks[i]=s.currentState;any=true;}}
        else if(i<9){XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};if(XR_FAILED(p18Bool(session,&get,&s))){P18Clear();return;}if(s.isActive){b[i-2]=s.currentState!=0;any=true;}}
        else{XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};if(XR_FAILED(p18Float(session,&get,&s))){P18Clear();return;}if(s.isActive){f[i-9]=s.currentState;any=true;}}
    }
    if(!any){P18Clear();return;}
    if(!p18WasActive)Log("P18 QUEST INPUT ACTIVE: release controls to arm; slot 0 reserved while Quest actions are active.");
    p18WasActive=true;
    P18SyncNativeCamcorderState();

    // Holding both thumbstick clicks opens the in-headset config. While the menu
    // is open all game input is consumed. In immersive mode this also replaces
    // left-stick-click sprint with physical arm-swing sprint.
    if(P37Controls(sticks,b,f)){
        p18BandageGrip.Reset();p18CamcorderGrip.Reset();p18PreviousBandagePulse=false;p18PreviousMicrophoneChord=false;p18PreviousButtons=0;
        p18UseLatched=false;p18PreviousRawX=false;p18UseLastAvailable=0;
        P18Publish({},true);return;
    }

    XINPUT_GAMEPAD pad=P18Map(sticks,b,f,gameplayVr);
    const bool microphoneChord=P18ApplyQuestChords(pad,b);
    if(!p18Armed){
        bool neutral=pad.wButtons==0&&pad.bLeftTrigger<30&&pad.bRightTrigger<30&&f[0]<.35f&&f[1]<.35f;
        for(auto v:sticks)neutral=neutral&&std::fabs(v.x)<.2f&&std::fabs(v.y)<.2f;
        p18BandageGrip.Reset();p18CamcorderGrip.Reset();p18PreviousBandagePulse=false;p18PreviousMicrophoneChord=false;
        if(neutral){
            // Arm both grip edge gates from known released states.
            p18BandageGrip.Update(f[0],false,GetTickCount64());
            p18CamcorderGrip.Update(f[1],false,GetTickCount64());
        }
        p18Armed=neutral;p18PreviousButtons=0;P18Publish({},true);return;
    }
    const auto now=GetTickCount64();
    const bool recordingsChord=b[2]&&b[3];
    const bool holdUseAvailable=p35runtime::HoldUseAvailable();
    if(P18ApplyUseLatch(pad,b[2],recordingsChord,holdUseAvailable,now))
        Log("PF8 QUEST X LATCH -> %s (native hold interaction available=%d); left stick remains free for push/pull/peek.",
            p18UseLatched?"HELD":"RELEASED",holdUseAvailable?1:0);
    const bool gesturesAllowed=gameplayVr&&!p37GuiOpen;
    const bool bandagePulse=p18BandageGrip.Update(f[0],gesturesAllowed,now);
    const bool bandagePressed=bandagePulse&&!p18PreviousBandagePulse;
    p18PreviousBandagePulse=bandagePulse;
    const bool camcorderPulse=p18CamcorderGrip.Update(f[1],gesturesAllowed,now);
    P18ApplyCamcorderGrip(pad,camcorderPulse);
    if(camcorderPulse&&!(p18PreviousButtons&XINPUT_GAMEPAD_RIGHT_SHOULDER))
        Log("MC6F RIGHT GRIP -> button-only native Outlast camcorder press; arm IK yields to the original raise/lower animation.");
    const WORD pressed=pad.wButtons&~p18PreviousButtons;p18PreviousButtons=pad.wButtons;
    const bool microphonePressed=microphoneChord&&!p18PreviousMicrophoneChord;
    p18PreviousMicrophoneChord=microphoneChord;
    P18ApplyNativeAnimationHandoff(now,pressed,bandagePressed,camcorderPulse);
    P18QueueNativeBandageUse(bandagePressed);
    if(microphonePressed)p35runtime::MicrophonePressed();
    if(bandagePressed)
        Log("PB6 LEFT GRIP -> queued one native Outlast bandage action; the game remains authoritative for inventory, injury state and healing animation.");
    if(pressed&XINPUT_GAMEPAD_X){
        p35runtime::QuestXPressed();
        Log("PF8 QUEST X -> native Use input; hold-style objects may latch while pickups and doors remain single actions.");
    }
    if(pressed&XINPUT_GAMEPAD_BACK)Log("PB1 QUEST X+Y -> native Xbox Back: recordings/camcorder files menu requested.");
    if(pressed&XINPUT_GAMEPAD_LEFT_SHOULDER)Log("PB1 QUEST A+B -> native Look Back requested.");
    if(microphonePressed)Log("PF6 QUEST microphone chord -> native ToggleMicrophone queued on the game thread.");
    P18Publish(pad,true);
    if(pressed||now-p18HapticTick>=50){
        p18HapticTick=now;XINPUT_VIBRATION rumble{};
        {std::lock_guard<std::mutex> lock(g_p18Mutex);if(now-g_p18RumbleTick<2000)rumble=g_p18Rumble;}
        const WORD motors[]={rumble.wLeftMotorSpeed,rumble.wRightMotorSpeed};
        for(int i=0;i<2;++i){
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=p18Haptic;info.subactionPath=p18Hands[i];
            const float amplitude=std::max(motors[i]/65535.0f*.65f,pressed?.12f:0.0f);
            if(amplitude>0){XrHapticVibration v{XR_TYPE_HAPTIC_VIBRATION};v.duration=60000000;v.frequency=XR_FREQUENCY_UNSPECIFIED;v.amplitude=amplitude;
                const auto result=p18Apply(session,&info,reinterpret_cast<XrHapticBaseHeader*>(&v));
                if(result==XR_SUCCESS)++p18HapticCalls;else if(++p18HapticErrors<=3)Log("P18 haptic request failed: %d",static_cast<int>(result));}
            else p18Stop(session,&info);
        }
    }
    if((g_probeFrameCounter.load()%600)==0)Log("P18 INPUT STATUS: armed=%d gamePolls=%llu gameRumbleCalls=%llu hapticAccepted=%llu hapticErrors=%llu",p18Armed,g_p18Polls.load(),g_p18Rumbles.load(),p18HapticCalls,p18HapticErrors);
}
