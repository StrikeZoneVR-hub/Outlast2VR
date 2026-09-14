// Compile the production bridge into this test so the exact capture/submission
// methods are exercised with WARP textures and a small fake XR swapchain.
#include "../src/dinput8_proxy.cpp"
#include <stdexcept>

static uint32_t acquireCount=0,releaseCount=0,waitCount=0;
static bool failWait=false,failAcquire=false;
static XrResult XRAPI_PTR Acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    if(failAcquire) return XR_ERROR_RUNTIME_FAILURE;
    *index=acquireCount++%3; return XR_SUCCESS;
}
static XrResult XRAPI_PTR Wait(XrSwapchain,const XrSwapchainImageWaitInfo*) {
    ++waitCount; return failWait?XR_TIMEOUT_EXPIRED:XR_SUCCESS;
}
static XrResult XRAPI_PTR Release(XrSwapchain,const XrSwapchainImageReleaseInfo*) {
    ++releaseCount; return XR_SUCCESS;
}
static XrResult XRAPI_PTR LocateViews(XrSession,const XrViewLocateInfo*,XrViewState* state,
    uint32_t capacity,uint32_t* count,XrView* views) {
    *count=2;
    state->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(capacity>=2&&views){
        views[0]={XR_TYPE_VIEW};views[1]={XR_TYPE_VIEW};
        views[0].pose.orientation.w=views[1].pose.orientation.w=1.0f;
        views[0].pose.position.x=-0.032f;views[1].pose.position.x=0.032f;
        views[0].fov={-0.95f,0.82f,0.78f,-0.72f};
        views[1].fov={-0.82f,0.95f,0.78f,-0.72f};
    }
    return XR_SUCCESS;
}
static void Check(bool b,const char* why) { if(!b) throw std::runtime_error(why); }

int main() try {
    OpenXRQuad bridge;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&bridge.device,nullptr,&bridge.context)),"WARP device");
    bridge.width=8; bridge.height=4; bridge.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    bridge.xrAcquireSwapchainImage=&Acquire; bridge.xrWaitSwapchainImage=&Wait; bridge.xrReleaseSwapchainImage=&Release;
    bridge.xrLocateViews=&LocateViews;
    bridge.session=reinterpret_cast<XrSession>(1);bridge.localSpace=reinterpret_cast<XrSpace>(1);
    bridge.stereoSwapchain=reinterpret_cast<XrSwapchain>(1);
    D3D11_TEXTURE2D_DESC td{};
    td.Width=8; td.Height=4; td.MipLevels=1; td.ArraySize=1;
    td.Format=bridge.format; td.SampleDesc={1,0}; td.Usage=D3D11_USAGE_DEFAULT;
    ID3D11Texture2D* backbuffer=nullptr;
    Check(SUCCEEDED(bridge.device->CreateTexture2D(&td,nullptr,&backbuffer)),"backbuffer");
    td.ArraySize=2;
    td.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(int i=0;i<3;++i) {
        XrSwapchainImageD3D11KHR image{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
        Check(SUCCEEDED(bridge.device->CreateTexture2D(&td,nullptr,&image.texture)),"XR image array");
        bridge.stereoImages.push_back(image);
    }
    td.Usage=D3D11_USAGE_STAGING; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ; td.BindFlags=0;
    ID3D11Texture2D* readback=nullptr;
    Check(SUCCEEDED(bridge.device->CreateTexture2D(&td,nullptr,&readback)),"readback");
    g_p10ViewProjectionValidated.store(true); g_p11cDepthGeneration.store(7);
    auto render=[&](uint64_t frame,uint32_t color) {
        bridge.p12Pending={}; bridge.p12Pending.valid=true;
        bridge.p12Pending.frame=frame; bridge.p12Pending.eye=bridge.p12NextEye;
        bridge.p12Pending.pair=(frame+1)/2;
        bridge.p12Pending.generation=7; bridge.p12Pending.time=1000*frame;
        bridge.p12Pending.view.pose.orientation.w=1;
        bridge.p12Pending.view.pose.position.x=static_cast<float>(frame);
        bridge.p12Pending.view.fov={-0.9f,0.7f,0.8f,-0.7f};
        std::array<uint32_t,32> pixels; pixels.fill(color);
        bridge.context->UpdateSubresource(backbuffer,0,nullptr,pixels.data(),8*4,0);
        g_probeFrameCounter.store(frame+1); g_p12BoundFrame.store(frame);
    };
    XrCompositionLayerProjectionView views[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    auto checkPixels=[&](uint32_t left,uint32_t right) {
        bridge.context->CopyResource(readback,bridge.stereoImages[(acquireCount-1)%3].texture);
        for(uint32_t eye=0;eye<2;++eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(SUCCEEDED(bridge.context->Map(readback,eye,D3D11_MAP_READ,0,&mapped)),"map eye array");
            for(uint32_t y=0;y<4;++y) for(uint32_t x=0;x<8;++x) {
                auto row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
                Check(row[x]==(eye==0?left:right),"persistent correct eye pixels across rotating XR images");
            }
            bridge.context->Unmap(readback,eye);
        }
    };
    render(1,0xFF000011);
    Check(bridge.P12CaptureEye(backbuffer),"capture left");
    Check(!bridge.P12CopyPair(views,2000,1000),"no uninitialized right eye");
    Check(acquireCount==0,"no XR acquire before pair ready");
    for(uint64_t frame=2;frame<=8;++frame) {
        render(frame,0xFF000000+static_cast<uint32_t>(frame)*0x11);
        Check(bridge.P12CaptureEye(backbuffer),"capture alternating eye");
        Check(bridge.P12CopyPair(views,(frame+1)*1000,1000),"pair or stable-pair submission");
        const uint64_t lf=frame%2?frame-2:frame-1,rf=lf+1;
        checkPixels(0xFF000000+static_cast<uint32_t>(lf)*0x11,0xFF000000+static_cast<uint32_t>(rf)*0x11);
        Check(views[0].pose.position.x==lf && views[1].pose.position.x==rf,"saved per-eye poses retained");
        Check(views[0].fov.angleLeft==-0.9f && views[1].fov.angleRight==0.7f,"saved asymmetric FOV retained");
        Check(views[0].subImage.imageRect.extent.width==8 && views[1].subImage.imageArrayIndex==1,"full uncropped image rectangles");
    }
    const auto pair=bridge.p12Eyes[0].pair;
    bridge.p12Eyes[0].pair=pair-1;
    Check(bridge.P12CopyPair(views,9000,1000),"mixed candidate reuses stable pair");
    bridge.p12Eyes[0].pair=pair;
    auto count=acquireCount;
    failAcquire=true;
    Check(!bridge.P12CopyPair(views,9000,1000)&&acquireCount==count,"acquire failure suppresses submission");
    failAcquire=false; failWait=true;
    const auto releases=releaseCount;
    Check(!bridge.P12CopyPair(views,9000,1000),"wait timeout suppresses submission");
    Check(bridge.p12StereoAcquired && releaseCount==releases,"never release image before successful wait");
    count=acquireCount; failWait=false;
    Check(bridge.P12CopyPair(views,9000,1000),"retry wait");
    Check(acquireCount==count && releaseCount==releases+1,"reuse outstanding acquisition");
    Check(!bridge.P12CopyPair(views,90000,1000),"reject stale display time");
    g_probeFrameCounter.store(15);
    Check(!bridge.P12CopyPair(views,9000,1000),"reject stale render frame");
    render(9,0x12345678); g_p12BoundFrame.store(8);
    Check(!bridge.P12CaptureEye(backbuffer),"unpatched render rejected");
    Check(!bridge.p12Eyes[0].valid&&!bridge.p12Eyes[1].valid&&bridge.p12NextEye==0,"bad capture invalidates pair and restarts left");
    render(10,0x12345678); g_p11cDepthGeneration.store(8);
    Check(!bridge.P12CaptureEye(backbuffer),"graphics rebuild rejected");
    g_p11cDepthGeneration.store(7);
    render(11,0x12345678); g_p10ViewProjectionValidated.store(false);
    Check(!bridge.P12CaptureEye(backbuffer),"unvalidated camera rejected");
    g_p10ViewProjectionValidated.store(true);
    render(12,0x12345678); g_probeFrameCounter.store(14);
    Check(!bridge.P12CaptureEye(backbuffer),"skipped Present rejected");
    bridge.p12HaveCenter=true; bridge.P12Reset(true);
    Check(!bridge.p12HaveCenter && g_p12Constants.params[2]==0,"reset disables native patch and center");

    // PF18 must keep gameplay visible when strict camera matching is rejected.
    // It copies the current native frame to both eyes as a full projection,
    // preserving the widest runtime FOV and never using the menu quad.
    std::array<uint32_t,32> compatibilityPixels;compatibilityPixels.fill(0xFF336699);
    bridge.context->UpdateSubresource(backbuffer,0,nullptr,compatibilityPixels.data(),8*4,0);
    const auto compatibilityReleases=releaseCount;
    Check(bridge.P12CopyCompatibilityFrame(backbuffer,views,20000),"PF18 compatibility gameplay submission");
    Check(releaseCount==compatibilityReleases+1,"PF18 releases compatibility XR image");
    checkPixels(0xFF336699,0xFF336699);
    Check(views[0].pose.position.x==0.0f&&views[1].pose.position.x==0.0f,"PF18 shared midpoint pose");
    Check(views[0].fov.angleLeft==-0.95f&&views[1].fov.angleRight==0.95f,"PF18 union runtime FOV");

    // Exercise the TS1 production shader path with a real typeless D3D11 depth
    // texture. Both eyes must be rendered and the acquired XR image released.
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width=8; depthDesc.Height=4; depthDesc.MipLevels=1; depthDesc.ArraySize=1;
    depthDesc.Format=DXGI_FORMAT_R32_TYPELESS; depthDesc.SampleDesc={1,0};
    depthDesc.Usage=D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* sceneDepth=nullptr;
    Check(SUCCEEDED(bridge.device->CreateTexture2D(&depthDesc,nullptr,&sceneDepth)),"TS1 scene depth");
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format=DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    ID3D11DepthStencilView* sceneDsv=nullptr;
    Check(SUCCEEDED(bridge.device->CreateDepthStencilView(sceneDepth,&dsvDesc,&sceneDsv)),"TS1 scene DSV");
    bridge.context->ClearDepthStencilView(sceneDsv,D3D11_CLEAR_DEPTH,0.0f,0);
    bridge.context->OMSetRenderTargets(0,nullptr,sceneDsv);
    const auto stereoReleases=releaseCount;
    Check(bridge.AcquireAndRenderDepthStereo(backbuffer,2.0f,8),"TS1 same-frame depth stereo pass");
    Check(bridge.lastFrameDepthStereo,"TS1 depth path reported active");
    Check(releaseCount==stereoReleases+1,"TS1 releases acquired XR image");
    bridge.context->OMSetRenderTargets(0,nullptr,nullptr);
    sceneDsv->Release(); sceneDepth->Release();

    bridge.P12ReleaseEyes();
    bridge.ReleaseStereoResources();
    for(auto& image:bridge.stereoImages) image.texture->Release();
    readback->Release(); backbuffer->Release(); bridge.context->Release(); bridge.device->Release();
    std::puts("PASS: production AER, PF18 fail-visible compatibility, TS1 depth stereo, rotating XR images, poses/FOV, failures, stale frames, rebuild, validation and recenter resets.");
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
