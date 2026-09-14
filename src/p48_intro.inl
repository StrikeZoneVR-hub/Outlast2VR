// Beta 1.1 optional in-headset title sequence. This is a small VIEW-space
// OpenXR layer and never changes the game's renderer, camera, UI, or timing.
bool p48Loaded=false,p48Enabled=true,p48Started=false,p48Finished=false,p48MusicOpen=false;
ULONGLONG p48StartTick=0;
XrSwapchain p48Swapchain=XR_NULL_HANDLE;
std::vector<XrSwapchainImageD3D11KHR> p48Images;
bool p48Acquired=false;uint32_t p48AcquiredIndex=0;

static constexpr ULONGLONG kP48DurationMs=7800;

void P48StopMusic(){
    if(!p48MusicOpen)return;
    mciSendStringW(L"stop Outlast2VRIntro",nullptr,0,nullptr);
    mciSendStringW(L"close Outlast2VRIntro",nullptr,0,nullptr);
    p48MusicOpen=false;
}

void P48Load(){
    if(p48Loaded)return;p48Loaded=true;
    const auto config=ModuleDir()+L"\\outlast2_vr_p35.ini";
    p48Enabled=GetPrivateProfileIntW(L"VR",L"StartupIntro",1,config.c_str())!=0;
    Log("PF19 STARTUP INTRO: enabled=%d; headset title layer only; gameplay and frontend routing unchanged.",p48Enabled?1:0);
}

void P48StartMusic(){
    const auto path=ModuleDir()+L"\\Outlast2VR_intro.mp3";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES){
        Log("PF19 startup music not found; visual intro continues without audio.");return;
    }
    std::wstring command=L"open \""+path+L"\" type mpegvideo alias Outlast2VRIntro";
    if(mciSendStringW(command.c_str(),nullptr,0,nullptr)!=0){
        Log("PF19 startup music could not be opened; visual intro continues.");return;
    }
    p48MusicOpen=true;
    mciSendStringW(L"setaudio Outlast2VRIntro volume to 340",nullptr,0,nullptr);
    mciSendStringW(L"play Outlast2VRIntro from 0 to 7800",nullptr,0,nullptr);
}

bool P48Ensure(){
    if(p48Swapchain!=XR_NULL_HANDLE)return !p48Images.empty();
    if(!sessionRunning)return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format=P46ColorFormat(DXGI_FORMAT_R8G8B8A8_UNORM);info.sampleCount=1;
    info.width=1024;info.height=512;info.faceCount=1;info.arraySize=1;info.mipCount=1;
    if(XR_FAILED(xrCreateSwapchain(session,&info,&p48Swapchain)))return false;
    uint32_t count=0;
    if(XR_FAILED(xrEnumerateSwapchainImages(p48Swapchain,0,&count,nullptr))||!count){
        xrDestroySwapchain(p48Swapchain);p48Swapchain=XR_NULL_HANDLE;return false;
    }
    p48Images.assign(count,XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(XR_FAILED(xrEnumerateSwapchainImages(p48Swapchain,count,&count,
       reinterpret_cast<XrSwapchainImageBaseHeader*>(p48Images.data())))){
        xrDestroySwapchain(p48Swapchain);p48Swapchain=XR_NULL_HANDLE;p48Images.clear();return false;
    }
    Log("PF19 headset intro layer ready: 1024x512, view-locked, comfort fade only.");
    return true;
}

static int P48TextWidth(const char* text,int scale){return int(std::strlen(text))*6*scale-scale;}
static void P48Centered(std::vector<uint32_t>& pixels,int y,const char* text,int scale,uint32_t color){
    P37Text(pixels,1024,512,(1024-P48TextWidth(text,scale))/2,y,text,scale,color);
}
static uint8_t P48Alpha(uint32_t color){return static_cast<uint8_t>(color>>24);}
static uint32_t P48WithAlpha(uint32_t color,float amount){
    const uint32_t alpha=static_cast<uint32_t>(std::clamp(float(P48Alpha(color))*amount,0.0f,255.0f));
    return (color&0x00FFFFFFu)|(alpha<<24);
}

bool P48RenderIntro(){
    P48Load();
    if(!p48Enabled||p48Finished||!sessionRunning)return false;
    if(!P48Ensure())return false;
    if(!p48Started){p48Started=true;p48StartTick=GetTickCount64();P48StartMusic();}
    const ULONGLONG elapsed=GetTickCount64()-p48StartTick;
    if(elapsed>=kP48DurationMs){p48Finished=true;P48StopMusic();Log("PF19 headset intro complete; normal presentation continues.");return false;}
    if(!p48Acquired){
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(XR_FAILED(xrAcquireSwapchainImage(p48Swapchain,&acquire,&p48AcquiredIndex)))return false;
        p48Acquired=true;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(xrWaitSwapchainImage(p48Swapchain,&wait)))return false;

    // Slow fades and a restrained half-second pulse follow the opening rhythm.
    // There are no flashes or abrupt cuts: this is the breath before the horror.
    const float fadeIn=std::clamp(float(elapsed)/650.0f,0.0f,1.0f);
    const float fadeOut=std::clamp(float(kP48DurationMs-elapsed)/1200.0f,0.0f,1.0f);
    const float visibility=fadeIn*fadeOut;
    const float beatPhase=float(elapsed%500)/500.0f;
    const float pulse=0.88f+0.12f*(1.0f-std::fabs(beatPhase*2.0f-1.0f));
    std::vector<uint32_t> pixels(1024*512,P48WithAlpha(0xF0060907u,visibility));
    const uint32_t white=P48WithAlpha(0xFFF2F0E9u,visibility*pulse);
    const uint32_t orange=P48WithAlpha(0xFFFF7137u,visibility*pulse);
    const uint32_t dim=P48WithAlpha(0xFF91A096u,visibility*0.70f);

    for(int y=24;y<512;y+=7)P37Fill(pixels,1024,512,0,y,1024,y+1,P48WithAlpha(0xFF182019u,visibility*0.20f));
    P37Fill(pixels,1024,512,150,252,874,254,P48WithAlpha(0xFFFF7137u,visibility*0.65f));
    if(elapsed<2450){
        P48Centered(pixels,174,"STRIKEZONE",8,white);
        P48Centered(pixels,286,"PRESENTS",4,dim);
    }else{
        P48Centered(pixels,132,"OUTLAST 2",10,white);
        if(elapsed>=3950)P48Centered(pixels,300,"VR",12,orange);
        else P48Centered(pixels,306,"A VIRTUAL REALITY CONVERSION",3,dim);
    }
    P48Centered(pixels,456,"STRIKEZONE VR",2,dim);

    const bool valid=p48AcquiredIndex<p48Images.size()&&p48Images[p48AcquiredIndex].texture;
    if(valid)context->UpdateSubresource(p48Images[p48AcquiredIndex].texture,0,nullptr,pixels.data(),1024*4,0);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if(XR_FAILED(xrReleaseSwapchainImage(p48Swapchain,&release))){failed=true;return false;}
    p48Acquired=false;return valid;
}

void P48IntroQuad(XrCompositionLayerQuad& quad){
    quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space=viewSpace;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain=p48Swapchain;quad.subImage.imageRect={{0,0},{1024,512}};
    quad.pose.orientation={0,0,0,1};quad.pose.position={0,0,-1.85f};
    quad.size={3.15f,1.575f};
}
