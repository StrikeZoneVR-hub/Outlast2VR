// Included in the bridge namespace after PatchContextSlot. Does not alter game
// shaders, guessed engine objects, or any world-space vertices.
constexpr GUID kP13UiTag={0x88a39f65,0xa781,0x4af2,{0x93,0xa1,0xee,0x7b,0x54,0x92,0x10,0x13}};
using P13CreateVSFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using P13SetVSFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance*const*,UINT);
using P13DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using P13DrawIndexedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using P13DrawInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
using P13DrawIndexedInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
P13CreateVSFn g_p13CreateVS=nullptr;
p14::UiRegistry g_p14UiRegistry;
constexpr GUID kP14CameraTag={0x88a39f65,0xa781,0x4af2,{0x93,0xa1,0xee,0x7b,0x54,0x92,0x10,0x14}};
using P14CreatePSFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
P14CreatePSFn g_p14CreatePS=nullptr;
std::map<std::vector<uint8_t>,std::string> g_p14CameraLayouts;
void P14RegisterCameraLayout(const void* bytes,size_t size){
    auto key=p14::Instructions(bytes,size);if(key.size()<4)return;
    uint32_t token=0;std::memcpy(&token,key.data(),4);if((token>>16)!=0)return;
    using ReflectFn=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
    static HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static ReflectFn reflect=compiler?reinterpret_cast<ReflectFn>(GetProcAddress(compiler,"D3DReflect")):nullptr;
    if(!reflect)return;
    ID3D11ShaderReflection* reflection=nullptr;
    if(FAILED(reflect(bytes,size,__uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(&reflection))))return;
    D3D11_SHADER_DESC sd{};std::string summary;
    if(SUCCEEDED(reflection->GetDesc(&sd)))for(UINT i=0;i<sd.ConstantBuffers;++i){
        auto* cb=reflection->GetConstantBufferByIndex(i);D3D11_SHADER_BUFFER_DESC bd{};
        if(FAILED(cb->GetDesc(&bd)))continue;
        D3D11_SHADER_INPUT_BIND_DESC binding{};if(FAILED(reflection->GetResourceBindingDescByName(bd.Name,&binding)))continue;
        for(UINT j=0;j<bd.Variables;++j){
            D3D11_SHADER_VARIABLE_DESC vd{};if(FAILED(cb->GetVariableByIndex(j)->GetDesc(&vd)))continue;
            if(std::strstr(vd.Name,"ScreenToWorld")||!std::strcmp(vd.Name,"CameraPositionPS")||
               !std::strcmp(vd.Name,"ProjectionInvScale")||!std::strcmp(vd.Name,"ViewProjectionMatrixPS")){
                char text[256]{};sprintf_s(text,"b%u:%s[%u] %s@%u+%u; ",binding.BindPoint,bd.Name,bd.Size,vd.Name,vd.StartOffset,vd.Size);
                summary+=text;
            }
        }
    }
    reflection->Release();
    auto [it,inserted]=g_p14CameraLayouts.emplace(std::move(key),summary);
    if(!inserted&&it->second!=summary)it->second.clear(); // ambiguous layout is never used
}
HRESULT STDMETHODCALLTYPE P14CreatePS(ID3D11Device* self,const void* bytes,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** out){
    const HRESULT hr=g_p14CreatePS(self,bytes,size,linkage,out);
    if(SUCCEEDED(hr)&&out&&*out){
        P17TagPixelShader(bytes,size,*out);
        const auto it=g_p14CameraLayouts.find(p14::Instructions(bytes,size));
        if(it!=g_p14CameraLayouts.end()&&!it->second.empty()&&it->second.size()<2048)
            (*out)->SetPrivateData(kP14CameraTag,static_cast<UINT>(it->second.size()+1),it->second.c_str());
    }
    return hr;
}
void P14ObservePS(ID3D11DeviceContext* ctx,ID3D11PixelShader* shader){
    if(ctx!=g_probeGameContext||!shader)return;
    char text[2048]{};UINT size=sizeof(text);
    if(FAILED(shader->GetPrivateData(kP14CameraTag,&size,text)))return;
    text[2047]=0;
    static std::vector<std::string> seen;
    if(seen.size()<32&&std::find(seen.begin(),seen.end(),text)==seen.end()){
        seen.emplace_back(text);Log("P14 CAMERA RECONSTRUCTION SHADER BOUND (diagnostic only, not patched): %s",text);
    }
}
ID3D11Device* g_p14Device=nullptr; // borrowed; used only while its immediate context is alive
std::atomic<uint64_t> g_p14VsCreated{0},g_p14VsTagged{0},g_p14VsMatched{0},g_p14DrawObserved{0};
void P14LoadUiRegistry(){
    wchar_t path[32768]{};const DWORD len=GetModuleFileNameW(nullptr,path,32768);
    if(!len||len>=32768)return;
    wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;*slash=0;
    const std::wstring cache=std::wstring(path)+L"\\..\\..\\OLGame\\CookedPCConsole\\GlobalShaderCache-PC-D3D-SM5.bin";
    FILE* file=nullptr;_wfopen_s(&file,cache.c_str(),L"rb");
    if(!file){Log("P14 UI CACHE unavailable: metadata-only classification remains active.");return;}
    _fseeki64(file,0,SEEK_END);const auto size=_ftelli64(file);_fseeki64(file,0,SEEK_SET);
    if(size<32||size>32*1024*1024){fclose(file);return;}
    std::vector<uint8_t> data(static_cast<size_t>(size));
    const bool ok=fread(data.data(),1,data.size(),file)==data.size();fclose(file);if(!ok)return;
    for(size_t o=0;o+32<=data.size();++o)if(!std::memcmp(data.data()+o,"DXBC",4)){
        uint32_t n=0;std::memcpy(&n,data.data()+o+24,4);
        if(n>=32&&n<=data.size()-o){g_p14UiRegistry.Add(data.data()+o,n);P14RegisterCameraLayout(data.data()+o,n);}
    }
    size_t ui=0;for(const auto& entry:g_p14UiRegistry.known)if(entry.second!=p13::UiShader::None)++ui;
    Log("P14 UI CACHE READY: %zu unambiguous UI instruction identities; stripped shader metadata is optional.",ui);
}
P13SetVSFn g_p13SetVS=nullptr;
P13DrawFn g_p13Draw=nullptr;
P13DrawIndexedFn g_p13DrawIndexed=nullptr;
P13DrawInstancedFn g_p13DrawInstanced=nullptr;
P13DrawIndexedInstancedFn g_p13DrawIndexedInstanced=nullptr;
using P13CopyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
using P13CopyRegionFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,UINT,UINT,UINT,ID3D11Resource*,UINT,const D3D11_BOX*);
P13CopyFn g_p13Copy=nullptr;
P13CopyRegionFn g_p13CopyRegion=nullptr;
using P13SetPSFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance*const*,UINT);
using P13SetBlendFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11BlendState*,const FLOAT*,UINT);
using P13SetDepthFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilState*,UINT);
using P13SetRasterFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RasterizerState*);
using P13SetTopologyFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,D3D11_PRIMITIVE_TOPOLOGY);
P13SetPSFn g_p13SetPS=nullptr;P13SetBlendFn g_p13SetBlend=nullptr;
P13SetDepthFn g_p13SetDepth=nullptr;P13SetRasterFn g_p13SetRaster=nullptr;P13SetTopologyFn g_p13SetTopology=nullptr;
void STDMETHODCALLTYPE P13SetPS(ID3D11DeviceContext* c,ID3D11PixelShader* s,ID3D11ClassInstance*const* classes,UINT n){g_p13SetPS(c,s,classes,n);P13RepairContextHooks(c);P14ObservePS(c,s);}
void STDMETHODCALLTYPE P13SetBlend(ID3D11DeviceContext* c,ID3D11BlendState* b,const FLOAT* f,UINT mask){g_p13SetBlend(c,b,f,mask);P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13SetDepth(ID3D11DeviceContext* c,ID3D11DepthStencilState* d,UINT ref){g_p13SetDepth(c,d,ref);P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13SetRaster(ID3D11DeviceContext* c,ID3D11RasterizerState* r){g_p13SetRaster(c,r);P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13SetTopology(ID3D11DeviceContext* c,D3D11_PRIMITIVE_TOPOLOGY t){g_p13SetTopology(c,t);P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13Copy(ID3D11DeviceContext* c,ID3D11Resource* dst,ID3D11Resource* src){
    g_p13Copy(c,dst,src);P15InvalidateGpuCopy(dst);if(dst==g_p10ViewProjectionBuffer.load())g_p13SourceRevision.fetch_add(1);P13RepairContextHooks(c);
}
void STDMETHODCALLTYPE P13CopyRegion(ID3D11DeviceContext* c,ID3D11Resource* dst,UINT ds,UINT x,UINT y,UINT z,ID3D11Resource* src,UINT ss,const D3D11_BOX* box){
    g_p13CopyRegion(c,dst,ds,x,y,z,src,ss,box);P15InvalidateGpuCopy(dst);if(dst==g_p10ViewProjectionBuffer.load())g_p13SourceRevision.fetch_add(1);P13RepairContextHooks(c);
}
std::atomic<bool> g_p13ExtractUi{false};
std::atomic<ID3D11Resource*> g_p13Backbuffer{nullptr}; // borrowed identity only; never dereferenced
p13::UiShader g_p13CurrentUi=p13::UiShader::None; // immediate-context thread only
ID3D11Texture2D* g_p13UiTexture=nullptr;
ID3D11RenderTargetView* g_p13UiRtv=nullptr;
uint64_t g_p13UiFrame=~0ull;
uint64_t g_p13UiDraws=0;
uint64_t g_p13UiCandidates=0;
bool g_p13UiLogged=false;
bool g_p13FallbackUiLogged=false;
struct P13BlendPair { ID3D11BlendState* original; ID3D11BlendState* overlay; };
std::vector<P13BlendPair> g_p13UiBlends;

void P13ReleaseUi() {
    if(g_p13UiRtv)g_p13UiRtv->Release();g_p13UiRtv=nullptr;
    if(g_p13UiTexture)g_p13UiTexture->Release();g_p13UiTexture=nullptr;
    for(auto& b:g_p13UiBlends){if(b.original)b.original->Release();if(b.overlay)b.overlay->Release();}
    g_p13UiBlends.clear();g_p13UiFrame=~0ull;
}

HRESULT STDMETHODCALLTYPE P13CreateVS(ID3D11Device* self,const void* bytes,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11VertexShader** out) {
    const HRESULT hr=g_p13CreateVS(self,bytes,size,linkage,out);
    if(SUCCEEDED(hr)&&out&&*out){
        
        ++g_p14VsCreated;
        auto kind=p13::ClassifyUiShader(bytes,size);
        if(kind==p13::UiShader::None){kind=g_p14UiRegistry.Find(bytes,size);if(kind!=p13::UiShader::None)++g_p14VsMatched;}
        if(kind!=p13::UiShader::None)(*out)->SetPrivateData(kP13UiTag,sizeof(kind),&kind);
        if(kind!=p13::UiShader::None&&++g_p14VsTagged<=8)Log("P14 UI SHADER TAGGED: kind=%u bytes=%zu instructionFallbackTotal=%llu",static_cast<unsigned>(kind),size,static_cast<unsigned long long>(g_p14VsMatched.load()));
    }
    return hr;
}

bool P13LooksLikeUnclassifiedScreenUi(const D3D11_BLEND_DESC& blendDesc,
    ID3D11DepthStencilView* dsv,const D3D11_DEPTH_STENCIL_DESC& depthDesc,
    bool geometryBacked) {
    // P44 normally identifies Scaleform/Canvas vertex shaders at creation time.
    // The game can create those shaders before the D3D dispatch hook is installed,
    // leaving the actual prompt/camcorder HUD untagged. Only accept the narrow
    // screen-space signature here: one alpha-blended target, no depth testing,
    // and the standard premultiplied/straight-alpha HUD blend factors.
    const auto& color=blendDesc.RenderTarget[0];
    if(!color.BlendEnable||color.BlendOp!=D3D11_BLEND_OP_ADD||
       color.DestBlend!=D3D11_BLEND_INV_SRC_ALPHA||
       (color.SrcBlend!=D3D11_BLEND_ONE&&color.SrcBlend!=D3D11_BLEND_SRC_ALPHA))return false;
    // A procedural full-screen triangle/quad has no vertex or index buffer and
    // is commonly used by scene post-process passes. Never move that draw into
    // the transparent HUD texture: doing so duplicates the whole level as a
    // panel over the stereo image. Actual Scaleform/Canvas draws are backed by
    // geometry even when their shader metadata is unavailable.
    return (!dsv||!depthDesc.DepthEnable)&&geometryBacked;
}

bool P13HasBoundGeometry(ID3D11DeviceContext* ctx) {
    ID3D11Buffer* vertex=nullptr;UINT stride=0,offset=0;
    ctx->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);
    ID3D11Buffer* index=nullptr;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT indexOffset=0;
    ctx->IAGetIndexBuffer(&index,&format,&indexOffset);
    const bool backed=vertex||index;
    if(vertex)vertex->Release();if(index)index->Release();
    return backed;
}

void STDMETHODCALLTYPE P13SetVS(ID3D11DeviceContext* self,ID3D11VertexShader* shader,ID3D11ClassInstance*const* classes,UINT count) {
    g_p13SetVS(self,shader,classes,count);
    P13RepairContextHooks(self);
    if(self==g_probeGameContext){
        g_p13CurrentUi=p13::UiShader::None;
        UINT size=sizeof(g_p13CurrentUi);
        if(shader)shader->GetPrivateData(kP13UiTag,&size,&g_p13CurrentUi);
    }
}

struct P13UiDrawScope {
    ID3D11DeviceContext* ctx;
    ID3D11RenderTargetView* rtvs[8]{};
    ID3D11DepthStencilView* dsv=nullptr;
    ID3D11BlendState* blend=nullptr;
    FLOAT blendFactor[4]{};UINT sampleMask=0;
    bool redirected=false;
    explicit P13UiDrawScope(ID3D11DeviceContext* context):ctx(context) {
        P13RepairContextHooks(ctx);
        if(ctx==g_probeGameContext){
            ++g_p14DrawObserved;
            // Runtime state transitions and ClearState can invalidate the cached shader.
            if(g_p13ExtractUi.load()){
                ID3D11VertexShader* vs=nullptr;ctx->VSGetShader(&vs,nullptr,nullptr);
                g_p13CurrentUi=p13::UiShader::None;UINT size=sizeof(g_p13CurrentUi);
                if(vs){vs->GetPrivateData(kP13UiTag,&size,&g_p13CurrentUi);vs->Release();}
            }
        }
        // Always inspect draws after native gameplay begins. Context-state
        // restoration can suppress OM/CB rebinding callbacks, and the camera
        // cannot be validated until this draw observer first sees the real
        // full-resolution scene pass. Requiring validation here created a
        // circular gate that left gameplay on the world screen indefinitely.
        // PF16A: validate on startup, frame change, or source revision change.
        // PF15 queried the output-merger and camera binding on every draw call;
        // the live log showed no draw-time repair, while that hot path caused
        // avoidable D3D11 work for the first minutes of a level.
        const bool cameraValidated=g_p10ViewProjectionValidated.load(std::memory_order_acquire);
        if(ctx==g_probeGameContext&&g_p10GameplayActive.load()&&
            (!cameraValidated||g_p10GpuShadowFrame!=g_probeFrameCounter.load()||
             g_p13CachedRevision.load()!=g_p13SourceRevision.load())){
            ID3D11DepthStencilView* depth=nullptr;ctx->OMGetRenderTargets(0,nullptr,&depth);
            P11BRefreshCameraBindingForDepthPass(ctx,depth);if(depth)depth->Release();
        }
        if(ctx==g_probeGameContext)P29ObserveCurrentUiTarget(ctx,g_p13CurrentUi);
        if(ctx!=g_probeGameContext||!g_p13ExtractUi.load())return;
        ++g_p13UiCandidates;
        ctx->OMGetRenderTargets(8,rtvs,&dsv);
        if(!rtvs[0])return;
        for(int i=1;i<8;++i)if(rtvs[i])return; // never redirect MRT scene passes
        ID3D11Resource* target=nullptr;rtvs[0]->GetResource(&target);
        const bool isBackbuffer=target==g_p13Backbuffer.load();
        if(target)target->Release();if(!isBackbuffer)return;
        ID3D11DepthStencilState* ds=nullptr;UINT stencilRef=0;
        ctx->OMGetDepthStencilState(&ds,&stencilRef);
        D3D11_DEPTH_STENCIL_DESC dd{};
        if(ds){ds->GetDesc(&dd);ds->Release();}else dd.DepthEnable=TRUE;
        // Null DSV disables depth. If one is bound, UI must explicitly disable it.
        if(dsv&&dd.DepthEnable)return;
        ctx->OMGetBlendState(&blend,blendFactor,&sampleMask);
        D3D11_BLEND_DESC bd{};
        if(blend)blend->GetDesc(&bd);
        else bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        const auto& color=bd.RenderTarget[0];
        if(!(color.RenderTargetWriteMask&7))return; // leave stencil-only draws alone
        if(color.BlendEnable&&(color.BlendOp!=D3D11_BLEND_OP_ADD||color.DestBlend!=D3D11_BLEND_INV_SRC_ALPHA||
           (color.SrcBlend!=D3D11_BLEND_ONE&&color.SrcBlend!=D3D11_BLEND_SRC_ALPHA)))return;
        const bool taggedUi=g_p13CurrentUi!=p13::UiShader::None;
        // P44's heuristic fallback can mistake a translucent full-screen
        // post-process draw for HUD. Redirecting that draw creates the exact
        // ghosted level-shaped overlay seen in the headset. Keep only shaders
        // positively identified as Scaleform/Canvas until a renderer-specific
        // signature for the remaining unclassified HUD is available.
        const bool fallbackUi=false;
        if(!taggedUi&&!fallbackUi)return;
        if(fallbackUi&&!g_p13FallbackUiLogged){
            g_p13FallbackUiLogged=true;
            Log("P44 UI FALLBACK ACTIVE: untagged alpha/depth-disabled backbuffer draws are isolated as screen-space HUD; P44 projection unchanged.");
        }
        ID3D11Device* dev=nullptr;ctx->GetDevice(&dev);if(!dev)return;
        if(!g_p13UiTexture){
            ID3D11Resource* resource=nullptr;rtvs[0]->GetResource(&resource);
            ID3D11Texture2D* texture=nullptr;
            if(resource){resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture));resource->Release();}
            D3D11_TEXTURE2D_DESC td{};if(texture){texture->GetDesc(&td);texture->Release();}
            td.BindFlags=D3D11_BIND_RENDER_TARGET;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=0;td.MiscFlags=0;
            // Share the original stencil surface only with compatible dimensions/sample count.
            if(!td.Width||td.SampleDesc.Count!=1||FAILED(dev->CreateTexture2D(&td,nullptr,&g_p13UiTexture))||
               FAILED(dev->CreateRenderTargetView(g_p13UiTexture,nullptr,&g_p13UiRtv))){
                P13ReleaseUi();dev->Release();return;
            }
        }
        ID3D11BlendState* overlay=nullptr;
        for(const auto& pair:g_p13UiBlends)if(pair.original==blend){overlay=pair.overlay;break;}
        if(!overlay){
            if(g_p13UiBlends.size()>=64){dev->Release();return;}
            // Retain game RGB blending but accumulate premultiplied alpha coverage.
            bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask|=D3D11_COLOR_WRITE_ENABLE_ALPHA;
            if(!bd.RenderTarget[0].BlendEnable){
                bd.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO;
                bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
            }
            if(FAILED(dev->CreateBlendState(&bd,&overlay))){dev->Release();return;}
            if(blend)blend->AddRef();g_p13UiBlends.push_back({blend,overlay});
        }
        dev->Release();
        const uint64_t frame=g_probeFrameCounter.load();
        if(g_p13UiFrame!=frame){const float clear[4]{};ctx->ClearRenderTargetView(g_p13UiRtv,clear);g_p13UiFrame=frame;}
        g_realOMSetRenderTargets(ctx,1,&g_p13UiRtv,dsv);
        ctx->OMSetBlendState(overlay,blendFactor,sampleMask);
        redirected=true;++g_p13UiDraws;
        if(!g_p13UiLogged){g_p13UiLogged=true;Log("P13 UI ISOLATION ACTIVE: validated HUD/Scaleform shader draws redirected from backbuffer to a shared transparent layer. kind=%u",static_cast<unsigned>(g_p13CurrentUi));}
    }
    ~P13UiDrawScope(){
        if(redirected){g_realOMSetRenderTargets(ctx,8,rtvs,dsv);ctx->OMSetBlendState(blend,blendFactor,sampleMask);}
        if(blend)blend->Release();if(dsv)dsv->Release();for(auto* r:rtvs)if(r)r->Release();
    }
};
void STDMETHODCALLTYPE P13Draw(ID3D11DeviceContext* c,UINT a,UINT b){P13UiDrawScope ui(c);{P15PixelScope pixel(c);g_p13Draw(c,a,b);}P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13DrawIndexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d){P13UiDrawScope ui(c);{P15PixelScope pixel(c);g_p13DrawIndexed(c,a,b,d);}P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13DrawInstanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e){P13UiDrawScope ui(c);{P15PixelScope pixel(c);g_p13DrawInstanced(c,a,b,d,e);}P13RepairContextHooks(c);}
void STDMETHODCALLTYPE P13DrawIndexedInstanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f){P13UiDrawScope ui(c);{P15PixelScope pixel(c);g_p13DrawIndexedInstanced(c,a,b,d,e,f);}P13RepairContextHooks(c);}

void P13RepairContextHooks(ID3D11DeviceContext* context){
    if(!context||context!=g_probeGameContext||!g_matrixProbeHooksInstalled.load())return;
    // P44 repairs the six renderer/head-tracking hooks plus only the five
    // context entries needed to classify and redirect final-target UI draws.
    // PF13 pixel correction is scoped inside the draw wrappers; no additional
    // device-state hooks or LCD capture are installed.
    if(g_p14Device&&g_p13CreateVS){
        void** deviceTable=*reinterpret_cast<void***>(g_p14Device);
        if(deviceTable[12]!=reinterpret_cast<void*>(&P13CreateVS))
            PatchContextSlot(deviceTable,12,reinterpret_cast<void*>(&P13CreateVS),reinterpret_cast<void**>(&g_p13CreateVS));
    }
    void** table=*reinterpret_cast<void***>(context);
    struct Entry{size_t slot;void* hook;void** original;};
    Entry entries[]={
        {7,reinterpret_cast<void*>(&HookVSSetConstantBuffers),reinterpret_cast<void**>(&g_realVSSetConstantBuffers)},
        {14,reinterpret_cast<void*>(&HookContextMap),reinterpret_cast<void**>(&g_realContextMap)},
        {15,reinterpret_cast<void*>(&HookContextUnmap),reinterpret_cast<void**>(&g_realContextUnmap)},
        {29,reinterpret_cast<void*>(&HookContextGetData),reinterpret_cast<void**>(&g_realContextGetData)},
        {30,reinterpret_cast<void*>(&HookContextSetPredication),reinterpret_cast<void**>(&g_realContextSetPredication)},
        {33,reinterpret_cast<void*>(&HookOMSetRenderTargets),reinterpret_cast<void**>(&g_realOMSetRenderTargets)},
        {34,reinterpret_cast<void*>(&HookOMSetRenderTargetsUAV),reinterpret_cast<void**>(&g_realOMSetRenderTargetsUAV)},
        {48,reinterpret_cast<void*>(&HookContextUpdateSubresource),reinterpret_cast<void**>(&g_realUpdateSubresource)},
        {11,reinterpret_cast<void*>(&P13SetVS),reinterpret_cast<void**>(&g_p13SetVS)},
        {12,reinterpret_cast<void*>(&P13DrawIndexed),reinterpret_cast<void**>(&g_p13DrawIndexed)},
        {13,reinterpret_cast<void*>(&P13Draw),reinterpret_cast<void**>(&g_p13Draw)},
        {20,reinterpret_cast<void*>(&P13DrawIndexedInstanced),reinterpret_cast<void**>(&g_p13DrawIndexedInstanced)},
        {21,reinterpret_cast<void*>(&P13DrawInstanced),reinterpret_cast<void**>(&g_p13DrawInstanced)}};
    static uint64_t loggedSlots=0;
    for(auto& e:entries)if(*e.original&&table[e.slot]!=e.hook){
        if(PatchContextSlot(table,e.slot,e.hook,e.original)&&!(loggedSlots&(1ull<<e.slot))){
            loggedSlots|=1ull<<e.slot;
            Log("P44 CORE/UI D3D DISPATCH REBOUND: slot=%zu.",e.slot);
        }
    }
}

bool P13InstallUiHooks(ID3D11DeviceContext* context) {
    ID3D11Device* device=nullptr;context->GetDevice(&device);if(!device)return false;
    g_p14Device=device; // borrowed while the immediate context owns the device
    P14LoadUiRegistry();
    void** deviceTable=*reinterpret_cast<void***>(device);
    void** contextTable=*reinterpret_cast<void***>(context);
    struct Site{void** table;size_t index;void* hook;void** original;};
    Site sites[]={
        {deviceTable,12,reinterpret_cast<void*>(&P13CreateVS),reinterpret_cast<void**>(&g_p13CreateVS)},
        {contextTable,11,reinterpret_cast<void*>(&P13SetVS),reinterpret_cast<void**>(&g_p13SetVS)},
        {contextTable,12,reinterpret_cast<void*>(&P13DrawIndexed),reinterpret_cast<void**>(&g_p13DrawIndexed)},
        {contextTable,13,reinterpret_cast<void*>(&P13Draw),reinterpret_cast<void**>(&g_p13Draw)},
        {contextTable,20,reinterpret_cast<void*>(&P13DrawIndexedInstanced),reinterpret_cast<void**>(&g_p13DrawIndexedInstanced)},
        {contextTable,21,reinterpret_cast<void*>(&P13DrawInstanced),reinterpret_cast<void**>(&g_p13DrawInstanced)}};
    size_t done=0;
    for(;done<std::size(sites);++done)
        if(!PatchContextSlot(sites[done].table,sites[done].index,sites[done].hook,sites[done].original))break;
    const bool ok=done==std::size(sites);
    if(!ok)while(done){auto& site=sites[--done];void* ignored=nullptr;PatchContextSlot(site.table,site.index,*site.original,&ignored);}
    device->Release();
    Log("PF13 UI/DRAW HOOKS=%s: CreateVS + VSSetShader + four Draw entries; main-depth pixel camera scope; LCD capture disabled.",ok?"installed":"skipped");
    return ok;
}
