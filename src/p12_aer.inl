// Included inside OpenXRQuad. Present owns this state; renderer constants are
// published atomically as a single mutex-protected snapshot before the next render.
struct P12EyeFrame {
    XrView view{XR_TYPE_VIEW};
    uint64_t frame = ~0ull;
    uint64_t pair = 0;
    uint64_t generation = 0;
    XrTime time = 0;
    uint32_t eye = 0;
    bool valid = false;
};
P12EyeFrame p12Pending, p12Eyes[2];
ID3D11Texture2D* p12EyeColor[2]{}; // last complete pair submitted to XR
ID3D11Texture2D* p12CaptureColor[2]{}; // candidate eye currently being captured
P12EyeFrame p12StableEyes[2];
bool p12StablePairValid=false, p12StableReuseLogged=false;
XrView p12PairViews[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
XrView p12MonoView{XR_TYPE_VIEW};
bool p12MonoViewValid=false;
XrPosef p12PairHead{{0,0,0,1},{0,0,0}};
XrPosef p12LockedHead{{0,0,0,1},{0,0,0}};
bool p12LockedHeadValid=false;
XrPosef p12Center{{0,0,0,1},{0,0,0}};
XrPosef p12LastHead{{0,0,0,1},{0,0,0}};
XrPosef p12BodyHead{{0,0,0,1},{0,0,0}};
bool p12HaveCenter = false, p12RecenterDown = false;
bool p12ConfigLoaded = false, p12PairLogged = false, p12PairLockLogged = false;
bool p12PairLocked = false;
int p12SameFrameStereo = 0;
bool p12LockHeadTranslation = true;
uint32_t p12NextEye = 0;
uint64_t p12CapturedCount[2]{};
uint64_t p12PairSequence=0, p12ActivePair=0;
uint32_t p12CompatibilityMisses=0;
bool p12CompatibilityLogged=false;
float p12UnitsPerMeter = 100.0f;
bool p15PitchLock=true;
P22RoomPose p22RoomFrame{};
float p15YawSign=1.0f;
bool p12StereoAcquired = false;
uint32_t p12AcquiredIndex = 0;
XrTime p12ReferenceChangeTime = 0;
uint64_t p12LastDropLog = ~0ull;

void P12Reset(bool resetCenter) {
    g_p15PoseTick.store(0);
    p12Pending.valid = p12Eyes[0].valid = p12Eyes[1].valid = false;
    p12StableEyes[0].valid=p12StableEyes[1].valid=false;
    p12StablePairValid=false;
    p12PairLocked=false;
    p12MonoViewValid=false;
    p12ActivePair=0;
    p12NextEye = 0;
    if (resetCenter) {p12HaveCenter = false;p12LockedHeadValid=false;p46LastGameplayValid=false;P22ResetRoom();}
    else {std::lock_guard<std::mutex> lock(g_p22RoomMutex);g_p22Room.tick=0;}
    std::lock_guard<std::mutex> lock(g_p12ConstantsMutex);
    g_p12Constants = {};
}

void P12ReleaseEyes() {
    p46LastGameplayValid=false;
    P12Reset(false);
    for (auto*& tex : p12EyeColor) { if (tex) tex->Release(); tex = nullptr; }
    for (auto*& tex : p12CaptureColor) { if (tex) tex->Release(); tex = nullptr; }
}

static bool P12ValidPose(const XrPosef& p) {
    const auto& q=p.orientation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(norm) && std::fabs(norm-1.0f)<0.01f &&
        std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.position.z);
}

void P12Prepare(XrTime time,bool applicationFocused=OutlastHasFocus()) {
    if (!gameplayVr || !sessionRunning || !applicationFocused) { P12Reset(true); return; }
    if (p12ReferenceChangeTime && time>=p12ReferenceChangeTime) {
        P12Reset(true); p12ReferenceChangeTime=0;
        Log("P12 LOCAL reference-space change became effective; center reset.");
    }
    if (!p12ConfigLoaded) {
        // P37 world scale: UE3/Outlast uses centimetre-scale world units, so
        // 100 game units per physical metre is the calibrated 1:1 baseline.
        // WorldScale >1 makes the game world feel larger; <1 makes it smaller.
        wchar_t value[64]{};
        const auto p37config=ModuleDir()+L"\\outlast2_vr_p37.ini";
        GetPrivateProfileStringW(L"VR",L"WorldScale",L"1.0",value,64,p37config.c_str());
        wchar_t* end=nullptr;float worldScale=std::wcstof(value,&end);
        if(end==value||*end!=0||!std::isfinite(worldScale)||worldScale<0.5f||worldScale>2.0f)worldScale=1.0f;
        p12UnitsPerMeter=100.0f/worldScale;
        p12ConfigLoaded=true;
        const auto config=ModuleDir()+L"\\outlast2_vr_p32.ini";
        p12SameFrameStereo=std::clamp<int>(static_cast<int>(GetPrivateProfileIntW(L"VR",L"SameFrameStereo",0,config.c_str())),0,2);
        g_p22BodyYaw.store(GetPrivateProfileIntW(L"VR",L"BodyYaw",1,config.c_str())!=0);
        g_p22RightNeutral.store(GetPrivateProfileIntW(L"VR",L"RightHandNeutral",1,config.c_str())!=0);
        g_p22RoomEnabled.store(GetPrivateProfileIntW(L"VR",L"HeadBodyFollow",0,config.c_str())!=0);
        p12LockHeadTranslation=GetPrivateProfileIntW(L"VR",L"LockHeadTranslation",1,config.c_str())!=0;
        g_p21RoomscaleFollow.store(GetPrivateProfileIntW(L"VR",L"RoomscaleBody",1,config.c_str())!=0);
        // Native capsule following remains opt-in.  Its old trigger/camera
        // synchronization path is not part of the body-anchor correction.
        g_p22RoomEnabled.store(g_p22RoomEnabled.load()&&g_p21RoomscaleFollow.load());
        g_p20Enabled.store(GetPrivateProfileIntW(L"VR",L"BodyIK",1,config.c_str())!=0);
        p15PitchLock=GetPrivateProfileIntW(L"VR",L"PitchLock",1,config.c_str())!=0;
        g_p22RoomBasisValid.store(p15PitchLock);
        Log("P22 CONFIG: BodyIK=%d BodyYaw=%d RightHandNeutral=%d HeadBodyFollow=%d LockHeadTranslation=%d basisValid=%d.",g_p20Enabled.load()?1:0,g_p22BodyYaw.load()?1:0,g_p22RightNeutral.load()?1:0,g_p22RoomEnabled.load()?1:0,p12LockHeadTranslation?1:0,p15PitchLock?1:0);
        Log("TS1 STEREO CONFIG: SameFrameStereo=%d (0=original AER, 1=PB6 clean mono, 2=experimental same-frame depth stereo with PB6 fallback).",p12SameFrameStereo);
        g_p15WalkingEnabled.store(GetPrivateProfileIntW(L"VR",L"HmdWalking",1,config.c_str())!=0);
        // Separate from the legacy ReflectionCamera option: PF13 excludes
        // all no-depth passes and only corrects exact main-view matrix matches.
        g_p15ReflectionEnabled.store(GetPrivateProfileIntW(L"VR",L"ScopedPixelCamera",1,
            (ModuleDir()+L"\\outlast2_vr_p35.ini").c_str())!=0);
        Log("PF13 SCOPED PIXEL CAMERA: enabled=%d; validated scene depth only; exact matrix matches; original buffers restored after draw.",
            g_p15ReflectionEnabled.load()?1:0);
        p15YawSign=GetPrivateProfileIntW(L"VR",L"HmdYawSign",1,config.c_str())<0?-1.0f:1.0f;
        Log("P15 CONFIG: PitchLock=%d HmdWalking=%d HmdYawSign=%.0f; headset yaw/pitch retained, camera translation anchored to the game body; no auto-walk.",p15PitchLock?1:0,g_p15WalkingEnabled.load()?1:0,p15YawSign);
        Log("P37 WORLD SCALE: %.3f game-units/m (1.0 world scale = 100 uu/m UE3 baseline); OpenXR IPD and tracked translation use the same scale. F8=recenter.",p12UnitsPerMeter);
    }
    const bool recenter=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if (recenter && !p12RecenterDown) { P12Reset(true); Log("P12 manual recenter requested."); }
    p12RecenterDown=recenter;

    XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
    const auto required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    XrViewState vs{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
    li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime=time; li.space=localSpace;
    uint32_t count=0;
    const XrResult hr=xrLocateSpace(viewSpace,localSpace,time,&head);
    const XrResult vr=xrLocateViews(session,&li,&vs,2,&count,views);
    const auto viewRequired=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if (XR_FAILED(hr) || XR_FAILED(vr) || count!=2 || (head.locationFlags&required)!=required ||
        (vs.viewStateFlags&viewRequired)!=viewRequired || !P12ValidPose(head.pose) ||
        !P12ValidPose(views[0].pose) || !P12ValidPose(views[1].pose)) {
        if (g_probeFrameCounter.load()%120==0)
            Log("P12 tracking unavailable: head=%d views=%d flags=%llu/%llu count=%u; no eye frame prepared.",
                static_cast<int>(hr),static_cast<int>(vr),static_cast<unsigned long long>(head.locationFlags),
                static_cast<unsigned long long>(vs.viewStateFlags),count);
        P12Reset(false); return;
    }
    // The native game camera remains the base camera for scripted moments,
    // while OpenXR continues to supply the live headset pose. The body-anchor
    // policy below limits only translation; it must never freeze the HMD pose.
    XrPosef renderHead=head.pose;

    // The game renders one monoscopic image per Present. When that image is
    // copied to both XR array slices, submitting the two native eye poses/FOVs
    // makes the same pixels land at two different optical locations, which is
    // exactly the full-level double vision seen in the headset. Build one
    // shared mono view for the render and submit paths; the renderer receives
    // live HMD orientation while the gamepad-only beta anchors translation to
    // the player body.
    if (p12SameFrameStereo) {
        // Do not synthesize a new XrView by averaging runtime data. Some
        // VDXR builds expose valid native eye views but reject an averaged
        // pose/FOV combination, which silently prevents P12Pending from ever
        // becoming valid and produces a black headset. Use the guaranteed-valid
        // native left pose for the mono render and both projection slices. The
        // two native eye FOVs are asymmetric, so use their union for the shared
        // mono FOV; otherwise the right side can remain an uncovered black strip.
        p12MonoView=views[0];
        p12MonoView.fov.angleLeft=std::min(views[0].fov.angleLeft,views[1].fov.angleLeft);
        p12MonoView.fov.angleRight=std::max(views[0].fov.angleRight,views[1].fov.angleRight);
        p12MonoView.fov.angleDown=std::min(views[0].fov.angleDown,views[1].fov.angleDown);
        p12MonoView.fov.angleUp=std::max(views[0].fov.angleUp,views[1].fov.angleUp);
        float fovProbe[4]{};
        p12MonoViewValid=P12ValidPose(p12MonoView.pose) &&
            p12::Projection(p12MonoView.fov.angleLeft,p12MonoView.fov.angleRight,
                            p12MonoView.fov.angleDown,p12MonoView.fov.angleUp,fovProbe);
        if (!p12MonoViewValid) { P12Reset(false); return; }
    }
    if (p12HaveCenter) {
        const auto& a=head.pose.orientation; const auto& b=p12LastHead.orientation;
        const float dot=std::clamp(std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w),0.0f,1.0f);
        const float dx=head.pose.position.x-p12LastHead.position.x;
        const float dy=head.pose.position.y-p12LastHead.position.y;
        const float dz=head.pose.position.z-p12LastHead.position.z;
        if (2.0f*std::acos(dot)>0.65f || dx*dx+dy*dy+dz*dz>1.0f) {
            P12Reset(true); Log("P12 tracking discontinuity: eye history and center reset.");
        }
    }
    if (!p12HaveCenter) {
        p12Center=head.pose;
        if(p15PitchLock){float yaw,pitch;PoseYawPitch(head.pose.orientation,yaw,pitch);p12Center.orientation={0,-std::sin(yaw*0.5f),0,std::cos(yaw*0.5f)};}
        p12HaveCenter=true;
    }
    p12LastHead=head.pose;
    if(p12LockHeadTranslation){
        // MC6F body anchor: retain positional 6DoF inside a neck-sized volume,
        // but do not let room-scale walking detach the view from the native
        // character.  Bound relative to displacement already consumed by the
        // body, not the original OpenXR center; this remains correct if the
        // optional native follower is enabled later.  This path is active in
        // ordinary gameplay, camcorder use and scripted scenes alike.
        constexpr float kBodyHorizontalRadius=0.10f;
        constexpr float kBodyVerticalRadius=0.18f;
        float localX,localY,localZ;
        P11RotateVector(P11QuatConjugate(p12Center.orientation),
            head.pose.position.x-p12Center.position.x,
            head.pose.position.y-p12Center.position.y,
            head.pose.position.z-p12Center.position.z,localX,localY,localZ);
        p20::V consumed{};
        {std::lock_guard<std::mutex> lock(g_p22RoomMutex);consumed=g_p22Room.consumed;}
        const auto boundedLocal=p22::BoundHeadToBody(
            {localX,localY,localZ},consumed,kBodyHorizontalRadius,kBodyVerticalRadius);
        localX=boundedLocal.x;localY=boundedLocal.y;localZ=boundedLocal.z;
        float worldX,worldY,worldZ;
        P11RotateVector(p12Center.orientation,localX,localY,localZ,worldX,worldY,worldZ);
        const XrVector3f bounded{
            p12Center.position.x+worldX,
            p12Center.position.y+worldY,
            p12Center.position.z+worldZ};
        const auto rawHeadPosition=head.pose.position;
        renderHead.position=bounded;
        head.pose.position=bounded;
        for(auto& view:views){
            view.pose.position={
                bounded.x+(view.pose.position.x-rawHeadPosition.x),
                bounded.y+(view.pose.position.y-rawHeadPosition.y),
                bounded.z+(view.pose.position.z-rawHeadPosition.z)};
        }
        if(p12MonoViewValid){
            p12MonoView.pose.position={
                bounded.x+(p12MonoView.pose.position.x-rawHeadPosition.x),
                bounded.y+(p12MonoView.pose.position.y-rawHeadPosition.y),
                bounded.z+(p12MonoView.pose.position.z-rawHeadPosition.z)};
        }
    }
    // P19 uses this corrected pose for the skeleton and shifts both grip poses
    // by the same amount.  Camera, head and hands therefore remain one tracking
    // frame instead of allowing the hands/body to separate from a bounded view.
    p12BodyHead=renderHead;

    // AER renders one game eye per Present. Keep the HMD pose and both eye
    // view poses anchored for the two Presents that form one stereo pair;
    // otherwise head motion between them becomes a translucent second copy of
    // the level when the runtime presents the pair together. The visual-fix
    // mode deliberately captures eye 0 every frame and later copies those
    // pixels to both slices, so the original P44 projection path remains
    // intact without mixing two simulation frames.
    if (p12SameFrameStereo) {
        p12PairViews[0]=views[0];
        p12PairViews[1]=views[1];
        p12PairHead=head.pose;
        p12PairLocked=false;
    } else if (p12PairLocked && p12NextEye==1) {
        head.pose=p12PairHead;
        views[1]=p12PairViews[1];
    } else {
        p12PairViews[0]=views[0];
        p12PairViews[1]=views[1];
        p12PairHead=head.pose;
        p12PairLocked=true;
        p12ActivePair=++p12PairSequence;
        if(!p12PairLockLogged){
            p12PairLockLogged=true;
            Log("P12 AER PAIR LOCK ACTIVE: left/right captures share one predicted HMD pose; P44 projection and eye FOV remain unchanged.");
        }
    }

    const uint32_t preparedEye=p12SameFrameStereo?0u:p12NextEye;
    const XrView* eyePtr=(p12SameFrameStereo&&p12MonoViewValid)?&p12MonoView:&views[preparedEye];
    const auto& eye=*eyePtr;
    P11HeadConstants h{};
    if (!p12::Projection(eye.fov.angleLeft,eye.fov.angleRight,eye.fov.angleDown,eye.fov.angleUp,h.projection)) {
        P12Reset(false); return;
    }
    const auto inverseCenter=P11QuatConjugate(p12Center.orientation);
    const auto headRelative=P11QuatMul(inverseCenter,renderHead.orientation);
    float hx,hy,hz;
    P11RotateVector(inverseCenter,renderHead.position.x-p12Center.position.x,renderHead.position.y-p12Center.position.y,renderHead.position.z-p12Center.position.z,hx,hy,hz);
    p22RoomFrame=P22PublishRoom({hx,hy,hz},p12UnitsPerMeter);
    float walkYaw,walkPitch;PoseYawPitch(headRelative,walkYaw,walkPitch);
    // P37 native engine ViewRotation sync. This lets engine-side systems that
    // consume the player view (culling and the native Wwise listener) track the HMD.
    if(g_nativeTablePatched.load(std::memory_order_relaxed)){
        g_nativeHeadYawRadians.store(walkYaw,std::memory_order_relaxed);
        g_nativeHeadPitchRadians.store(walkPitch,std::memory_order_relaxed);
    }
    // Do not use an eye's IPD offset or roll to determine walking direction.
    if(std::fabs(std::cos(walkPitch))>0.05f)g_p15MovementYaw.store(walkYaw*p15YawSign);
    g_p15PoseTick.store(GetTickCount64());
    const auto nativeFrame=g_p46NativeViewFrame.load(std::memory_order_acquire);
    const auto currentFrame=g_probeFrameCounter.load();
    const bool nativeViewSync=g_nativeTablePatched.load()&&nativeFrame!=~0ull&&currentFrame>=nativeFrame&&currentFrame-nativeFrame<=2;
    // Native GetViewRotation applies yaw/pitch, not roll. Preserve that engine
    // pitch and apply only the remaining rotation here. An old hook call cannot
    // disable renderer tracking forever when a scripted camera takes over.
    const XrQuaternionf nativeYaw{0,-std::sin(walkYaw/2),0,std::cos(walkYaw/2)};
    const XrQuaternionf nativePitch{std::sin(walkPitch/2),0,0,std::cos(walkPitch/2)};
    const auto nativeRotation=P11QuatMul(nativeYaw,nativePitch);
    const auto eyeRelative=P11QuatMul(inverseCenter,eye.pose.orientation);
    const auto relative=nativeViewSync?P11QuatMul(P11QuatConjugate(nativeRotation),eyeRelative):eyeRelative;
    float x,y,z;
    P11RotateVector(relative,1,0,0,x,y,z); h.right[0]=x; h.right[1]=y; h.right[2]=-z;
    P11RotateVector(relative,0,1,0,x,y,z); h.up[0]=x; h.up[1]=y; h.up[2]=-z;
    P11RotateVector(relative,0,0,-1,x,y,z); h.forward[0]=x; h.forward[1]=y; h.forward[2]=-z;
    const auto translationBasis=nativeViewSync?P11QuatMul(P11QuatConjugate(nativeRotation),inverseCenter):inverseCenter;
    P11RotateVector(translationBasis,eye.pose.position.x-p12Center.position.x,
        eye.pose.position.y-p12Center.position.y,eye.pose.position.z-p12Center.position.z,x,y,z);
    const auto consumed=p22RoomFrame.consumed;
    const float offset[3]={(x-consumed.x)*p12UnitsPerMeter,y*p12UnitsPerMeter,-(z-consumed.z)*p12UnitsPerMeter};
    for(int i=0;i<3;++i) {
        h.eyeOffset[0]+=offset[i]*h.right[i];
        h.eyeOffset[1]+=offset[i]*h.up[i];
        h.eyeOffset[2]+=offset[i]*h.forward[i];
    }
    h.eyeOffset[3]=0.0f;
    h.params[0]=kP10ProjectionScale; h.params[1]=1; h.params[2]=1; h.params[3]=1.0f;
    h.right[3]=(p15PitchLock&&!nativeViewSync&&!g_nativeHeadLock.load())?1.0f:0.0f;
    p12Pending={};
    p12Pending.view=eye; p12Pending.frame=g_probeFrameCounter.load();
    p12Pending.pair=p12ActivePair;
    p12Pending.generation=g_p11cDepthGeneration.load();
    p12Pending.time=time; p12Pending.eye=preparedEye; p12Pending.valid=true;
    {
        std::lock_guard<std::mutex> lock(g_p12ConstantsMutex);
        g_p12Constants=h;
    }
    if (!p11RendererTrackingLogged) {
        p11RendererTrackingLogged=true;
        const float dx=views[1].pose.position.x-views[0].pose.position.x;
        const float dy=views[1].pose.position.y-views[0].pose.position.y;
        const float dz=views[1].pose.position.z-views[0].pose.position.z;
        if (p12SameFrameStereo) {
            Log("P12 MONO PROJECTION PREPARED: runtime IPD=%.2fmm; one shared native pose/FOV is used for both XR views; live HMD orientation and MC6F body-relative bounded 6DoF remain active. no crop/depth warp.",
                1000.0f*std::sqrt(dx*dx+dy*dy+dz*dz));
        } else {
            Log("P12 NATIVE EYE PROJECTION PREPARED: runtime IPD=%.2fmm; LOCAL render-pose submission; no crop/depth warp. L FOV=%.2f %.2f %.2f %.2f R FOV=%.2f %.2f %.2f %.2f radians.",
                1000.0f*std::sqrt(dx*dx+dy*dy+dz*dz),views[0].fov.angleLeft,views[0].fov.angleRight,
                views[0].fov.angleDown,views[0].fov.angleUp,views[1].fov.angleLeft,views[1].fov.angleRight,
                views[1].fov.angleDown,views[1].fov.angleUp);
        }
    }
}

bool P12CaptureEye(ID3D11Texture2D* backbuffer) {
    const uint64_t now=g_probeFrameCounter.load();
    if (!p12Pending.valid || p12Pending.frame+1!=now ||
        p12Pending.frame!=g_p12BoundFrame.load() ||
        !g_p10ViewProjectionValidated.load() ||
        p12Pending.generation!=g_p11cDepthGeneration.load()) {
        if (p12LastDropLog==~0ull || now-p12LastDropLog>=120) {
            p12LastDropLog=now;
            Log("P12 WAITING FOR MATCHED EYE: pending=%d render=%llu bound=%llu present=%llu validated=%d generation=%llu/%llu.",
                p12Pending.valid,static_cast<unsigned long long>(p12Pending.frame),
                static_cast<unsigned long long>(g_p12BoundFrame.load()),static_cast<unsigned long long>(now),
                g_p10ViewProjectionValidated.load(),static_cast<unsigned long long>(p12Pending.generation),
                static_cast<unsigned long long>(g_p11cDepthGeneration.load()));
        }
        P12Reset(false); return false;
    }
    D3D11_TEXTURE2D_DESC td{}; backbuffer->GetDesc(&td);
    if (td.Width!=width || td.Height!=height || td.Format!=format || td.ArraySize!=1 || td.MipLevels!=1) {
        P12Reset(false); return false;
    }
    const uint32_t eye=p12Pending.eye;
    if (!p12CaptureColor[eye]) {
        auto desc=td;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=0; desc.CPUAccessFlags=0; desc.MiscFlags=0;
        desc.SampleDesc={1,0};
        const HRESULT hr=device->CreateTexture2D(&desc,nullptr,&p12CaptureColor[eye]);
        if (FAILED(hr)) { Log("P12 eye texture creation failed: 0x%08X",static_cast<unsigned>(hr)); P12Reset(false); return false; }
    }
    if (td.SampleDesc.Count>1) context->ResolveSubresource(p12CaptureColor[eye],0,backbuffer,0,format);
    else context->CopySubresourceRegion(p12CaptureColor[eye],0,0,0,0,backbuffer,0,nullptr);
    p12Eyes[eye]=p12Pending;
    ++p12CapturedCount[eye];
    p12Pending.valid=false;
    p12NextEye=1-eye;
    if(eye==1)p12PairLocked=false;
    return true;
}

bool P12CopyPair(XrCompositionLayerProjectionView* out, XrTime displayTime, XrDuration period) {
    const uint64_t now=g_probeFrameCounter.load();
    bool candidate=true;
    for (int eye=0;eye<2;++eye) {
        const auto& saved=p12Eyes[eye];
        if (!saved.valid || now<saved.frame || now-saved.frame>2 ||
            saved.generation!=g_p11cDepthGeneration.load() ||
            (period>0 && std::llabs(displayTime-saved.time)>period*4)) { candidate=false;break; }
    }
    // Never submit a valid-looking mixture of an old left image and a new
    // right image. The two captures must be the ordered members of one pair;
    // the old age-only check allowed stale history to pass after a dropped or
    // transition frame.
    if(candidate&&(p12Eyes[0].pair==0||p12Eyes[0].pair!=p12Eyes[1].pair||
       p12Eyes[0].eye!=0||p12Eyes[1].eye!=1||
       p12Eyes[0].frame==~0ull||p12Eyes[0].frame+1!=p12Eyes[1].frame)){
        candidate=false;
        if(p12LastDropLog==~0ull||now-p12LastDropLog>=120){
            p12LastDropLog=now;
            Log("P12 REJECTED MIXED EYE HISTORY: L frame=%llu pair=%llu R frame=%llu pair=%llu; no stereo submission.",
                static_cast<unsigned long long>(p12Eyes[0].frame),static_cast<unsigned long long>(p12Eyes[0].pair),
                static_cast<unsigned long long>(p12Eyes[1].frame),static_cast<unsigned long long>(p12Eyes[1].pair));
        }
    }
    const P12EyeFrame* sourceEyes[2]{};
    ID3D11Texture2D* sourceColor[2]{};
    if(candidate){
        for(uint32_t eye=0;eye<2;++eye){
            if(!p12EyeColor[eye]){
                D3D11_TEXTURE2D_DESC desc{};p12CaptureColor[eye]->GetDesc(&desc);
                desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=0;desc.CPUAccessFlags=0;desc.MiscFlags=0;desc.SampleDesc={1,0};
                if(FAILED(device->CreateTexture2D(&desc,nullptr,&p12EyeColor[eye]))){candidate=false;break;}
            }
        }
        if(candidate)for(uint32_t eye=0;eye<2;++eye){
            context->CopyResource(p12EyeColor[eye],p12CaptureColor[eye]);
            p12StableEyes[eye]=p12Eyes[eye];
            sourceEyes[eye]=&p12StableEyes[eye];sourceColor[eye]=p12EyeColor[eye];
        }
        p12StablePairValid=candidate;
        p12StableReuseLogged=false;
    }
    if(!candidate){
        if(!p12StablePairValid)return false;
        for(uint32_t eye=0;eye<2;++eye){
            const auto& saved=p12StableEyes[eye];
            if(!saved.valid||!p12EyeColor[eye]||now<saved.frame||now-saved.frame>3||
               saved.generation!=g_p11cDepthGeneration.load()||
               (period>0&&std::llabs(displayTime-saved.time)>period*6)){
                return false;
            }
            sourceEyes[eye]=&p12StableEyes[eye];sourceColor[eye]=p12EyeColor[eye];
        }
        if(!p12StableReuseLogged){
            p12StableReuseLogged=true;
            Log("P12 AER STABLE PAIR REUSED: candidate eye pair incomplete; previous complete stereo pair held to prevent mono/stereo flashing.");
        }
    }
    if (!p12StereoAcquired) {
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (xrAcquireSwapchainImage(stereoSwapchain,&ai,&p12AcquiredIndex)!=XR_SUCCESS) return false;
        p12StereoAcquired=true;
    }
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout=XR_INFINITE_DURATION;
    // A failed wait must not be followed by Release. Retain the acquisition to retry.
    if (xrWaitSwapchainImage(stereoSwapchain,&wi)!=XR_SUCCESS) return false;
    const bool valid=p12AcquiredIndex<stereoImages.size() && stereoImages[p12AcquiredIndex].texture;
    if (valid) for (uint32_t eye=0;eye<2;++eye) {
        context->CopySubresourceRegion(stereoImages[p12AcquiredIndex].texture,
            D3D11CalcSubresource(0,eye,1),0,0,0,sourceColor[eye],0,nullptr);
        out[eye].pose=sourceEyes[eye]->view.pose;
        out[eye].fov=sourceEyes[eye]->view.fov;
        out[eye].subImage.swapchain=stereoSwapchain;
        out[eye].subImage.imageRect={{0,0},{static_cast<int32_t>(width),static_cast<int32_t>(height)}};
        out[eye].subImage.imageArrayIndex=eye;
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult release=xrReleaseSwapchainImage(stereoSwapchain,&ri);
    if (release!=XR_SUCCESS) { Log("P12 eye swapchain release failed: %d",static_cast<int>(release)); failed=true; return false; }
    p12StereoAcquired=false;
    if (valid && (!p12PairLogged || (p12CapturedCount[0]+p12CapturedCount[1])%600==0)) {
        p12PairLogged=true;
        Log("P12 AER PAIR READY: pair=%llu L frame=%llu R frame=%llu captures=%llu/%llu. Shared anchored HMD pose; separate eye origins/FOV; full source rect.",
            static_cast<unsigned long long>(sourceEyes[0]->pair),
            static_cast<unsigned long long>(sourceEyes[0]->frame),static_cast<unsigned long long>(sourceEyes[1]->frame),
            static_cast<unsigned long long>(p12CapturedCount[0]),static_cast<unsigned long long>(p12CapturedCount[1]));
    }
    return valid;
}

bool P12CopySameFrame(XrCompositionLayerProjectionView* out, XrTime displayTime, XrDuration period) {
    if(!p12SameFrameStereo||!out||!p12CaptureColor[0]||!p12Eyes[0].valid||!p12MonoViewValid)return false;
    const uint64_t now=g_probeFrameCounter.load();
    if(now<p12Eyes[0].frame||now-p12Eyes[0].frame>1||
       p12Eyes[0].generation!=g_p11cDepthGeneration.load()||
       (period>0&&std::llabs(displayTime-p12Eyes[0].time)>period*4))return false;

    // TS1 obtains two eye images from the same completed game frame and its
    // scene depth.  It deliberately retains the shared valid pose/FOV used by
    // PB6; disparity lives in the pixels, avoiding the old whole-scene offset.
    if(p12SameFrameStereo==2){
        // Keep PB6's exact full-frame framing.  TS1 changes only binocular
        // disparity, never crop, zoom, FOV, or the proven headset coverage.
        const float targetAspect=height?static_cast<float>(width)/static_cast<float>(height):1.0f;
        if(AcquireAndRenderDepthStereo(p12CaptureColor[0],targetAspect,width)){
            for(uint32_t eye=0;eye<2;++eye){
                out[eye].pose=p12MonoView.pose;
                out[eye].fov=p12MonoView.fov;
                out[eye].subImage.swapchain=stereoSwapchain;
                out[eye].subImage.imageRect={{0,0},{static_cast<int32_t>(width),static_cast<int32_t>(height)}};
                out[eye].subImage.imageArrayIndex=eye;
            }
            static bool depthLogged=false;
            if(!depthLogged){
                depthLogged=true;
                Log("TS1 SAME-FRAME DEPTH STEREO ACTIVE: both gameplay eyes synthesized from one simulation frame and real scene depth; menus/UI unchanged.");
            }
            return true;
        }
        static bool fallbackLogged=false;
        if(!fallbackLogged){
            fallbackLogged=true;
            Log("TS1 depth unavailable; using PB6 clean mono fallback for this and subsequent unavailable frames. No fake eye shift.");
        }
    }

    if(!p12StereoAcquired){
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(xrAcquireSwapchainImage(stereoSwapchain,&ai,&p12AcquiredIndex)!=XR_SUCCESS)return false;
        p12StereoAcquired=true;
    }
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(xrWaitSwapchainImage(stereoSwapchain,&wi)!=XR_SUCCESS)return false;
    bool valid=p12AcquiredIndex<stereoImages.size()&&stereoImages[p12AcquiredIndex].texture;
    if(valid){
        D3D11_TEXTURE2D_DESC src{},dst{};
        p12CaptureColor[0]->GetDesc(&src);
        stereoImages[p12AcquiredIndex].texture->GetDesc(&dst);
        valid=src.Width==dst.Width&&src.Height==dst.Height&&
            P13CompatibleColorFormat(src.Format,dst.Format)&&
            src.ArraySize==1&&src.MipLevels==1&&src.SampleDesc.Count==1&&dst.ArraySize>=2&&
            dst.MipLevels==1&&dst.SampleDesc.Count==1;
        if(valid)for(uint32_t eye=0;eye<2;++eye)
            context->CopySubresourceRegion(stereoImages[p12AcquiredIndex].texture,
                D3D11CalcSubresource(0,eye,1),0,0,0,p12CaptureColor[0],0,nullptr);
    }
    for(uint32_t eye=0;valid&&eye<2;++eye){
        // Both views intentionally use the same native pose/FOV because both
        // slices contain the same monoscopic game image. HMD movement is
        // already present in the renderer's live camera constants.
        out[eye].pose=p12MonoView.pose;
        out[eye].fov=p12MonoView.fov;
        out[eye].subImage.swapchain=stereoSwapchain;
        out[eye].subImage.imageRect={{0,0},{static_cast<int32_t>(width),static_cast<int32_t>(height)}};
        out[eye].subImage.imageArrayIndex=eye;
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult release=xrReleaseSwapchainImage(stereoSwapchain,&ri);
    if(!valid||release!=XR_SUCCESS)p46LastGameplayValid=false;
    p12StereoAcquired=false;
    if(release!=XR_SUCCESS){
        Log("P44 same-frame eye swapchain release failed: %d",static_cast<int>(release));
        failed=true;return false;
    }
    static bool logged=false;
    if(valid&&!logged){
        logged=true;
        Log("PB6 CLEAN MONO PATH ACTIVE: one captured gameplay frame copied to both eye slices; one shared native OpenXR pose/FOV; no alternating eye history.");
    }
    return valid;
}

// PF18 fail-visible compatibility path. If the strict renderer/camera match
// rejects several consecutive gameplay frames, submit the completed native
// backbuffer as a full-view OpenXR projection. This can never route gameplay
// through the menu panel and deliberately does not invent stereo disparity.
bool P12CopyCompatibilityFrame(ID3D11Texture2D* backbuffer,
    XrCompositionLayerProjectionView* out,XrTime displayTime) {
    if(!backbuffer||!out||!xrLocateViews||stereoSwapchain==XR_NULL_HANDLE||
       localSpace==XR_NULL_HANDLE||session==XR_NULL_HANDLE)return false;

    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    XrViewState state{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=displayTime;locate.space=localSpace;
    uint32_t viewCount=0;
    const XrResult located=xrLocateViews(session,&locate,&state,2,&viewCount,views);
    const XrViewStateFlags required=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(XR_FAILED(located)||viewCount!=2||(state.viewStateFlags&required)!=required||
       !P12ValidPose(views[0].pose)||!P12ValidPose(views[1].pose))return false;

    // Use the midpoint pose and the union of both runtime FOVs. Both slices
    // contain identical pixels, so submitting two different eye poses would
    // create false geometry and discomfort.
    XrView shared{XR_TYPE_VIEW};
    shared.pose=views[0].pose;
    shared.pose.position.x=(views[0].pose.position.x+views[1].pose.position.x)*0.5f;
    shared.pose.position.y=(views[0].pose.position.y+views[1].pose.position.y)*0.5f;
    shared.pose.position.z=(views[0].pose.position.z+views[1].pose.position.z)*0.5f;
    shared.fov.angleLeft=std::min(views[0].fov.angleLeft,views[1].fov.angleLeft);
    shared.fov.angleRight=std::max(views[0].fov.angleRight,views[1].fov.angleRight);
    shared.fov.angleUp=std::max(views[0].fov.angleUp,views[1].fov.angleUp);
    shared.fov.angleDown=std::min(views[0].fov.angleDown,views[1].fov.angleDown);

    D3D11_TEXTURE2D_DESC source{},destination{};backbuffer->GetDesc(&source);
    if(stereoImages.empty()||!stereoImages[0].texture)return false;
    stereoImages[0].texture->GetDesc(&destination);
    if(source.Width!=destination.Width||source.Height!=destination.Height||
       !P13CompatibleColorFormat(source.Format,destination.Format)||source.ArraySize!=1||
       source.MipLevels!=1||destination.ArraySize<2||destination.MipLevels!=1||
       destination.SampleDesc.Count!=1)return false;

    if(!p12StereoAcquired){
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(xrAcquireSwapchainImage(stereoSwapchain,&acquire,&p12AcquiredIndex)!=XR_SUCCESS)return false;
        p12StereoAcquired=true;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(xrWaitSwapchainImage(stereoSwapchain,&wait)!=XR_SUCCESS)return false;
    bool valid=p12AcquiredIndex<stereoImages.size()&&stereoImages[p12AcquiredIndex].texture;
    if(valid){
        for(uint32_t eye=0;eye<2;++eye){
            const UINT subresource=D3D11CalcSubresource(0,eye,1);
            if(source.SampleDesc.Count>1)
                context->ResolveSubresource(stereoImages[p12AcquiredIndex].texture,subresource,backbuffer,0,source.Format);
            else
                context->CopySubresourceRegion(stereoImages[p12AcquiredIndex].texture,subresource,0,0,0,backbuffer,0,nullptr);
            out[eye].pose=shared.pose;out[eye].fov=shared.fov;
            out[eye].subImage.swapchain=stereoSwapchain;
            out[eye].subImage.imageRect={{0,0},{static_cast<int32_t>(width),static_cast<int32_t>(height)}};
            out[eye].subImage.imageArrayIndex=eye;
        }
    }
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult released=xrReleaseSwapchainImage(stereoSwapchain,&releaseInfo);
    p12StereoAcquired=false;
    if(XR_FAILED(released)){failed=true;return false;}
    if(valid&&!p12CompatibilityLogged){
        p12CompatibilityLogged=true;
        Log("PF18 FAIL-VISIBLE GAMEPLAY ACTIVE: strict matched capture timed out; native backbuffer submitted as a full-view OpenXR projection, never a 2D panel.");
    }
    return valid;
}

bool P12RefreshSize(ID3D11Texture2D* backbuffer) {
    D3D11_TEXTURE2D_DESC td{}; backbuffer->GetDesc(&td);
    if (td.Width==width && td.Height==height && td.Format==format) return true;
    P12ReleaseEyes(); ReleaseStereoResources(); P13ReleaseUi();
    if(p46ScreenAcquire.held){failed=true;Log("P46 resize with world-screen image acquired; restart required");return false;}
    if(p46ScreenSwapchain)xrDestroySwapchain(p46ScreenSwapchain);
    p46ScreenSwapchain=XR_NULL_HANDLE;p46ScreenImages.clear();p46Anchored=false;p46LastScreenValid=false;
    if(p13HudAcquire.held||p13FlatAcquire.held){failed=true;return false;}
    if(p13HudSwapchain)xrDestroySwapchain(p13HudSwapchain);
    p13HudSwapchain=XR_NULL_HANDLE;p13HudImages.clear();
    if (p12StereoAcquired) { failed=true; Log("P12 resize while XR image acquired; restart required."); return false; }
    if (flatSwapchain) xrDestroySwapchain(flatSwapchain);
    if (stereoSwapchain) xrDestroySwapchain(stereoSwapchain);
    flatSwapchain=stereoSwapchain=XR_NULL_HANDLE;
    p36FlatEverReady=false;
    flatImages.clear(); stereoImages.clear();
    width=td.Width; height=td.Height; format=td.Format;
    g_expectedDepthWidth.store(width); g_expectedDepthHeight.store(height);
    g_p11cPipelineRebuildRequested.store(true);
    if (!CreateColorSwapchain(1,flatSwapchain,flatImages,"P12 resized flat") ||
        !CreateColorSwapchain(2,stereoSwapchain,stereoImages,"P12 resized AER")) {
        failed=true; return false;
    }
    Log("P12 resolution changed: %ux%u, discarded both eye histories.",width,height);
    return true;
}
