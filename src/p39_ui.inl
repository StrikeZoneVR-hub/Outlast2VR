// Lightweight VR camera HUD. Pickup UI is deliberately absent here: P43
// publishes pickup state to Outlast's own AvailableInteractions array, so only
// the game's native prompt may announce a pickup.
// Included as OpenXRQuad members after p37_gui.inl, so it reuses the tiny
// pixel-font helpers without enabling P13/P14 generic HUD extraction.

XrSwapchain p39CameraSwapchain=XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> p39CameraImages;
std::vector<uint32_t> p39CameraUploaded;

bool P39EnsureStaticSwapchain(
    XrSwapchain& swapchain,
    std::vector<XrSwapchainImageD3D11KHR>& images,
    std::vector<uint32_t>& uploaded,
    uint32_t w,uint32_t h){

    if(swapchain!=XR_NULL_HANDLE)return true;
    if(!sessionRunning)return false;

    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    ci.sampleCount=1;ci.width=w;ci.height=h;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;

    if(XR_FAILED(xrCreateSwapchain(session,&ci,&swapchain)))return false;

    uint32_t n=0;
    if(XR_FAILED(xrEnumerateSwapchainImages(swapchain,0,&n,nullptr))||!n)return false;
    images.assign(n,XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(xrEnumerateSwapchainImages(
        swapchain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))))return false;
    uploaded.assign(n,0);
    return true;
}

static std::vector<uint32_t> P39CameraPixels(float energy,bool batteryValid){
    const int w=512,h=128;
    std::vector<uint32_t> px(size_t(w)*h,0x00000000u);
    const uint32_t white=0xFFFFFFFFu;
    const uint32_t soft=0xD0FFFFFFu;
    const uint32_t dim=0x705A5A5Au;

    // Real Outlast 2 battery energy. p35runtime discovers the reflected
    // BatteryEnergy-like FloatProperty on the live Hero/controller/camcorder
    // and reads the game's own value. Reloading in Outlast immediately raises
    // this value again; NV drain lowers it naturally.
    const int bx=270,by=24,bw=190,bh=58;
    P37Fill(px,w,h,bx,by,bx+bw,by+5,white);
    P37Fill(px,w,h,bx,by+bh-5,bx+bw,by+bh,white);
    P37Fill(px,w,h,bx,by,bx+5,by+bh,white);
    P37Fill(px,w,h,bx+bw-5,by,bx+bw,by+bh,white);
    P37Fill(px,w,h,bx+bw,by+16,bx+bw+12,by+36,white);

    const int innerX=bx+12,innerY=by+12,innerW=bw-24,innerH=bh-24;
    P37Fill(px,w,h,innerX,innerY,innerX+innerW,innerY+innerH,dim);

    if(batteryValid){
        const int fill=std::clamp(
            static_cast<int>(std::lround(innerW*std::clamp(energy,0.0f,1.0f))),
            0,innerW);
        if(fill>0)P37Fill(px,w,h,innerX,innerY,innerX+fill,innerY+innerH,soft);
    }

    P37Text(px,w,h,274,94,"BATTERY",2,white);
    return px;
}

uint32_t p39CameraVersion=1;
int p39LastBatteryPercent=-999;
bool p39LastBatteryValid=false;
std::vector<uint32_t> p39CameraPixels;

bool P39RenderCameraHud(){
    if(!gameplayVr||p37GuiOpen||!g_p37CameraActive.load(std::memory_order_relaxed))return false;
    if(!P39EnsureStaticSwapchain(p39CameraSwapchain,p39CameraImages,p39CameraUploaded,512,128))return false;

    const bool batteryValid=p35runtime::BatteryValid();
    const float energy=p35runtime::BatteryEnergy();
    const int percent=batteryValid
        ?std::clamp(static_cast<int>(std::lround(energy*100.0f)),0,100)
        :-1;

    if(p39CameraPixels.empty()||
       percent!=p39LastBatteryPercent||
       batteryValid!=p39LastBatteryValid){
        p39LastBatteryPercent=percent;
        p39LastBatteryValid=batteryValid;
        ++p39CameraVersion;
        if(!p39CameraVersion)++p39CameraVersion;
        p39CameraPixels=P39CameraPixels(energy,batteryValid);
    }

    uint32_t index=0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(xrAcquireSwapchainImage(p39CameraSwapchain,&ai,&index)))return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(xrWaitSwapchainImage(p39CameraSwapchain,&wi)))return false;

    if(index<p39CameraImages.size()&&p39CameraImages[index].texture&&
       p39CameraUploaded[index]!=p39CameraVersion){
        context->UpdateSubresource(
            p39CameraImages[index].texture,0,nullptr,
            p39CameraPixels.data(),512*4,0);
        p39CameraUploaded[index]=p39CameraVersion;
    }

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(p39CameraSwapchain,&ri);
    return true;
}

void P39CameraHudQuad(XrCompositionLayerQuad& q){
    q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q.space=viewSpace;q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain=p39CameraSwapchain;
    q.subImage.imageRect={{0,0},{512,128}};
    q.subImage.imageArrayIndex=0;
    q.pose.orientation={0,0,0,1};
    q.pose.position={0.34f,0.22f,-1.45f};
    q.size={0.42f,0.105f};
}
