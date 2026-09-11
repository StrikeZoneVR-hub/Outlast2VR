namespace p33hud {
std::mutex mutex;
Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
uint64_t lastFrame=0,lastTick=0;
uint32_t sourceWidth=0,sourceHeight=0;
DXGI_FORMAT sourceFormat=DXGI_FORMAT_UNKNOWN;
bool logged=false,identityMismatchLogged=false;
struct OffSeen {void* resource=nullptr;uint64_t tick=0;};
std::array<OffSeen,48> offSeen{};size_t offCursor=0;

// P35: the live pass proved CameraHudPro-tex is one native 1920x1080 target,
// but the engine's RHI identity token is a wrapper and is NOT the ID3D11Resource
// passed to OMSetRenderTargets. Keep the exact UObject/name/size validation, then
// identify its D3D target by native dimensions + off/on activity instead of
// requiring pointer equality across two different abstraction layers.
inline bool NativeHudObject(void*& engineIdentity,uint32_t& w,uint32_t& h){
    engineIdentity=nullptr;w=h=0;
    auto* pawn=g_p20Pawn.load();void* controller=nullptr;void* hud=nullptr;
    void* nativeTexture=nullptr;void* klass=nullptr;int wi=0,hi=0;
    if(!pawn||!P19Read(pawn,0x6680,controller)||!controller||!P19Read(controller,0xc40,hud)||!hud||
       !P19Read(hud,0x7fc,nativeTexture)||!nativeTexture||!P19Read(nativeTexture,0x58,klass)||!klass||
       P19BoneName(static_cast<unsigned char*>(nativeTexture)+0x50)!="CameraHudPro-tex"||
       P19BoneName(static_cast<unsigned char*>(klass)+0x50)!="TextureRenderTarget2D"||
       !P19Read(nativeTexture,0x164,wi)||!P19Read(nativeTexture,0x168,hi)||
       wi<64||hi<32||wi>4096||hi>4096)return false;
    w=static_cast<uint32_t>(wi);h=static_cast<uint32_t>(hi);
    engineIdentity=p31bridge::ExpectedGpu(nativeTexture); // diagnostic/preference only in P35
    return true;
}
inline void MarkOff(void* resource,uint64_t now){
    if(!resource)return;
    for(auto& e:offSeen)if(e.resource==resource){e.tick=now;return;}
    offSeen[offCursor++%offSeen.size()]={resource,now};
}
inline bool RecentlyOff(void* resource,uint64_t now){
    for(const auto& e:offSeen)if(e.resource==resource&&e.tick&&now>=e.tick&&now-e.tick<1200)return true;
    return false;
}
inline bool MainSceneDepthBound(ID3D11DeviceContext* ctx){
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;ctx->OMGetRenderTargets(0,nullptr,&dsv);
    return dsv&&P11AClassifyDepthStencilView(dsv.Get())==P11ADepthPassKind::MainScene;
}
inline void Bound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views){
    if(ctx!=g_probeGameContext||!g_p10GameplayActive.load()||!P15Focused()||!views||count<1||count>8)return;
    void* engineIdentity=nullptr;uint32_t ew=0,eh=0;if(!NativeHudObject(engineIdentity,ew,eh))return;
    for(UINT i=1;i<count;++i)if(views[i])return; // native HUD is a single offscreen RT
    if(MainSceneDepthBound(ctx))return;
    const auto now=GetTickCount64();const bool cameraOn=p35runtime::CameraWanted();
    for(UINT i=0;i<count;++i){
        if(!views[i])continue;Microsoft::WRL::ComPtr<ID3D11Resource> r;views[i]->GetResource(&r);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> t;if(!r||FAILED(r.As(&t)))continue;
        if(t.Get()==g_p13Backbuffer.load())continue;
        D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
        if(d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||d.Width!=ew||d.Height!=eh)continue;
        if(!cameraOn){MarkOff(r.Get(),now);std::lock_guard<std::mutex> lock(mutex);if(texture.Get()==t.Get()){texture.Reset();lastTick=0;}continue;}
        const bool exact=engineIdentity&&r.Get()==engineIdentity;
        if(!exact&&RecentlyOff(r.Get(),now))continue; // normal 1920x1080 targets seen with camera OFF are rejected
        if(engineIdentity&&!exact&&!identityMismatchLogged){identityMismatchLogged=true;Log("P35 CAMERA HUD RHI BRIDGE: CameraHudPro-tex engine identity differs from the bound D3D resource as observed in the live pass; using validated dimensions + camera activity correlation.");}
        {std::lock_guard<std::mutex> lock(mutex);texture=t;lastFrame=g_probeFrameCounter.load();lastTick=now;sourceWidth=d.Width;sourceHeight=d.Height;sourceFormat=d.Format;}
        if(!logged){logged=true;Log("P35 CAMERA HUD GPU TARGET ACQUIRED: CameraHudPro-tex %ux%u format=%u; strict engine-RHI/D3D identity matching removed.",d.Width,d.Height,unsigned(d.Format));}
        return;
    }
}
inline ID3D11Texture2D* Acquire(){
    std::lock_guard<std::mutex> lock(mutex);
    if(!p35runtime::CameraWanted()||!texture||!lastTick||GetTickCount64()-lastTick>180)return nullptr;
    texture->AddRef();return texture.Get();
}
}
void P33HudTargetBound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views){p33hud::Bound(ctx,count,views);}
ID3D11Texture2D* P33AcquireHudTexture(){return p33hud::Acquire();}
