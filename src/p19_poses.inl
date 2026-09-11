// Controller poses in the same LOCAL space/display time as the head. No mesh writes.
PFN_xrCreateActionSpace p19CreateSpace=nullptr;
PFN_xrGetActionStatePose p19GetPose=nullptr;
XrAction p19PoseAction=XR_NULL_HANDLE;
XrSpace p19Spaces[2]{};
XrPosef p19Poses[2]{};
bool p19TrackingEnabled=false,p19PoseBound=false,p19Valid[2]{},p19CaptureDown=false;
unsigned p19CaptureCount=0;
ULONGLONG p19CaptureAfter=0;
ULONGLONG p19StatusTick=0;
unsigned p19LastStatusMask=~0u;
bool p20ToggleDown=false;
bool p22RoomToggleDown=false;
void P19ClearTracked(){
    p19Valid[0]=p19Valid[1]=false;
    std::lock_guard<std::mutex> lock(g_p20PoseMutex);
    g_p20Tracked.tick=0;
    g_p20Tracked.handValid[0]=g_p20Tracked.handValid[1]=false;
}
void P19AddPoseBindings(std::vector<XrActionSuggestedBinding>& bindings){
    if(!p19TrackingEnabled)return;
    if(!LoadInstanceProc("xrCreateActionSpace",p19CreateSpace)||!LoadInstanceProc("xrGetActionStatePose",p19GetPose))return;
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    strcpy_s(info.actionName,"body_grip_pose");strcpy_s(info.localizedActionName,"Body IK controller grip pose");
    info.actionType=XR_ACTION_TYPE_POSE_INPUT;info.countSubactionPaths=2;info.subactionPaths=p18Hands;
    if(XR_FAILED(p18CreateAction(p18Set,&info,&p19PoseAction)))return;
    XrPath paths[2]{};
    if(XR_FAILED(p18Path(instance,"/user/hand/left/input/grip/pose",&paths[0]))||
       XR_FAILED(p18Path(instance,"/user/hand/right/input/grip/pose",&paths[1])))return;
    for(int i=0;i<2;++i)bindings.push_back({p19PoseAction,paths[i]});
    p19PoseBound=true;
}
void P19CreateSpaces(){
    if(!p19TrackingEnabled||!p19PoseBound)return;
    for(int i=0;i<2;++i){
        XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};ci.action=p19PoseAction;ci.subactionPath=p18Hands[i];ci.poseInActionSpace.orientation.w=1;
        if(XR_FAILED(p19CreateSpace(session,&ci,&p19Spaces[i]))){
            for(auto& space:p19Spaces)if(space){xrDestroySpace(space);space=XR_NULL_HANDLE;}
            p19PoseBound=false;Log("P19 grip spaces unavailable; P18 button input retained.");return;
        }
    }
    Log("P19 GRIP POSES READY: LOCAL-space poses enabled for MC2 hybrid upper-body IK.");
}
void P19Locate(XrTime time,bool focused){
    P19ClearTracked();
    if(!focused||!gameplayVr||state!=XR_SESSION_STATE_FOCUSED||!p19PoseBound||!p12HaveCenter||!p12Pending.valid){
        return;}
    const bool toggle=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    if(toggle&&!p20ToggleDown){g_p20Enabled.store(!g_p20Enabled.load());Log("P20 BODY IK TOGGLE: %s",g_p20Enabled.load()?"enabled":"disabled; original pose restored on normal skeletal tick");}
    p20ToggleDown=toggle;
    const bool roomToggle=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
    if(roomToggle&&!p22RoomToggleDown)Log("P23 capsule following disabled pending camera/trigger synchronization repair; visual roomscale remains active.");
    p22RoomToggleDown=roomToggle;
    const auto required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    for(int i=0;i<2;++i){
        if(!p19Spaces[i])continue;
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};gi.action=p19PoseAction;gi.subactionPath=p18Hands[i];
        XrActionStatePose active{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(p19GetPose(session,&gi,&active))||!active.isActive)continue;
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(xrLocateSpace(p19Spaces[i],localSpace,time,&loc))&&
           (loc.locationFlags&required)==required&&P12ValidPose(loc.pose)){p19Poses[i]=loc.pose;p19Valid[i]=true;}
    }
    const auto now=GetTickCount64();
    const unsigned statusMask=(p19Valid[0]?1u:0u)|(p19Valid[1]?2u:0u);
    if(statusMask!=p19LastStatusMask||now-p19StatusTick>=5000){
        p19LastStatusMask=statusMask;p19StatusTick=now;
        Log("MC2 TRACKED CONTROLLERS: left=%d right=%d predictedTime=%lld bodyIK=%d.",
            p19Valid[0]?1:0,p19Valid[1]?1:0,static_cast<long long>(time),g_p20Enabled.load()?1:0);
    }
    {
        P20Tracked sample;sample.center=p12Center;sample.units=p12UnitsPerMeter;sample.tick=GetTickCount64();sample.roomGeneration=p22RoomFrame.generation;
        sample.handValid[0]=p19Valid[0];sample.handValid[1]=p19Valid[1];
        XrPosef poses[]={p12BodyHead,p19Poses[0],p19Poses[1]};
        const XrVector3f bodyCorrection{
            p12BodyHead.position.x-p12LastHead.position.x,
            p12BodyHead.position.y-p12LastHead.position.y,
            p12BodyHead.position.z-p12LastHead.position.z};
        for(int i=1;i<3;++i){
            poses[i].position.x+=bodyCorrection.x;
            poses[i].position.y+=bodyCorrection.y;
            poses[i].position.z+=bodyCorrection.z;
        }
        const auto inverse=P11QuatConjugate(p12Center.orientation);
        for(int i=0;i<3;++i){
            if(i>0&&!sample.handValid[i-1])continue;
            float x,y,z;P11RotateVector(inverse,poses[i].position.x-p12Center.position.x,poses[i].position.y-p12Center.position.y,poses[i].position.z-p12Center.position.z,x,y,z);
            sample.position[i]={x,y,z};const auto q=P11QuatMul(inverse,poses[i].orientation);sample.rotation[i]={q.x,q.y,q.z,q.w};
        }
        std::lock_guard<std::mutex> lock(g_p20PoseMutex);g_p20Tracked=sample;
    }
    const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(down&&!p19CaptureDown&&p19CaptureCount<3&&!g_p19CaptureRequest.load()&&!p19CaptureAfter){
        p19CaptureAfter=now+5000;
        Log("P19 capture armed: pick up both controllers; waiting five seconds and valid tracking before saving.");
    }
    if(p19CaptureAfter&&now>p19CaptureAfter+20000){
        p19CaptureAfter=0;Log("P19 capture timed out: both controllers must be tracked. No invalid pose sample saved; press F9 to retry.");
    }
    if(P19CaptureReady(now,p19CaptureAfter,p19Valid[0],p19Valid[1])&&!g_p19CaptureRequest.load()){
        p19CaptureAfter=0;
        ++p19CaptureCount;const auto dir=P19Directory();CreateDirectoryW(dir.c_str(),nullptr);
        FILE* f=nullptr;const auto path=dir+L"\\sample_"+std::to_wstring(p19CaptureCount)+L"_poses.txt";
        if(!_wfopen_s(&f,path.c_str(),L"w")&&f){
            fprintf(f,"time=%lld unitsPerMeter=%.6f leftValid=%d rightValid=%d\n",static_cast<long long>(time),p12UnitsPerMeter,p19Valid[0],p19Valid[1]);
            const XrPosef values[]={p12Center,p12LastHead,p19Poses[0],p19Poses[1]};
            const char* names[]={"center","head","leftGrip","rightGrip"};
            for(int i=0;i<4;++i){const auto& p=values[i];fprintf(f,"%s position=%.6f %.6f %.6f quaternion=%.6f %.6f %.6f %.6f\n",names[i],p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w);}
            fclose(f);g_p19CaptureRequest.store(p19CaptureCount);Log("P19 capture requested: %u, tracked hands=%d/%d",p19CaptureCount,p19Valid[0],p19Valid[1]);
        }
    }
    p19CaptureDown=down;
}
