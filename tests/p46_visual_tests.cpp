#include "../src/dinput8_proxy.cpp"
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(bool condition,const char* label){if(!condition)throw std::runtime_error(label);}
static XrPosef testHead{{0,0,0,1},{0,0,0}};
static XrResult XRAPI_PTR Head(XrSpace,XrSpace,XrTime,XrSpaceLocation* location){
    location->pose=testHead;location->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;return XR_SUCCESS;
}
static XrResult XRAPI_PTR Views(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views){
    *count=2;state->viewStateFlags=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    for(int i=0;i<2;++i){views[i].pose=testHead;views[i].pose.position.x+=(i?1:-1)*.032f;views[i].fov={-.8f,.8f,.8f,-.8f};}return XR_SUCCESS;
}
int main()try{
    BOOL hiddenPredicate=FALSE;
    Check(p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION_PREDICATE,
        &hiddenPredicate,sizeof(hiddenPredicate))&&hiddenPredicate==TRUE,"CPU occlusion predicate repaired");
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION_PREDICATE,
        &hiddenPredicate,sizeof(hiddenPredicate)),"visible predicate preserved");
    BOOL overflow=FALSE;
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_SO_OVERFLOW_PREDICATE,
        &overflow,sizeof(overflow))&&overflow==FALSE,"stream output predicate preserved");
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION_PREDICATE,
        &overflow,1)&&overflow==FALSE,"short predicate payload preserved");
    std::uint64_t hiddenSamples=0;
    Check(p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION,&hiddenSamples,sizeof(hiddenSamples))&&hiddenSamples==1,
        "hidden occlusion result forced visible");
    std::uint64_t visibleSamples=17;
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION,&visibleSamples,sizeof(visibleSamples))&&visibleSamples==17,
        "visible occlusion result preserved");
    std::uint64_t timestamp=0;
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_TIMESTAMP,&timestamp,sizeof(timestamp))&&timestamp==0,
        "non-occlusion query preserved");
    Check(!p36visibility::ForceOcclusionVisible(D3D11_QUERY_OCCLUSION,&hiddenSamples,sizeof(std::uint32_t)),
        "short occlusion result rejected");
    Check(p46::Srgb(DXGI_FORMAT_R8G8B8A8_UNORM)==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"SDR tagged sRGB");
    Check(p46::Linear(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)==DXGI_FORMAT_R8G8B8A8_UNORM,"no double encoding");
    Check(p46::Gameplay(DXGI_FORMAT_R8G8B8A8_UNORM,true)==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"gameplay bytes tagged sRGB");
    Check(p46::Gameplay(DXGI_FORMAT_R8G8B8A8_UNORM,false)==DXGI_FORMAT_R8G8B8A8_UNORM,"legacy gameplay fallback");
    Check(p46::Gameplay(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,true)==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,"sRGB gameplay stays sRGB");
    Check(p46::Srgb(DXGI_FORMAT_R16G16B16A16_FLOAT)==DXGI_FORMAT_R16G16B16A16_FLOAT,"float not tagged sRGB");
    auto anchor=p46::Anchor(testHead,2.2f);Check(std::fabs(anchor.position.z+2.2f)<1e-6f,"screen in front, not behind");
    XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    for(int i=0;i<2;++i){views[i].pose={{0,0,0,1},{(i?1:-1)*.032f,0,0}};views[i].fov={-.8f,.8f,.8f,-.8f};}
    p46::ScreenConstants c[2]{};
    Check(p46::Geometry(anchor,views[0],2.4f,2,c[0])&&p46::Geometry(anchor,views[1],2.4f,2,c[1]),"screen geometry");
    Check(c[0].clip[0][1]>0&&c[0].uv[0][1]==0,"image top at top");
    Check(c[0].clip[0][0]>c[1].clip[0][0],"correct binocular convergence");
    for(auto& eye:c)for(auto& vertex:eye.clip)Check(std::fabs(vertex[0])<vertex[3]&&std::fabs(vertex[1])<vertex[3],"all corners visible");
    auto asymmetric=views[1];asymmetric.fov={-.7f,.95f,.9f,-.75f};
    Check(p46::Geometry(anchor,asymmetric,2.4f,16.f/9,c[1]),"asymmetric eye geometry");
    Check(!p46::Geometry(anchor,views[0],2.4f,0,c[1]),"bad aspect rejected");

    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP create");
    Check(InstallViewProjectionProbeHooks(ctx.Get()),"production context hooks");
    D3D11_QUERY_DESC occlusionDesc{D3D11_QUERY_OCCLUSION,0};
    ComPtr<ID3D11Query> menuOcclusion,gameplayOcclusion;
    Check(SUCCEEDED(dev->CreateQuery(&occlusionDesc,&menuOcclusion)),"menu occlusion query");
    g_p10GameplayActive=false;ctx->Begin(menuOcclusion.Get());ctx->End(menuOcclusion.Get());ctx->Flush();
    std::uint64_t menuSamples=~0ull;HRESULT queryResult=S_FALSE;
    for(int i=0;i<10000&&queryResult==S_FALSE;++i)queryResult=ctx->GetData(menuOcclusion.Get(),&menuSamples,sizeof(menuSamples),0);
    Check(queryResult==S_OK&&menuSamples==0,"non-gameplay occlusion result preserved");
    Check(SUCCEEDED(dev->CreateQuery(&occlusionDesc,&gameplayOcclusion)),"gameplay occlusion query");
    g_p10GameplayActive=true;ctx->Begin(gameplayOcclusion.Get());ctx->End(gameplayOcclusion.Get());ctx->Flush();
    std::uint64_t gameplaySamples=0;queryResult=S_FALSE;
    for(int i=0;i<10000&&queryResult==S_FALSE;++i)queryResult=ctx->GetData(gameplayOcclusion.Get(),&gameplaySamples,sizeof(gameplaySamples),0);
    Check(queryResult==S_OK&&gameplaySamples==1,"hooked gameplay occlusion result forced visible");
    g_p10GameplayActive=false;
    std::vector<uint32_t> pixels(128*64,0xff808080);
    // Four differently colored corner markers; central gray tests encoded values.
    for(int y=0;y<64;y++)for(int x=0;x<128;x++)if(x<32||x>96)
        pixels[y*128+x]=y<32?(x<64?0xff0000ff:0xff00ff00):(x<64?0xffff0000:0xffffffff);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=128;desc.Height=64;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D11_SUBRESOURCE_DATA init{pixels.data(),128*4,0};ComPtr<ID3D11Texture2D> source,target,readback;
    Check(SUCCEEDED(dev->CreateTexture2D(&desc,&init,&source)),"source");
    desc.Width=desc.Height=512;desc.ArraySize=2;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    Check(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,&target)),"target");
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
    Check(SUCCEEDED(dev->CreateTexture2D(&desc,nullptr,&readback)),"readback");
    D3D11_VIEWPORT viewport{11,13,71,73,.2f,.8f};ctx->RSSetViewports(1,&viewport);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    p46::ScreenRenderer renderer;Check(SUCCEEDED(renderer.Init(dev.Get())),"compile renderer");
    Check(std::string(renderer.lastStage)=="ready","D3D11.1 isolated context ready");
    Check(renderer.Render(ctx.Get(),source.Get(),nullptr,target.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,views,anchor,2.4f),"render screen");
    UINT n=1;D3D11_VIEWPORT after{};ctx->RSGetViewports(&n,&after);
    Check(!std::memcmp(&viewport,&after,sizeof(after)),"viewport restored");
    D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetPrimitiveTopology(&topology);Check(topology==D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,"IA restored");
    ctx->CopyResource(readback.Get(),target.Get());
    for(int eye=0;eye<2;eye++){
        D3D11_MAPPED_SUBRESOURCE data{};Check(SUCCEEDED(ctx->Map(readback.Get(),eye,D3D11_MAP_READ,0,&data)),"map readback");
        auto pixel=[&](int x,int y){return reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(data.pData)+y*data.RowPitch)[x];};
        Check(pixel(256,256)==0xff808080,"encoded mid gray unchanged (no veil/extra gamma)");
        Check(pixel(0,0)==0xff000000&&pixel(511,511)==0xff000000,"empty surround, no gameplay outside screen");
        Check(pixel(145,210)==0xff0000ff&&pixel(365,210)==0xff00ff00,"both top corners visible");
        Check(pixel(145,300)==0xffff0000&&pixel(365,300)==0xffffffff,"both bottom corners visible");
        ctx->Unmap(readback.Get(),eye);
    }
    // Text extracted during the exact menu-transition frame must be restored
    // inside the surface, once, with the native premultiplied-alpha convention.
    std::vector<uint32_t> overlayPixels(128*64,0x80000080);
    D3D11_TEXTURE2D_DESC overlayDesc{};source->GetDesc(&overlayDesc);
    D3D11_SUBRESOURCE_DATA overlayInit{overlayPixels.data(),128*4,0};ComPtr<ID3D11Texture2D> overlay;
    Check(SUCCEEDED(dev->CreateTexture2D(&overlayDesc,&overlayInit,&overlay)),"overlay texture");
    Check(renderer.Render(ctx.Get(),source.Get(),overlay.Get(),target.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,views,anchor,2.4f),"menu transition composite");
    ctx->CopyResource(readback.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE blended{};Check(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&blended)),"map overlay");
    const uint32_t center=reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(blended.pData)+256*blended.RowPitch)[256];
    Check(std::abs(int(center&255)-192)<=1&&std::abs(int((center>>8)&255)-64)<=1&&std::abs(int((center>>16)&255)-64)<=1,"UI alpha applied once without dark rectangle");
    ctx->Unmap(readback.Get(),0);
    // Reproduce the retail game's device contract. Deferred contexts fail on
    // SINGLETHREADED devices; the world screen must use context-state swapping.
    ComPtr<ID3D11Device> singleDevice;ComPtr<ID3D11DeviceContext> singleContext;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,
        nullptr,0,D3D11_SDK_VERSION,&singleDevice,nullptr,&singleContext)),"single-threaded WARP create");
    ComPtr<ID3D11DeviceContext> forbiddenDeferred;
    Check(FAILED(singleDevice->CreateDeferredContext(0,&forbiddenDeferred)),"reproduce old deferred-context failure");
    p46::ScreenRenderer singleRenderer;
    Check(SUCCEEDED(singleRenderer.Init(singleDevice.Get()))&&std::string(singleRenderer.lastStage)=="ready",
        "world screen supports Outlast single-threaded device");
    // Simulate the yaw/pitch camera hook handoff; preserve native pitch and
    // carry roll through the renderer, including the scripted fallback.
    OpenXRQuad xr;xr.gameplayVr=xr.sessionRunning=true;xr.p12ConfigLoaded=true;xr.p12SameFrameStereo=1;
    Check(xr.P48TextWidth("OUTLAST 2",10)==530,"headset intro title centered from deterministic glyph width");
    Check((xr.P48WithAlpha(0xFFF2F0E9u,0.5f)>>24)==127,"headset intro comfort fade alpha");
    xr.p12LockHeadTranslation=false;xr.p15PitchLock=true;xr.xrLocateViews=Views;xr.xrLocateSpace=Head;
    xr.p12HaveCenter=true;xr.p12Center={{0,0,0,1},{0,0,0}};
    const float pitch=.2f,roll=.1f;
    testHead.orientation=OpenXRQuad::P11QuatMul({std::sin(pitch/2),0,0,std::cos(pitch/2)},{0,0,std::sin(roll/2),std::cos(roll/2)});
    xr.p12LastHead=testHead;g_nativeTablePatched=true;g_probeFrameCounter=100;g_p46NativeViewFrame=100;
    xr.P12Prepare(1000000000,true);
    Check(xr.p12Pending.valid,"native camera prepared");
    Check(g_p12Constants.right[3]==0,"native pitch not flattened away");
    Check(std::fabs(g_p12Constants.right[1])>.05f,"head roll retained");
    g_probeFrameCounter=110;g_nativeHeadLock=true;xr.P12Prepare(1100000000,true);
    Check(g_p12Constants.right[3]==0,"scripted camera pitch retained");
    Check(std::fabs(g_p12Constants.forward[1])>.1f,"fallback head tracking survives old native calls");
    g_nativeHeadLock=false;g_nativeTablePatched=false;
    p46ui::mainKnown=false;p46ui::loadingCompleted=false;g_p46GameplayCameraDiscoveryEnabled=false;
    Check(p46ui::Screen(),"startup remains screen until native state is known");
    uint32_t menu=1;p46ui::MainResult(&menu);Check(p46ui::Screen(),"main menu overrides hidden cursor");
    Check(!g_p46GameplayCameraDiscoveryEnabled.load(),"menu camera discovery blocked");
    menu=0;p46ui::MainResult(&menu);Check(g_p46GameplayCameraDiscoveryEnabled.load(),"gameplay camera discovery armed");
    p46ui::paused=true;Check(p46ui::Screen(),"pause/options remain screen");
    p46ui::paused=false;p46ui::loading=true;Check(p46ui::Screen(),"loading screen");
    p46ui::loading=false;Check(!p46ui::Screen(),"gameplay restored");
    // A visible OS cursor or not-yet-discovered renderer binding must not trap
    // native gameplay on the world screen. This reproduces the headset report
    // from the first working world-screen build.
    OpenXRQuad route;route.gameplayVr=false;route.hiddenCursorFrames=route.visibleCursorFrames=0;
    g_p10ViewProjectionValidated=false;
    for(int i=0;i<3;++i)route.UpdateAutomaticPresentationMode();
    Check(route.gameplayVr,"native gameplay bypasses cursor/projection circular gate");
    p46ui::paused=true;route.UpdateAutomaticPresentationMode();
    Check(!route.gameplayVr,"native pause returns immediately to world screen");
    p46ui::paused=false;
    uint32_t stale=120;
    Check(!p46ui::SceneReady(false,false,stale)&&stale==0,"screen mode clears stale scene veto");
    Check(p46ui::SceneReady(true,false,stale)&&stale==1,"gameplay transition gets bounded discovery window");
    D3D11_BUFFER_DESC candidateDesc{};candidateDesc.ByteWidth=160;candidateDesc.Usage=D3D11_USAGE_DEFAULT;
    candidateDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> persistentCandidate;
    Check(SUCCEEDED(dev->CreateBuffer(&candidateDesc,nullptr,&persistentCandidate)),"persistent camera candidate");
    ID3D11Buffer* candidate=persistentCandidate.Get();g_realVSSetConstantBuffers(ctx.Get(),1,1,&candidate);
    {std::lock_guard<std::mutex> lock(g_cbProbeMutex);g_cbProbeStates.erase(candidate);}
    g_p10ViewProjectionValidated=false;g_probeFrameCounter=333;g_p46GameplayCameraDiscoveryEnabled=false;
    P46ObserveRestoredViewProjectionCandidate(ctx.Get());
    {std::lock_guard<std::mutex> lock(g_cbProbeMutex);Check(g_cbProbeStates.find(candidate)==g_cbProbeStates.end(),
        "menu candidate ignored before gameplay");}
    g_p46GameplayCameraDiscoveryEnabled=true;g_p46GameplayCameraDiscoveryStartFrame=333;
    P46ObserveRestoredViewProjectionCandidate(ctx.Get());
    {std::lock_guard<std::mutex> lock(g_cbProbeMutex);auto it=g_cbProbeStates.find(candidate);
        Check(it!=g_cbProbeStates.end()&&it->second.retained==candidate&&it->second.lastBindFrame==333,
            "restored persistent camera binding observed without a set callback");}
    // Regression from the Fix 3 headset log: after context-state restoration,
    // the draw fallback must observe a scene before camera validation exists.
    D3D11_TEXTURE2D_DESC depthDesc{};depthDesc.Width=128;depthDesc.Height=64;depthDesc.MipLevels=depthDesc.ArraySize=1;
    depthDesc.Format=DXGI_FORMAT_R24G8_TYPELESS;depthDesc.SampleDesc.Count=1;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTexture;ComPtr<ID3D11DepthStencilView> depthView;
    Check(SUCCEEDED(dev->CreateTexture2D(&depthDesc,nullptr,&depthTexture)),"handoff depth texture");
    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};depthViewDesc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthViewDesc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    Check(SUCCEEDED(dev->CreateDepthStencilView(depthTexture.Get(),&depthViewDesc,&depthView)),"handoff depth view");
    g_expectedDepthWidth=128;g_expectedDepthHeight=64;g_p46SceneFrame=~0ull;g_p10GameplayActive=true;
    g_p10ViewProjectionValidated=false;g_realOMSetRenderTargets(ctx.Get(),0,nullptr,depthView.Get());
    {P13UiDrawScope draw(ctx.Get());}
    Check(g_p46SceneFrame.load()==333,"unvalidated gameplay draw opens camera discovery gate");
    g_p10ViewProjectionValidated=true;g_p15ReflectionEnabled=true;
    Check(!P15IsRetainedSceneDepth(nullptr),"null depth excluded");
    g_realOMSetRenderTargets(ctx.Get(),0,nullptr,nullptr);
    {P15PixelScope pixel(ctx.Get());
        for(int i=0;i<14;++i)Check(!pixel.replaced[i]&&!pixel.originals[i],
            "no-depth postprocess/UI excluded before pixel buffer inspection");}
    // A depth resource that is not the retained scene resource is excluded.
    {std::lock_guard<std::mutex> lock(g_earlyDepthMutex);
        if(g_earlyFullResDsv){g_earlyFullResDsv->Release();g_earlyFullResDsv=nullptr;}}
    g_realOMSetRenderTargets(ctx.Get(),0,nullptr,depthView.Get());
    Check(!P15IsRetainedSceneDepth(depthView.Get()),"same-size unretained resource excluded");
    {P15PixelScope pixel(ctx.Get());
        for(int i=0;i<14;++i)Check(!pixel.replaced[i]&&!pixel.originals[i],
            "unvalidated depth pass excluded before pixel buffer inspection");}
    {std::lock_guard<std::mutex> lock(g_earlyDepthMutex);
        g_earlyFullResDsv=depthView.Get();g_earlyFullResDsv->AddRef();}
    Check(P15IsRetainedSceneDepth(depthView.Get()),"retained scene resource accepted");
    g_p10GameplayActive=false;
    std::puts("PASS: world-space screen, both eyes, corners, aspect, color bytes, pipeline isolation, camera handoff, native UI routing");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
