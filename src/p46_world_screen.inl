// Included in OpenXRQuad after p13Acquire is declared.
p46::ScreenRenderer p46Renderer;
XrSwapchain p46ScreenSwapchain=XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> p46ScreenImages;
P13Acquire p46ScreenAcquire;
XrPosef p46Anchor{{0,0,0,1},{0,0,-2.2f}};
bool p46Anchored=false,p46RecenterDown=false;
uint32_t p46ScreenWidth=0,p46ScreenHeight=0;
bool p46LastGameplayValid=false;
XrTime p46LastGameplayTime=0;
XrCompositionLayerProjectionView p46LastGameplay[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
bool p46LastScreenValid=false;
XrCompositionLayerProjectionView p46LastScreen[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};

bool P46EnsureScreen(){
    if(p46ScreenSwapchain)return true;
    const HRESULT init=p46Renderer.Init(device);
    if(FAILED(init)){
        static bool logged=false;if(!logged){logged=true;Log("P46 world-screen D3D initialization failed: stage=%s hr=0x%08X deviceFlags=0x%X",
            p46Renderer.lastStage,unsigned(init),unsigned(device->GetCreationFlags()));}
        return false;
    }
    static bool rendererLogged=false;
    if(!rendererLogged){rendererLogged=true;Log("P46 world-screen renderer compatible: context-state isolation active, deviceFlags=0x%X",unsigned(device->GetCreationFlags()));}
    uint32_t count=0;
    XrViewConfigurationView recommended[2]={{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    if(XR_FAILED(xrEnumerateViewConfigurationViews(instance,systemId,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&count,recommended))||count!=2)return false;
    p46ScreenWidth=std::max(recommended[0].recommendedImageRectWidth,recommended[1].recommendedImageRectWidth);
    p46ScreenHeight=std::max(recommended[0].recommendedImageRectHeight,recommended[1].recommendedImageRectHeight);
    p46ScreenWidth=std::min(p46ScreenWidth,std::min(recommended[0].maxImageRectWidth,recommended[1].maxImageRectWidth));
    p46ScreenHeight=std::min(p46ScreenHeight,std::min(recommended[0].maxImageRectHeight,recommended[1].maxImageRectHeight));
    if(!p46ScreenWidth||!p46ScreenHeight)return false;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format=P46ColorFormat(format);ci.sampleCount=ci.faceCount=ci.mipCount=1;ci.arraySize=2;ci.width=p46ScreenWidth;ci.height=p46ScreenHeight;
    if(XR_FAILED(xrCreateSwapchain(session,&ci,&p46ScreenSwapchain)))return false;
    if(XR_FAILED(xrEnumerateSwapchainImages(p46ScreenSwapchain,0,&count,nullptr))||!count){xrDestroySwapchain(p46ScreenSwapchain);p46ScreenSwapchain=XR_NULL_HANDLE;return false;}
    p46ScreenImages.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(xrEnumerateSwapchainImages(p46ScreenSwapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(p46ScreenImages.data())))){
        xrDestroySwapchain(p46ScreenSwapchain);p46ScreenSwapchain=XR_NULL_HANDLE;p46ScreenImages.clear();return false;
    }
    Log("P46 WORLD SCREEN READY: %ux%u per eye; LOCAL anchored, projection layer, complete source aspect; gameplay target unchanged",p46ScreenWidth,p46ScreenHeight);return true;
}

bool P46RenderScreen(ID3D11Texture2D* source,ID3D11Texture2D* ui,XrCompositionLayerProjectionView* out){
    if(!P46EnsureScreen())return false;
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};XrViewState state{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};li.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;li.displayTime=p13Frame.predictedDisplayTime;li.space=localSpace;
    uint32_t count=0;
    constexpr auto flags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if(XR_FAILED(xrLocateViews(session,&li,&state,2,&count,views))||count!=2||(state.viewStateFlags&flags)!=flags||!P12ValidPose(views[0].pose)||!P12ValidPose(views[1].pose))return false;
    const bool recenter=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    if(recenter&&!p46RecenterDown)p46Anchored=false;p46RecenterDown=recenter;
    if(p12ReferenceChangeTime&&li.displayTime>=p12ReferenceChangeTime){p46Anchored=false;P12Reset(true);p12ReferenceChangeTime=0;}
    if(!p46Anchored){
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        constexpr auto headFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if(XR_FAILED(xrLocateSpace(viewSpace,localSpace,li.displayTime,&head))||(head.locationFlags&headFlags)!=headFlags||!P12ValidPose(head.pose))return false;
        p46Anchor=p46::Anchor(head.pose,2.2f);p46Anchored=true;
    }
    if(!p46ScreenAcquire.held){XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(xrAcquireSwapchainImage(p46ScreenSwapchain,&ai,&p46ScreenAcquire.index)!=XR_SUCCESS)return false;p46ScreenAcquire.held=true;}
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(xrWaitSwapchainImage(p46ScreenSwapchain,&wi)!=XR_SUCCESS)return false;
    const bool rendered=p46ScreenAcquire.index<p46ScreenImages.size()&&p46Renderer.Render(context,source,ui,
        p46ScreenImages[p46ScreenAcquire.index].texture,P46ColorFormat(format),views,p46Anchor,2.4f);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const auto released=xrReleaseSwapchainImage(p46ScreenSwapchain,&ri);
    if(released!=XR_SUCCESS){failed=true;return false;}p46ScreenAcquire.held=false;
    if(!rendered)return false;
    for(int i=0;i<2;++i){out[i]={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};out[i].pose=views[i].pose;out[i].fov=views[i].fov;
        out[i].subImage.swapchain=p46ScreenSwapchain;out[i].subImage.imageArrayIndex=i;
        out[i].subImage.imageRect={{0,0},{static_cast<int32_t>(p46ScreenWidth),static_cast<int32_t>(p46ScreenHeight)}};}
    std::copy(out,out+2,p46LastScreen);p46LastScreenValid=true;
    return true;
}
