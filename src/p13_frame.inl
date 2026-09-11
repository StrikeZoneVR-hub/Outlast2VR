// OpenXR frame interval encloses the game's render: wait/locate before it,
// submit after it. No guessed next-frame pose is substituted for rendered data.
XrFrameState p13Frame{XR_TYPE_FRAME_STATE};
bool p13FrameBegun=false;
XrSwapchain p13HudSwapchain=XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> p13HudImages;
struct P13Acquire { bool held=false;uint32_t index=0; };
P13Acquire p13FlatAcquire,p13HudAcquire;
bool p13UiSubmittedLogged=false;
bool p13ReticleMaskLogged=false;
uint32_t p13HudWidth=0,p13HudHeight=0;
DXGI_FORMAT p13HudFormat=DXGI_FORMAT_UNKNOWN;
uint64_t p45SubmitDiagnostics=0;
#include "p46_world_screen.inl"
uint32_t p46MissingSceneFrames=0;
int p46LastSubmitMode=-1;
bool p46LastGameplayRequest=false;
void P46LogSettings(){
    wchar_t path[32768]{};const DWORD length=GetEnvironmentVariableW(L"OUTLAST2VR_SETTINGS_PATH",path,32768);
    if(!length||length>=32768){Log("P46 SETTINGS: active user path not supplied; launch through the updated Outlast2VR.exe to apply graphics safety overrides");return;}
    const wchar_t* keys[]={L"MotionBlur",L"bAllowTemporalAA",L"bAllowPostprocessFXAA",L"AmbientOcclusion",L"UseVsync",L"ScreenPercentage"};
    for(auto key:keys){wchar_t value[64]{};GetPrivateProfileStringW(L"SystemSettings",key,L"missing",value,64,path);
        Log("P46 SETTINGS ON DISK (not live GPU state): %ls=%ls",key,value);}
}

const char* P45LayerType(const XrCompositionLayerBaseHeader* layer){
    if(!layer)return "none";
    switch(layer->type){
        case XR_TYPE_COMPOSITION_LAYER_PROJECTION:return "projection";
        case XR_TYPE_COMPOSITION_LAYER_QUAD:return "quad";
        default:return "other";
    }
}

static D3D11_RECT P13CenterReticleRect(uint32_t imageWidth,uint32_t imageHeight){
    if(!imageWidth||!imageHeight)return {0,0,0,0};
    const auto shortest=static_cast<float>(std::min(imageWidth,imageHeight));
    const LONG half=static_cast<LONG>(std::clamp(std::lround(shortest*(12.0f/1080.0f)),6l,32l));
    const LONG cx=static_cast<LONG>(imageWidth/2),cy=static_cast<LONG>(imageHeight/2);
    return {std::max<LONG>(0,cx-half),std::max<LONG>(0,cy-half),
        std::min<LONG>(static_cast<LONG>(imageWidth),cx+half),
        std::min<LONG>(static_cast<LONG>(imageHeight),cy+half)};
}

bool P13ClearCenterReticle(ID3D11Texture2D* target){
    if(!target||!context)return false;
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);
    const auto rect=P13CenterReticleRect(desc.Width,desc.Height);
    if(rect.right<=rect.left||rect.bottom<=rect.top)return false;
    ID3D11Device* device=nullptr;target->GetDevice(&device);
    if(!device)return false;
    ID3D11RenderTargetView* view=nullptr;
    const HRESULT viewResult=device->CreateRenderTargetView(target,nullptr,&view);
    device->Release();
    if(FAILED(viewResult)||!view)return false;
    ID3D11DeviceContext1* context1=nullptr;
    const HRESULT contextResult=context->QueryInterface(__uuidof(ID3D11DeviceContext1),
        reinterpret_cast<void**>(&context1));
    if(FAILED(contextResult)||!context1){view->Release();return false;}
    const float transparent[4]{};
    context1->ClearView(view,transparent,&rect,1);
    context1->Release();view->Release();
    return true;
}

bool P13CopyTexture(XrSwapchain swapchain,const std::vector<XrSwapchainImageD3D11KHR>& images,
    ID3D11Texture2D* source,P13Acquire& acquired,bool clearCenterReticle=false) {
    if(!source||swapchain==XR_NULL_HANDLE)return false;
    if(!acquired.held){
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(xrAcquireSwapchainImage(swapchain,&ai,&acquired.index)!=XR_SUCCESS)return false;
        acquired.held=true;
    }
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(xrWaitSwapchainImage(swapchain,&wi)!=XR_SUCCESS)return false;
    bool valid=acquired.index<images.size()&&images[acquired.index].texture;
    if(valid){
        D3D11_TEXTURE2D_DESC src{},dst{};source->GetDesc(&src);images[acquired.index].texture->GetDesc(&dst);
        valid=src.Width==dst.Width&&src.Height==dst.Height&&
            P13CompatibleColorFormat(src.Format,dst.Format)&&src.ArraySize==1;
        if(valid){
            if(src.SampleDesc.Count>1)context->ResolveSubresource(images[acquired.index].texture,0,source,0,src.Format);
            else context->CopySubresourceRegion(images[acquired.index].texture,0,0,0,0,source,0,nullptr);
            if(clearCenterReticle){
                const bool cleared=P13ClearCenterReticle(images[acquired.index].texture);
                if(!p13ReticleMaskLogged){
                    p13ReticleMaskLogged=true;
                    Log(cleared
                        ?"PF4 HUD RETICLE MASK READY: only the 24px-at-1080p center dot region is transparent; prompts and menus remain intact."
                        :"PF4 HUD RETICLE MASK unavailable: D3D11.1 ClearView/RTV validation failed; HUD remains unmodified.");
                }
            }
        }
    }
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(xrReleaseSwapchainImage(swapchain,&ri)!=XR_SUCCESS){failed=true;return false;}
    acquired.held=false;return valid;
}

void P13Quad(XrCompositionLayerQuad& quad,XrSwapchain swapchain,bool transparent) {
    quad.layerFlags=transparent?XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT:0;
    quad.space=viewSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain=swapchain;
    const uint32_t imageW=transparent&&p13HudWidth?p13HudWidth:width;
    const uint32_t imageH=transparent&&p13HudHeight?p13HudHeight:height;
    quad.subImage.imageRect={{0,0},{static_cast<int32_t>(imageW),static_cast<int32_t>(imageH)}};
    quad.subImage.imageArrayIndex=0;
    float distance=2.0f,widthMeters=3.2f;
    if(transparent){
        // P34: smaller readable CameraHudPro layer. Only the VIEW-space HUD is
        // resized; game/world stereo is unchanged.
        static const auto config=ModuleDir()+L"\\outlast2_vr_p32.ini";
        wchar_t scaleText[32]{},distanceText[32]{};
        GetPrivateProfileStringW(L"VR",L"HudScale",L"0.62",scaleText,32,config.c_str());
        GetPrivateProfileStringW(L"VR",L"HudDistance",L"1.65",distanceText,32,config.c_str());
        wchar_t* end=nullptr;float scale=std::wcstof(scaleText,&end);
        if(end==scaleText)scale=0.62f;scale=p32::ClampHudScale(scale);
        end=nullptr;distance=std::wcstof(distanceText,&end);
        if(end==distanceText)distance=1.65f;distance=p32::ClampHudDistance(distance);
        widthMeters=3.2f*scale;
    }else{
        // Keep startup/menu interaction on the original mono quad path, but
        // make the complete settings screen fit comfortably in view.
        static const auto config=ModuleDir()+L"\\outlast2_vr_p32.ini";
        wchar_t scaleText[32]{},distanceText[32]{};
        GetPrivateProfileStringW(L"VR",L"MenuScale",L"0.18",scaleText,32,config.c_str());
        GetPrivateProfileStringW(L"VR",L"MenuDistance",L"2.20",distanceText,32,config.c_str());
        wchar_t* end=nullptr;float scale=std::wcstof(scaleText,&end);
        if(end==scaleText||!std::isfinite(scale))scale=0.18f;
        scale=std::clamp(scale,0.14f,0.70f);
        end=nullptr;distance=std::wcstof(distanceText,&end);
        if(end==distanceText||!std::isfinite(distance))distance=2.20f;
        distance=std::clamp(distance,1.00f,3.50f);
        widthMeters=3.2f*scale;
    }
    quad.pose.orientation={0,0,0,1};quad.pose.position={0,0,-distance};
    quad.size={widthMeters,widthMeters*static_cast<float>(imageH)/std::max(1u,imageW)};
}

bool P13EnsureHudSwapchain(ID3D11Texture2D* source){
    if(!source||!sessionRunning)return false;
    D3D11_TEXTURE2D_DESC src{};source->GetDesc(&src);
    if(!src.Width||!src.Height||src.ArraySize!=1||src.SampleDesc.Count!=1)return false;
    if(p13HudSwapchain!=XR_NULL_HANDLE&&p13HudWidth==src.Width&&p13HudHeight==src.Height&&p13HudFormat==src.Format)return true;
    if(p13HudAcquire.held){
        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(p13HudSwapchain,&ri);p13HudAcquire.held=false;
    }
    if(p13HudSwapchain!=XR_NULL_HANDLE&&xrDestroySwapchain){
        xrDestroySwapchain(p13HudSwapchain);p13HudSwapchain=XR_NULL_HANDLE;p13HudImages.clear();
    }
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=static_cast<int64_t>(P46ColorFormat(src.Format));ci.sampleCount=1;ci.width=src.Width;ci.height=src.Height;
    ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
    XrResult xr=xrCreateSwapchain(session,&ci,&p13HudSwapchain);
    if(XR_FAILED(xr)){
        Log("P34 HUD swapchain creation failed for native %ux%u fmt=%u: %s (%d)",src.Width,src.Height,unsigned(src.Format),XrResultName(xr),int(xr));
        p13HudSwapchain=XR_NULL_HANDLE;return false;
    }
    uint32_t imageCount=0;
    xr=xrEnumerateSwapchainImages(p13HudSwapchain,0,&imageCount,nullptr);
    if(XR_FAILED(xr)||!imageCount){xrDestroySwapchain(p13HudSwapchain);p13HudSwapchain=XR_NULL_HANDLE;return false;}
    p13HudImages.assign(imageCount,XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    xr=xrEnumerateSwapchainImages(p13HudSwapchain,imageCount,&imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(p13HudImages.data()));
    if(XR_FAILED(xr)){xrDestroySwapchain(p13HudSwapchain);p13HudSwapchain=XR_NULL_HANDLE;p13HudImages.clear();return false;}
    p13HudWidth=src.Width;p13HudHeight=src.Height;p13HudFormat=src.Format;
    Log("P34 CAMERA HUD SWAPCHAIN READY: exact native size %ux%u fmt=%u images=%u.",src.Width,src.Height,unsigned(src.Format),imageCount);
    return true;
}

void P13DiscardOpenFrame(){
    if(!p13FrameBegun)return;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime=p13Frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    xrEndFrame(session,&end);p13FrameBegun=false;
}

void P13BeginNextFrame(bool applicationFocused=OutlastHasFocus()){
    if(p13FrameBegun)return;

    P13RepairContextHooks(context);
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    p13Frame={XR_TYPE_FRAME_STATE};
    if(xrWaitFrame(session,&wait,&p13Frame)!=XR_SUCCESS){P12Reset(false);P19ClearTracked();g_p13ExtractUi.store(false);return;}
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    const XrResult br=xrBeginFrame(session,&begin);
    if(XR_FAILED(br)){P12Reset(false);P19ClearTracked();g_p13ExtractUi.store(false);return;}
    p13FrameBegun=true;
    if(p13Frame.shouldRender){
        const double periodSeconds=p13Frame.predictedDisplayPeriod>0
            ?static_cast<double>(p13Frame.predictedDisplayPeriod)*1.0e-9:1.0/72.0;
        void* nativeController=g_nativeControllerSelf.load(std::memory_order_acquire);
        if(!nativeController)nativeController=g_nativeHeroSelf.load(std::memory_order_relaxed);
        // The view hook captures the same controller object used by the
        // validated movement path. Observe its pawn here so body/collision
        // helpers and the cinematic camera latch all share one owner.
        P20Observe(nativeController);
        P22UpdateHeadLock(nativeController);
        P22NativeRoomMove(nativeController,
            static_cast<float>(std::clamp(periodSeconds,0.001,0.1)));
        P12Prepare(p13Frame.predictedDisplayTime,applicationFocused);
        // MC1 samples both controllers against the exact display time and LOCAL
        // reference space used by this frame's head pose. Body IK is still off,
        // so this cannot alter the proven Fix 6 camera, body, or renderer.
        P19Locate(p13Frame.predictedDisplayTime,applicationFocused);
        // Isolate the normal crosshair and Scaleform UI only during stereo
        // gameplay. Menus retain the existing mono flat presentation path.
        g_p13ExtractUi.store(gameplayVr&&p12Pending.valid);
    }else{P12Reset(false);P19ClearTracked();g_p13ExtractUi.store(false);}
}

void P13Submit(IDXGISwapChain* swapchain){
    if(failed||!Init(swapchain)){P12Reset(false);g_p13ExtractUi.store(false);return;}
    PollEvents();
    P18Update();
    if(!sessionRunning){P12Reset(true);g_p13ExtractUi.store(false);return;}

    // P36: on Quest Link, begin a flat/menu frame immediately after the XR
    // session becomes ready so startup logos do not spend an extra Present with
    // no submitted layer. Gameplay keeps the original pre-render timing.
    if(!p13FrameBegun&&!gameplayVr)P13BeginNextFrame();

    ID3D11Texture2D* backbuffer=nullptr;
    if(FAILED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backbuffer)))){
        P13DiscardOpenFrame();P12Reset(false);g_p13ExtractUi.store(false);return;
    }
    g_p13Backbuffer.store(backbuffer);
    const bool sizeReady=P12RefreshSize(backbuffer);

    bool sceneReadyForSubmit=false,gameplaySubmitted=false;
    if(p13FrameBegun){
        const XrCompositionLayerBaseHeader* layers[4]{};uint32_t count=0;
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView eyes[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};

        if(sizeReady&&!failed&&p13Frame.shouldRender){
            const uint64_t renderFrame=g_probeFrameCounter.load()-1;

            // Native menu/pause/loading signals override cursor heuristics.
            // Observe original scene bindings independently of camera patching,
            // so a failed patch is not misclassified as a loading screen.
            const bool matchedMainScene=g_p12BoundFrame.load(std::memory_order_acquire)==renderFrame;
            const bool sceneObserved=g_p46SceneFrame.load(std::memory_order_acquire)==renderFrame||matchedMainScene;
            const bool gameplayRequested=gameplayVr&&!p46ui::Screen();
            sceneReadyForSubmit=p46ui::SceneReady(gameplayRequested,sceneObserved,p46MissingSceneFrames);
            if(gameplayRequested!=p46LastGameplayRequest){
                Log("P46 GAMEPLAY REQUEST: active=%d scene=%d missing=%u cameraValidated=%d prepared=%d bound=%llu",
                    gameplayRequested?1:0,sceneObserved?1:0,p46MissingSceneFrames,
                    g_p10ViewProjectionValidated.load()?1:0,p12Pending.valid?1:0,
                    static_cast<unsigned long long>(g_p12BoundFrame.load()));
                p46LastGameplayRequest=gameplayRequested;
            }
            const bool gameplayScene=sceneReadyForSubmit;
            if(gameplayScene){
                p46Anchored=false;
                if(P12CaptureEye(backbuffer)&&
                   (p12SameFrameStereo
                    ? P12CopySameFrame(eyes,p13Frame.predictedDisplayTime,p13Frame.predictedDisplayPeriod)
                    : P12CopyPair(eyes,p13Frame.predictedDisplayTime,p13Frame.predictedDisplayPeriod))){
                    projection.space=localSpace;projection.viewCount=2;projection.views=eyes;
                    layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                    gameplaySubmitted=true;
                    std::copy(eyes,eyes+2,p46LastGameplay);p46LastGameplayTime=p13Frame.predictedDisplayTime;p46LastGameplayValid=true;
                }else if(p46LastGameplayValid&&p13Frame.predictedDisplayTime>=p46LastGameplayTime&&
                         p13Frame.predictedDisplayTime-p46LastGameplayTime<=250000000){
                    // Hold pixels with the original pose/FOV, never a newer pose.
                    std::copy(p46LastGameplay,p46LastGameplay+2,eyes);
                    projection.space=localSpace;projection.viewCount=2;projection.views=eyes;
                    layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                    gameplaySubmitted=true;
                }
            }

            // Non-gameplay content is rendered onto a world-anchored surface.
            // Once native gameplay begins, never run the world-screen D3D draw
            // again: its context-state swap prevented the known-good Beta
            // renderer hooks from observing an uninterrupted gameplay frame.
            // Reuse only the last released menu/loading image during the short
            // camera handoff; a level is never copied onto this surface.
            if(!count){
                p46LastGameplayValid=false;
                if(!gameplayRequested){
                    P12Reset(false);
                    // On a transition, put extracted text back into the screen.
                    ID3D11Texture2D* menuUi=g_p13UiFrame==renderFrame?g_p13UiTexture:nullptr;
                    if(P46RenderScreen(backbuffer,menuUi,eyes)){
                        projection.space=localSpace;projection.viewCount=2;projection.views=eyes;
                        layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                    }
                }else if(p46LastScreenValid){
                    std::copy(p46LastScreen,p46LastScreen+2,eyes);
                    projection.space=localSpace;projection.viewCount=2;projection.views=eyes;
                    layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                }
            }

            // Submit the extracted crosshair/Scaleform UI once in VIEW space
            // with BOTH-eye visibility. It is no longer baked at two eye depths.
            // While the camcorder is raised, suppress its oversized native
            // fullscreen overlay and submit only PB1's compact battery layer.
            const bool compactCameraHud=g_p37CameraActive.load(std::memory_order_relaxed);
            if(gameplaySubmitted&&!compactCameraHud&&count>0&&count<4&&g_p13UiFrame==renderFrame&&g_p13UiTexture&&
                P13EnsureHudSwapchain(g_p13UiTexture)&&
                P13CopyTexture(p13HudSwapchain,p13HudImages,g_p13UiTexture,p13HudAcquire,true)){
                static XrCompositionLayerQuad sharedUi{XR_TYPE_COMPOSITION_LAYER_QUAD};
                P13Quad(sharedUi,p13HudSwapchain,true);
                layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&sharedUi);
                if(!p13UiSubmittedLogged){p13UiSubmittedLogged=true;Log("P44 MONO UI READY: crosshair/Scaleform texture submitted once to BOTH eyes in VIEW space.");}
            }

            // P39: custom camera HUD only. No native CameraHudPro reroute,
            // no render-target inspection, and no second copy of the flat HUD.
            if(gameplaySubmitted&&count>0&&count<4&&P39RenderCameraHud()){
                static XrCompositionLayerQuad cameraHud{XR_TYPE_COMPOSITION_LAYER_QUAD};
                P39CameraHudQuad(cameraHud);
                layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cameraHud);
            }

            // VR configuration menu is a tiny independent VIEW-space quad.
            if(p37GuiOpen&&count<4&&P37RenderGui()){
                static XrCompositionLayerQuad gui{XR_TYPE_COMPOSITION_LAYER_QUAD};
                P37GuiQuad(gui);
                layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&gui);
            }
        }

        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime=p13Frame.predictedDisplayTime;
        end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=count;end.layers=count?layers:nullptr;
        const uint64_t diagnosticFrame=p45SubmitDiagnostics++;
        if(diagnosticFrame%900==0)P46LogSettings();
        const int submitMode=!count?0:gameplaySubmitted?1:2;
        if(diagnosticFrame<12||diagnosticFrame%900==0||submitMode!=p46LastSubmitMode){
            Log("P45 XR SUBMIT: frame=%llu mode=%s layers=%u l0=%s l1=%s l2=%s l3=%s",
                static_cast<unsigned long long>(diagnosticFrame),gameplaySubmitted?"gameplay":"world-screen",count,
                P45LayerType(count>0?layers[0]:nullptr),P45LayerType(count>1?layers[1]:nullptr),
                P45LayerType(count>2?layers[2]:nullptr),P45LayerType(count>3?layers[3]:nullptr));
        }
        if(submitMode!=p46LastSubmitMode)Log("P46 ROUTING: reason=%s missingSceneFrames=%u prepared=%d bound=%llu scene=%llu nativeView=%llu",
            p46ui::Reason(),p46MissingSceneFrames,p12Pending.valid?1:0,
            static_cast<unsigned long long>(g_p12BoundFrame.load()),static_cast<unsigned long long>(g_p46SceneFrame.load()),static_cast<unsigned long long>(g_p46NativeViewFrame.load()));
        p46LastSubmitMode=submitMode;
        const XrResult er=xrEndFrame(session,&end);p13FrameBegun=false;
        if(XR_FAILED(er)){Log("P36 xrEndFrame failed: %d",static_cast<int>(er));p46LastGameplayValid=false;P12Reset(false);}
    }

    backbuffer->Release();
    g_p13ExtractUi.store(false);
    if(!failed){UpdateAutomaticPresentationMode();P13BeginNextFrame();}
    else P12Reset(false);
}
