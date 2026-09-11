// P37 in-headset configuration menu and locomotion mode.
// Included as members of OpenXRQuad so it can create a tiny VIEW-space quad.
bool p37GuiOpen=false,p37GuiLatch=false,p37Loaded=false;
int p37Mode=0,p37Selection=0; // 0 normal, 1 immersive arm-swing run
ULONGLONG p37BothStart=0,p37LastUiMove=0,p37LastSwingTick=0;
XrVector3f p37PrevHands[2]{};bool p37PrevHandValid[2]{};float p37SwingSpeed=0;
XrSwapchain p37GuiSwapchain=XR_NULL_HANDLE;std::vector<XrSwapchainImageD3D11KHR> p37GuiImages;

void P37LoadConfig(){
    if(p37Loaded)return;p37Loaded=true;
    const auto cfg=ModuleDir()+L"\\outlast2_vr_p37.ini";
    p37Mode=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"VR",L"LocomotionMode",0,cfg.c_str())),0,1);p37Selection=p37Mode;
    Log("P37 CONFIG: locomotion=%s; hold both thumbstick clicks 0.65 s for VR menu.",p37Mode?"IMMERSIVE ARM-SWING":"NORMAL");
}
void P37SaveMode(){
    const auto cfg=ModuleDir()+L"\\outlast2_vr_p37.ini";
    WritePrivateProfileStringW(L"VR",L"LocomotionMode",p37Mode?L"1":L"0",cfg.c_str());
}
float P37ArmSwing(){
    const auto now=GetTickCount64();if(!p37LastSwingTick){p37LastSwingTick=now;return 0;}
    const float dt=std::max(0.001f,float(now-p37LastSwingTick)/1000.0f);p37LastSwingTick=now;
    float sum=0;int n=0;
    for(int i=0;i<2;++i){
        if(p19Valid[i]){
            const auto p=p19Poses[i].position;
            if(p37PrevHandValid[i]){const float dx=p.x-p37PrevHands[i].x,dy=p.y-p37PrevHands[i].y,dz=p.z-p37PrevHands[i].z;sum+=std::sqrt(dx*dx+dy*dy+dz*dz)/dt;++n;}
            p37PrevHands[i]=p;p37PrevHandValid[i]=true;
        }else p37PrevHandValid[i]=false;
    }
    const float instant=n?sum/n:0.0f;p37SwingSpeed=p37SwingSpeed*0.72f+instant*0.28f;return p37SwingSpeed;
}
bool P37Controls(XrVector2f sticks[2],bool b[7],const float[4]){
    P37LoadConfig();const auto now=GetTickCount64();const bool both=b[5]&&b[6];
    if(both){if(!p37BothStart)p37BothStart=now;if(!p37GuiLatch&&now-p37BothStart>=650){p37GuiOpen=!p37GuiOpen;p37GuiLatch=true;p37Selection=p37Mode;Log("P37 VR CONFIG %s",p37GuiOpen?"OPEN":"CLOSED");}}
    else{p37BothStart=0;p37GuiLatch=false;}
    if(p37GuiOpen){
        if(now-p37LastUiMove>250){if(sticks[0].y>.55f){p37Selection=0;p37LastUiMove=now;}else if(sticks[0].y<-.55f){p37Selection=1;p37LastUiMove=now;}}
        if(b[0]){p37Mode=p37Selection;P37SaveMode();p37GuiOpen=false;Log("P37 LOCOMOTION MODE -> %s",p37Mode?"IMMERSIVE ARM-SWING":"NORMAL");}
        if(b[1])p37GuiOpen=false;
        return true;
    }
    const float speed=P37ArmSwing();
    if(gameplayVr&&p37Mode==1){
        // Immersive mode: physical alternating arm motion replaces left-stick-click sprint.
        b[5]=(sticks[0].y>.25f&&speed>.32f);
    }
    return false;
}

static void P37Fill(std::vector<uint32_t>& px,int w,int h,int x0,int y0,int x1,int y1,uint32_t c){
    x0=std::clamp(x0,0,w);x1=std::clamp(x1,0,w);y0=std::clamp(y0,0,h);y1=std::clamp(y1,0,h);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)px[size_t(y)*w+x]=c;
}
static std::array<uint8_t,7> P37Glyph(char c){
    switch(c){
#define G(ch,a,b,c,d,e,f,g) case ch:return {a,b,c,d,e,f,g};
G('A',14,17,17,31,17,17,17) G('B',30,17,17,30,17,17,30) G('C',14,17,16,16,16,17,14)
G('D',30,17,17,17,17,17,30) G('E',31,16,16,30,16,16,31) G('F',31,16,16,30,16,16,16)
G('G',14,17,16,23,17,17,15) G('H',17,17,17,31,17,17,17) G('I',31,4,4,4,4,4,31)
G('L',16,16,16,16,16,16,31) G('M',17,27,21,21,17,17,17) G('N',17,25,21,19,17,17,17)
G('O',14,17,17,17,17,17,14) G('R',30,17,17,30,20,18,17) G('S',15,16,16,14,1,1,30)
G('T',31,4,4,4,4,4,4) G('U',17,17,17,17,17,17,14) G('V',17,17,17,17,17,10,4)
G('W',17,17,17,21,21,21,10) G('Y',17,17,10,4,4,4,4) G('X',17,17,10,4,10,17,17)
G('P',30,17,17,30,16,16,16) G('K',17,18,20,24,20,18,17) G(' ',0,0,0,0,0,0,0)
#undef G
    default:return {0,0,0,0,0,0,0};}
}
static void P37Text(std::vector<uint32_t>& px,int w,int h,int x,int y,const char* s,int scale,uint32_t c){
    for(;*s;++s,x+=6*scale){auto g=P37Glyph(*s);for(int yy=0;yy<7;++yy)for(int xx=0;xx<5;++xx)if(g[yy]&(1<<(4-xx)))P37Fill(px,w,h,x+xx*scale,y+yy*scale,x+(xx+1)*scale,y+(yy+1)*scale,c);}
}
bool P37EnsureGui(){
    if(p37GuiSwapchain!=XR_NULL_HANDLE)return true;if(!sessionRunning)return false;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format=DXGI_FORMAT_R8G8B8A8_UNORM;ci.sampleCount=1;ci.width=512;ci.height=256;ci.faceCount=1;ci.arraySize=1;ci.mipCount=1;
    if(XR_FAILED(xrCreateSwapchain(session,&ci,&p37GuiSwapchain)))return false;uint32_t n=0;if(XR_FAILED(xrEnumerateSwapchainImages(p37GuiSwapchain,0,&n,nullptr))||!n)return false;
    p37GuiImages.assign(n,XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});if(XR_FAILED(xrEnumerateSwapchainImages(p37GuiSwapchain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(p37GuiImages.data()))))return false;
    Log("P37 VR CONFIG QUAD READY: 512x256.");return true;
}
bool P37RenderGui(){
    if(!p37GuiOpen||!P37EnsureGui())return false;uint32_t index=0;XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_FAILED(xrAcquireSwapchainImage(p37GuiSwapchain,&ai,&index)))return false;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=XR_INFINITE_DURATION;if(XR_FAILED(xrWaitSwapchainImage(p37GuiSwapchain,&wi)))return false;
    std::vector<uint32_t> px(512*256,0xE0202020u);P37Fill(px,512,256,12,12,500,244,0xF0303030u);
    P37Text(px,512,256,42,28,"VR CONFIG",4,0xFFFFFFFFu);
    const uint32_t sel=0xFF4FAF70u,dim=0xFF555555u;
    P37Fill(px,512,256,38,88,474,136,p37Selection==0?sel:dim);P37Text(px,512,256,64,100,"NORMAL MODE",3,0xFFFFFFFFu);
    P37Fill(px,512,256,38,146,474,194,p37Selection==1?sel:dim);P37Text(px,512,256,64,158,"IMMERSIVE MODE",3,0xFFFFFFFFu);
    P37Text(px,512,256,70,216,"A SELECT  B CLOSE",2,0xFFFFFFFFu);
    if(index<p37GuiImages.size()&&p37GuiImages[index].texture)context->UpdateSubresource(p37GuiImages[index].texture,0,nullptr,px.data(),512*4,0);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};xrReleaseSwapchainImage(p37GuiSwapchain,&ri);return true;
}
void P37GuiQuad(XrCompositionLayerQuad& q){q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;q.space=viewSpace;q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;q.subImage.swapchain=p37GuiSwapchain;q.subImage.imageRect={{0,0},{512,256}};q.pose.orientation={0,0,0,1};q.pose.position={0,0,-1.35f};q.size={1.45f,0.725f};}
