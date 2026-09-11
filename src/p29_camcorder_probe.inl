#include "p29_camcorder_target.h"

struct P29TargetSeen {
    uintptr_t resource{};
    uint32_t width{};
    uint32_t height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    uint64_t draws{};
    uint64_t lastFrame{};
};

bool g_p29ConfigRead=false;
bool g_p29ProbeEnabled=true;
std::vector<P29TargetSeen> g_p29Targets;
uint64_t g_p29Rejected=0;
uint64_t g_p29LastSummaryFrame=0;
bool g_p29BoundCandidate=false;
uint32_t g_p29NativeWidth=0,g_p29NativeHeight=0;
uint64_t g_p29NativeFrame=~0ull;
void* g_p29NativeTexture=nullptr;
void* g_p29NativeResource=nullptr;

bool P29CameraRaised(){
    auto* pawn=g_p20Pawn.load();unsigned char state=255;
    return pawn&&P19Read(pawn,0x68bd,state)&&state==1;
}

void P29LoadConfig(){
    if(g_p29ConfigRead)return;g_p29ConfigRead=true;
    g_p29ProbeEnabled=GetPrivateProfileIntW(L"VR",L"CamcorderTargetProbe",1,(ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;
    Log("P29B camcorder target probe: %s; read-only D3D11 observation, no render-target writes.",g_p29ProbeEnabled?"enabled":"disabled");
}

bool P29ReadNativeTarget(){
    const auto frame=g_probeFrameCounter.load();
    if(frame==g_p29NativeFrame)return g_p29NativeWidth!=0;
    g_p29NativeFrame=frame;g_p29NativeWidth=g_p29NativeHeight=0;
    auto* pawn=g_p20Pawn.load();void* controller=nullptr;void* hud=nullptr;
    void* texture=nullptr;void* klass=nullptr;void* resource=nullptr;int w=0,h=0;
    if(!P29CameraRaised()||!P19Read(pawn,0x6680,controller)||!P19Read(controller,0xc40,hud)||
       !P19Read(hud,0x7fc,texture)||!texture||!P19Read(texture,0x58,klass)||!klass||
       P19BoneName(static_cast<unsigned char*>(texture)+0x50)!="CameraHudPro-tex"||
       P19BoneName(static_cast<unsigned char*>(klass)+0x50)!="TextureRenderTarget2D"||
       !P19Read(texture,0x164,w)||!P19Read(texture,0x168,h)||
       w<128||h<64||w>4096||h>4096||!P19Read(texture,0x110,resource)||!resource)return false;
    g_p29NativeWidth=static_cast<uint32_t>(w);g_p29NativeHeight=static_cast<uint32_t>(h);
    if(texture!=g_p29NativeTexture||resource!=g_p29NativeResource){
        g_p29NativeTexture=texture;g_p29NativeResource=resource;
        Log("P29B NATIVE HUD TEXTURE: object=%p resource=%p size=%dx%d; exact UObject name/class verified. GPU candidates still require identity confirmation.",texture,resource,w,h);
    }
    return true;
}

// Observe target changes, not every scene draw. The old UI detector returned
// None for all native Scaleform shaders, so it cannot be a prerequisite here.
void P29TargetBound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views){
    if(ctx!=g_probeGameContext)return;
    g_p29BoundCandidate=false;P29LoadConfig();
    if(!g_p29ProbeEnabled||!g_p10GameplayActive.load()||!P15Focused()||
       count<1||count>8||!views||!views[0]||!P29ReadNativeTarget())return;
    for(UINT i=1;i<count;++i)if(views[i])return;
    ID3D11Resource* resource=nullptr;views[0]->GetResource(&resource);
    ID3D11Texture2D* texture=nullptr;
    if(resource)resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
    if(texture){D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        g_p29BoundCandidate=p29::MatchesNativeTarget(d.Width,d.Height,d.SampleDesc.Count,
            g_p29NativeWidth,g_p29NativeHeight,true,resource==g_p13Backbuffer.load());
        texture->Release();}
    if(resource)resource->Release();
}

void P29LogSrvSet(ID3D11DeviceContext* ctx){
    ID3D11ShaderResourceView* srvs[8]{};ctx->PSGetShaderResources(0,8,srvs);
    for(UINT i=0;i<8;++i){
        if(!srvs[i])continue;
        ID3D11Resource* r=nullptr;srvs[i]->GetResource(&r);ID3D11Texture2D* t=nullptr;
        if(r)r->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&t));
        if(t){D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);Log("P29B CAM HUD SRV: slot=%u tex=%p %ux%u fmt=%u samples=%u bind=0x%x",i,t,d.Width,d.Height,static_cast<unsigned>(d.Format),d.SampleDesc.Count,d.BindFlags);t->Release();}
        if(r)r->Release();srvs[i]->Release();
    }
}

void P29ObserveCurrentUiTarget(ID3D11DeviceContext* ctx,p13::UiShader ui){
    P29LoadConfig();
    if(!g_p29ProbeEnabled||ctx!=g_probeGameContext||!g_p29BoundCandidate||!g_p10GameplayActive.load()||!P15Focused()||!P29CameraRaised())return;
    ID3D11RenderTargetView* rtvs[8]{};ID3D11DepthStencilView* dsv=nullptr;ctx->OMGetRenderTargets(8,rtvs,&dsv);
    if(!rtvs[0]){if(dsv)dsv->Release();return;}
    bool single=true;for(int i=1;i<8;++i)if(rtvs[i])single=false;
    ID3D11Resource* resource=nullptr;rtvs[0]->GetResource(&resource);ID3D11Texture2D* texture=nullptr;
    if(resource)resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));
    D3D11_TEXTURE2D_DESC td{};if(texture)texture->GetDesc(&td);
    const bool backbuffer=resource&&resource==g_p13Backbuffer.load();
    if(texture&&p29::MatchesNativeTarget(td.Width,td.Height,td.SampleDesc.Count,g_p29NativeWidth,g_p29NativeHeight,single,backbuffer)){
        const uintptr_t key=reinterpret_cast<uintptr_t>(resource);auto it=std::find_if(g_p29Targets.begin(),g_p29Targets.end(),[&](const P29TargetSeen& s){return s.resource==key;});
        if(it==g_p29Targets.end()&&g_p29Targets.size()<32){g_p29Targets.push_back({key,td.Width,td.Height,td.Format,0,0});it=std::prev(g_p29Targets.end());}
        if(it!=g_p29Targets.end()){
            ++it->draws;it->lastFrame=g_probeFrameCounter.load();const uint64_t n=it->draws;
            if(n==1||n==8||n==32){
                D3D11_VIEWPORT vp{};UINT vpc=1;ctx->RSGetViewports(&vpc,&vp);
                ID3D11BlendState* blend=nullptr;FLOAT factor[4]{};UINT mask=0;ctx->OMGetBlendState(&blend,factor,&mask);D3D11_BLEND_DESC bd{};if(blend)blend->GetDesc(&bd);
                ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ctx->VSGetShader(&vs,nullptr,nullptr);ctx->PSGetShader(&ps,nullptr,nullptr);
                Log("P29B CAM HUD TARGET: frame=%llu draw=%llu resource=%p rtv=%p %ux%u fmt=%u samples=%u ui=%u dsv=%d viewport=%.0fx%.0f blend=%d src=%u dst=%u vs=%p ps=%p",
                    static_cast<unsigned long long>(g_probeFrameCounter.load()),static_cast<unsigned long long>(n),resource,rtvs[0],td.Width,td.Height,static_cast<unsigned>(td.Format),td.SampleDesc.Count,static_cast<unsigned>(ui),dsv?1:0,vpc?vp.Width:0.0f,vpc?vp.Height:0.0f,bd.RenderTarget[0].BlendEnable?1:0,static_cast<unsigned>(bd.RenderTarget[0].SrcBlend),static_cast<unsigned>(bd.RenderTarget[0].DestBlend),vs,ps);
                P29LogSrvSet(ctx);
                if(vs)vs->Release();if(ps)ps->Release();if(blend)blend->Release();
            }
        }
    }else ++g_p29Rejected;
    if(texture)texture->Release();if(resource)resource->Release();if(dsv)dsv->Release();for(auto* r:rtvs)if(r)r->Release();
}

void P29FrameSummary(){
    P29LoadConfig();if(!g_p29ProbeEnabled)return;
    const uint64_t frame=g_probeFrameCounter.load();if(frame<g_p29LastSummaryFrame+120)return;g_p29LastSummaryFrame=frame;
    const P29TargetSeen* best=nullptr;for(const auto& t:g_p29Targets)if(!best||t.draws>best->draws)best=&t;
    if(best)Log("P29B CAM HUD SUMMARY: frame=%llu candidates=%zu rejected=%llu best=%p %ux%u fmt=%u draws=%llu lastFrame=%llu. Keep camcorder raised 3-5 seconds, then send outlast2_vr_p32.log.",
        static_cast<unsigned long long>(frame),g_p29Targets.size(),static_cast<unsigned long long>(g_p29Rejected),reinterpret_cast<void*>(best->resource),best->width,best->height,static_cast<unsigned>(best->format),static_cast<unsigned long long>(best->draws),static_cast<unsigned long long>(best->lastFrame));
    else Log("P29B CAM HUD SUMMARY: frame=%llu no offscreen UI target accepted yet. Raise the camcorder and keep it visible 3-5 seconds.",static_cast<unsigned long long>(frame));
}




