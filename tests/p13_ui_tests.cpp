#include "../src/dinput8_proxy.cpp"
#include <wrl/client.h>
#include <stdexcept>
#include <fstream>
using Microsoft::WRL::ComPtr;
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static ComPtr<ID3DBlob> Compile(const char* source,const char* target){
    ComPtr<ID3DBlob> code,err;
    auto hr=D3DCompile(source,std::strlen(source),"P13 test",nullptr,nullptr,"main",target,0,0,&code,&err);
    if(FAILED(hr)&&err)std::fprintf(stderr,"%s\n",static_cast<char*>(err->GetBufferPointer()));
    Check(SUCCEEDED(hr),"compile test shader");return code;
}
static XrTime lastLocate=0,lastEnd=0;
static int waits=0,begins=0,ends=0;
static XrResult XRAPI_PTR WaitFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* fs){
    ++waits;fs->shouldRender=XR_TRUE;fs->predictedDisplayTime=1000000000;fs->predictedDisplayPeriod=11111111;return XR_SUCCESS;
}
static XrResult XRAPI_PTR BeginFrame(XrSession,const XrFrameBeginInfo*){++begins;return XR_SUCCESS;}
static XrResult XRAPI_PTR EndFrame(XrSession,const XrFrameEndInfo* end){++ends;lastEnd=end->displayTime;return XR_SUCCESS;}
static XrResult XRAPI_PTR LocateHead(XrSpace,XrSpace,XrTime t,XrSpaceLocation* loc){
    lastLocate=t;loc->locationFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    loc->pose={{0,0,0,1},{0,0,0}};return XR_SUCCESS;
}
static XrResult XRAPI_PTR LocateViews(XrSession,const XrViewLocateInfo* li,XrViewState* state,uint32_t,uint32_t* count,XrView* out){
    lastLocate=li->displayTime;*count=2;state->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    for(int i=0;i<2;++i){out[i].pose={{0,0,0,1},{i==0?-0.034f:0.034f,0,0}};out[i].fov={-0.85f,0.8f,0.85f,-0.8f};}return XR_SUCCESS;
}
int main(int argc,char** argv)try{
    if(argc==2){
        std::ifstream file(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        Check(!bytes.empty(),"shader cache available");size_t count[4]{};
        p14::UiRegistry cacheRegistry;
        for(size_t o=0;o+32<bytes.size();++o)if(!std::memcmp(bytes.data()+o,"DXBC",4)){
            uint32_t n=0;std::memcpy(&n,bytes.data()+o+24,4);if(n>=32&&n<=bytes.size()-o)cacheRegistry.Add(bytes.data()+o,n);
        }
        size_t recovered=0;
        for(size_t o=0;o+32<bytes.size();++o)if(!std::memcmp(bytes.data()+o,"DXBC",4)){
            uint32_t n=0;std::memcpy(&n,bytes.data()+o+24,4);if(n>=32&&n<=bytes.size()-o){
                auto kind=p13::ClassifyUiShader(bytes.data()+o,n);++count[static_cast<unsigned>(kind)];
                if(kind!=p13::UiShader::None){
                    ComPtr<ID3DBlob> strippedCache;
                    Check(SUCCEEDED(D3DStripShader(bytes.data()+o,n,D3DCOMPILER_STRIP_REFLECTION_DATA,&strippedCache)),"strip real cache UI");
                    Check(cacheRegistry.Find(strippedCache->GetBufferPointer(),strippedCache->GetBufferSize())==kind,"real stripped UI exact match");++recovered;
                }
            }
        }
        std::printf("Cache classifier: non-UI=%zu Scaleform2D=%zu Scaleform3D=%zu Canvas=%zu\n",count[0],count[1],count[2],count[3]);
        std::printf("P14 real stripped UI recovered=%zu\n",recovered);
        Check(count[1]>0&&count[2]>0&&count[3]>0,"actual game UI families found");return 0;
    }
    const char* uiVs="float4x4 c_Transform; float4 main(uint id:SV_VertexID):SV_Position { float2 p=id==0?float2(-1,-1):(id==1?float2(-1,3):float2(3,-1)); return mul(float4(p,0,1),c_Transform); }";
    const char* worldVs="float4 main(uint id:SV_VertexID):SV_Position {float2 p=id==0?float2(-1,-1):(id==1?float2(-1,3):float2(3,-1));return float4(p,0,1);}";
    auto uiCode=Compile(uiVs,"vs_5_0"),worldCode=Compile(worldVs,"vs_5_0");
    auto psCode=Compile("float4 main():SV_Target{return float4(1,0,0,0.5);}","ps_5_0");
    p14::UiRegistry registry;
    registry.Add(uiCode->GetBufferPointer(),uiCode->GetBufferSize());
    ComPtr<ID3DBlob> stripped;
    Check(SUCCEEDED(D3DStripShader(uiCode->GetBufferPointer(),uiCode->GetBufferSize(),D3DCOMPILER_STRIP_REFLECTION_DATA,&stripped)),"strip metadata");
    Check(p13::ClassifyUiShader(stripped->GetBufferPointer(),stripped->GetBufferSize())==p13::UiShader::None,"P13 cannot classify stripped UI");
    Check(registry.Find(stripped->GetBufferPointer(),stripped->GetBufferSize())==p13::UiShader::Canvas,"P14 exact instructions recover stripped UI");
    Check(registry.Find(worldCode->GetBufferPointer(),worldCode->GetBufferSize())==p13::UiShader::None,"unknown world never matched");
    for(size_t n=0;n<stripped->GetBufferSize();n+=7)Check(registry.Find(stripped->GetBufferPointer(),n)==p13::UiShader::None,"truncated stripped shader rejected");
    g_p14UiRegistry=registry;
    Check(p13::ClassifyUiShader(uiCode->GetBufferPointer(),uiCode->GetBufferSize())==p13::UiShader::Canvas,"known Canvas transform");
    Check(p13::ClassifyUiShader(worldCode->GetBufferPointer(),worldCode->GetBufferSize())==p13::UiShader::None,"world not UI");
    Check(p13::ClassifyUiShader(psCode->GetBufferPointer(),psCode->GetBufferSize())==p13::UiShader::None,"pixel shader not VS");
    for(size_t n=0;n<uiCode->GetBufferSize();n+=7)Check(p13::ClassifyUiShader(uiCode->GetBufferPointer(),n)==p13::UiShader::None,"truncated shader rejected");
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)),"WARP");
    Check(InstallViewProjectionProbeHooks(ctx.Get()),"install production hooks");
    ComPtr<ID3D11VertexShader> ui,world;ComPtr<ID3D11PixelShader> ps;
    Check(SUCCEEDED(device->CreateVertexShader(stripped->GetBufferPointer(),stripped->GetBufferSize(),nullptr,&ui)),"UI shader");
    Check(SUCCEEDED(device->CreateVertexShader(worldCode->GetBufferPointer(),worldCode->GetBufferSize(),nullptr,&world)),"world shader");
    Check(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)),"pixel shader");
    p13::UiShader tag=p13::UiShader::None;UINT tagSize=sizeof(tag);
    Check(SUCCEEDED(ui->GetPrivateData(kP13UiTag,&tagSize,&tag))&&tag==p13::UiShader::Canvas,"created shader tagged");
    D3D11_TEXTURE2D_DESC td{};td.Width=8;td.Height=4;td.ArraySize=td.MipLevels=1;td.SampleDesc={1,0};td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;ComPtr<ID3D11RenderTargetView> rtv;
    Check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)),"target");
    Check(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)),"RTV");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&readback)),"staging");
    float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{matrix,0,0};ComPtr<ID3D11Buffer> cb;
    Check(SUCCEEDED(device->CreateBuffer(&bd,&init,&cb)),"UI constants");
    D3D11_BLEND_DESC blendDesc{};auto& b=blendDesc.RenderTarget[0];
    b.BlendEnable=TRUE;b.SrcBlend=D3D11_BLEND_SRC_ALPHA;b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOp=D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha=D3D11_BLEND_ZERO;b.DestBlendAlpha=D3D11_BLEND_ONE;b.BlendOpAlpha=D3D11_BLEND_OP_ADD;b.RenderTargetWriteMask=7;
    ComPtr<ID3D11BlendState> blend;Check(SUCCEEDED(device->CreateBlendState(&blendDesc,&blend)),"game blend");
    D3D11_BUFFER_DESC vertexDesc{};vertexDesc.ByteWidth=32;vertexDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> vertexBuffer;Check(SUCCEEDED(device->CreateBuffer(&vertexDesc,nullptr,&vertexBuffer)),"fallback geometry buffer");
    auto* rt=rtv.Get();ctx->OMSetRenderTargets(1,&rt,nullptr);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    D3D11_VIEWPORT vp{0,0,8,4,0,1};ctx->RSSetViewports(1,&vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(ui.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);
    auto* constants=cb.Get();ctx->VSSetConstantBuffers(0,1,&constants);
    const float green[4]={0,1,0,1};ctx->ClearRenderTargetView(rt,green);
    g_p13Backbuffer.store(target.Get());g_p13ExtractUi.store(true);g_probeFrameCounter.store(4);
    ctx->Draw(3,0);
    Check(g_p13UiFrame==4&&g_p13UiDraws==1,"production UI draw isolated");
    auto pixel=[&](ID3D11Texture2D* texture){
        ctx->CopyResource(readback.Get(),texture);D3D11_MAPPED_SUBRESOURCE map{};
        Check(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)),"map pixel");
        uint32_t value=0;std::memcpy(&value,map.pData,4);ctx->Unmap(readback.Get(),0);return value;
    };
    Check(pixel(target.Get())==0xFF00FF00,"UI removed from world image, not merely duplicated");
    const uint32_t overlay=pixel(g_p13UiTexture);
    Check((overlay&0xff)>=127&&(overlay&0xff)<=128&&((overlay>>24)&0xff)>=127&&((overlay>>24)&0xff)<=128,"premultiplied color and alpha coverage");
    ID3D11RenderTargetView* restored=nullptr;ctx->OMGetRenderTargets(1,&restored,nullptr);
    Check(restored==rt,"RTV restored");restored->Release();
    ID3D11BlendState* restoredBlend=nullptr;FLOAT factors[4];UINT mask;
    ctx->OMGetBlendState(&restoredBlend,factors,&mask);Check(restoredBlend==blend.Get(),"blend restored");restoredBlend->Release();
    // P44 visual-fix policy: never infer UI from a draw's blend/depth state.
    // Full-screen post-process and scene effects can look like transparent UI;
    // only shaders explicitly tagged by the UI registry may be extracted.
    const auto fallbackBefore=g_p13UiDraws;
    UINT stride=16,offset=0;auto* vb=vertexBuffer.Get();ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    g_probeFrameCounter.store(5);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->VSSetShader(world.Get(),nullptr,0);ctx->Draw(3,0);
    Check(g_p13UiDraws==fallbackBefore&&g_p13UiFrame==4,"unclassified full-screen draw stays in scene");
    g_probeFrameCounter.store(6);ctx->OMSetBlendState(nullptr,nullptr,~0u);
    ID3D11Buffer* noVertexBuffer=nullptr;UINT zeroStride=0,zeroOffset=0;
    ctx->IASetVertexBuffers(0,1,&noVertexBuffer,&zeroStride,&zeroOffset);
    ctx->VSSetShader(world.Get(),nullptr,0);ctx->Draw(3,0);
    Check(g_p13UiFrame==4,"opaque world shader not redirected");Check(pixel(target.Get())!=0xFF00FF00,"world still renders");
    g_p13Backbuffer.store(nullptr);ctx->VSSetShader(ui.Get(),nullptr,0);ctx->Draw(3,0);
    Check(g_p13UiFrame==4,"offscreen/non-backbuffer UI untouched");
    // Repeated real pipeline transitions must not silently remove Draw hooks.
    g_p13Backbuffer.store(target.Get());
    for(uint64_t frame=10;frame<30;++frame){
        const auto before=g_p13UiDraws;
        g_probeFrameCounter.store(frame);
        ctx->OMSetRenderTargets(1,&rt,nullptr);
        ctx->PSSetShader(ps.Get(),nullptr,0);ctx->VSSetShader(ui.Get(),nullptr,0);
        ctx->OMSetBlendState(blend.Get(),nullptr,~0u);ctx->RSSetState(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3,0);
        Check(g_p13UiDraws==before+1&&g_p13UiFrame==frame,"HUD hook survives pipeline dispatch changes");
    }
    // The production repair path retains renderer/head tracking and the minimal
    // UI hooks, without restoring the old pixel-camera/LCD/reflection scopes.
    P13RepairContextHooks(ctx.Get());
    Check((*reinterpret_cast<void***>(ctx.Get()))[48]==reinterpret_cast<void*>(&HookContextUpdateSubresource),"runtime dispatch repair reinstalls camera update observer");
    g_p10ViewProjectionBuffer.store(cb.Get());auto revision=g_p13SourceRevision.load();
    ctx->UpdateSubresource(cb.Get(),0,nullptr,matrix,0,0);
    Check(g_p13SourceRevision.load()==revision+1,"camera CPU update invalidates cached copy");
    g_p10ViewProjectionBuffer.store(nullptr);
    OpenXRQuad bridge;bridge.gameplayVr=bridge.sessionRunning=true;
    bridge.xrWaitFrame=&WaitFrame;bridge.xrBeginFrame=&BeginFrame;bridge.xrEndFrame=&EndFrame;
    bridge.xrLocateSpace=&LocateHead;bridge.xrLocateViews=&LocateViews;
    bridge.localSpace=reinterpret_cast<XrSpace>(1);bridge.viewSpace=reinterpret_cast<XrSpace>(2);
    g_probeFrameCounter.store(42);bridge.P13BeginNextFrame(true);
    Check(waits==1&&begins==1&&ends==0&&bridge.p13FrameBegun,"begin before game render");
    Check(bridge.p12Pending.valid&&bridge.p12Pending.frame==42&&lastLocate==1000000000&&bridge.p12Pending.time==lastLocate,"exact predicted display time, no guessed extra period");
    Check(g_p12Constants.params[3]==1,"coherent camera-position update enabled");
    bridge.P13BeginNextFrame(true);Check(waits==1&&begins==1,"no double begin");
    bridge.P13DiscardOpenFrame();Check(ends==1&&lastEnd==1000000000&&!bridge.p13FrameBegun,"matching frame end time");
    XrCompositionLayerQuad hud{XR_TYPE_COMPOSITION_LAYER_QUAD};bridge.width=1920;bridge.height=1080;
    bridge.P13Quad(hud,reinterpret_cast<XrSwapchain>(3),true);
    Check(hud.eyeVisibility==XR_EYE_VISIBILITY_BOTH&&hud.space==bridge.viewSpace&&hud.layerFlags==XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,"one premultiplied HUD layer for both eyes");
    const auto reticle=OpenXRQuad::P13CenterReticleRect(1920,1080);
    Check(reticle.left==948&&reticle.top==528&&reticle.right==972&&reticle.bottom==552,
        "reticle mask removes only a centered 24x24 region at 1080p");
    const auto tinyReticle=OpenXRQuad::P13CenterReticleRect(8,4);
    Check(tinyReticle.left==0&&tinyReticle.top==0&&tinyReticle.right==8&&tinyReticle.bottom==4,
        "reticle mask clamps safely to tiny test textures");
    g_p13ExtractUi.store(false);P13ReleaseUi();ctx->ClearState();
    std::puts("PASS: bounded classifier, minimal D3D UI isolation/alpha/state restore, world exclusion, head-camera update tracking, exact XR frame timing and shared HUD layer.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}

