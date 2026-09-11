namespace p31bridge {
using Microsoft::WRL::ComPtr;
p31lcd::Compositor compositor;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11Texture2D> lens,output;
ComPtr<ID3D11ShaderResourceView> lensView,outputView;
ComPtr<ID3D11RenderTargetView> outputTarget;
uint64_t epoch=0,lensTick=0,composedFrame=~0ull;
bool boundLens=false,drewLens=false;
inline void Reset(){
    compositor={};device.Reset();lens.Reset();output.Reset();lensView.Reset();outputView.Reset();outputTarget.Reset();
    boundLens=drewLens=false;lensTick=0;composedFrame=~0ull;p31native::compositeTick.store(0);
}
inline bool Active(){
    if(!p31native::runtimeEnabled.load())return false;
    const auto generation=p31native::generation.load();if(epoch!=generation){Reset();epoch=generation;}
    auto tick=p31native::captureTick.load();return tick&&GetTickCount64()-tick<250&&g_p10GameplayActive.load()&&P15Focused();
}
// This is only an identity token. Never call COM through an engine-memory
// pointer: obtain the actual owned interface from a bound RTV or SRV first.
inline void* ExpectedGpu(void* object){
    if(!p31native::Object(object)||p31native::Kind(object)!="TextureRenderTarget2D")return nullptr;
    void* resource=p31native::Pointer(object,0x110);
    if(p31native::Pointer(resource,0)!=p31native::Base()+0x1a42c80||p31native::Pointer(resource,0x90)!=object)return nullptr;
    auto* rhi=static_cast<unsigned char*>(p31native::Pointer(resource,0x98));
    if(!rhi||reinterpret_cast<uintptr_t>(rhi)<0x10000||
       p31native::Pointer(rhi-0x14,0)!=p31native::Base()+0x1b5e040)return nullptr;
    return p31native::Pointer(rhi,0xd0);
}
inline DXGI_FORMAT SampleFormat(DXGI_FORMAT format){
    if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS)return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS)return DXGI_FORMAT_B8G8R8A8_UNORM;
    return format;
}
inline void Bound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views){
    if(ctx!=g_probeGameContext||!p31native::runtimeEnabled.load())return;
    if(!Active()){Reset();return;}
    void* expected=ExpectedGpu(p31native::publishedTexture.load());bool nextBound=false;
    for(UINT i=0;views&&i<count&&i<8;++i){
        if(!views[i])continue;ComPtr<ID3D11Resource> resource;views[i]->GetResource(&resource);
        if(resource.Get()!=expected)continue;
        ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))continue;
        nextBound=true;
        if(texture.Get()!=lens.Get()){
            Reset();lens=texture;ctx->GetDevice(&device);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            if(desc.ArraySize!=1||desc.MipLevels!=1||desc.SampleDesc.Count!=1){Reset();return;}
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=SampleFormat(desc.Format);srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
            if(FAILED(device->CreateShaderResourceView(lens.Get(),&srv,&lensView))){Reset();return;}
            static HMODULE module=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto compile=module?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(module,"D3DCompile")):nullptr;
            if(!compile||FAILED(compositor.Initialize(device.Get(),compile))){Reset();return;}
            Log("P31C lens GPU target acquired: %ux%u format=%u",desc.Width,desc.Height,unsigned(desc.Format));
        }
    }
    if(boundLens&&!nextBound&&drewLens){lensTick=GetTickCount64();drewLens=false;composedFrame=~0ull;}
    boundLens=nextBound;
}
inline bool Compose(ID3D11DeviceContext* ctx,ID3D11ShaderResourceView* hud){
    D3D11_SHADER_RESOURCE_VIEW_DESC hd{};hud->GetDesc(&hd);
    ComPtr<ID3D11Resource> resource;hud->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture)))return false;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    if(!output){
        desc.Format=hd.Format;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&output))||FAILED(device->CreateRenderTargetView(output.Get(),nullptr,&outputTarget))||
           FAILED(device->CreateShaderResourceView(output.Get(),nullptr,&outputView))){output.Reset();outputTarget.Reset();outputView.Reset();return false;}
    }else{
        D3D11_TEXTURE2D_DESC old{};output->GetDesc(&old);
        if(old.Width!=desc.Width||old.Height!=desc.Height||old.Format!=hd.Format){output.Reset();outputTarget.Reset();outputView.Reset();return Compose(ctx,hud);}
    }
    if(composedFrame!=g_probeFrameCounter.load()){
        // Explicit trial convention. No inference from the final headset image.
        static const bool straight=GetPrivateProfileIntW(L"VR",L"LensHudStraightAlpha",0,(ModuleDir()+L"\\outlast2_vr_p32.ini").c_str())!=0;
        if(FAILED(compositor.Render(ctx,lensView.Get(),hud,outputTarget.Get(),straight?p31lcd::HudAlpha::Straight:p31lcd::HudAlpha::Premultiplied)))return false;
        composedFrame=g_probeFrameCounter.load();
    }
    if(!p31native::compositeTick.load())Log("P31C LCD composite ready: independent lens + native HUD.");
    p31native::compositeTick.store(GetTickCount64());return true;
}
    DrawScope::DrawScope(ID3D11DeviceContext* c):ctx(c){
        if(c!=g_probeGameContext||!Active())return;
        if(boundLens){drewLens=true;return;}
        if(!lensView||!lensTick||GetTickCount64()-lensTick>500)return;
        ComPtr<ID3D11DepthStencilView> depth;c->OMGetRenderTargets(0,nullptr,&depth);
        if(!depth||P11AClassifyDepthStencilView(depth.Get())!=P11ADepthPassKind::MainScene)return;
        void* expected=ExpectedGpu(p31native::publishedHud.load());if(!expected)return;
        ID3D11ShaderResourceView* raw[16]{};c->PSGetShaderResources(0,16,raw);
        for(UINT i=0;i<16;++i){
            ComPtr<ID3D11ShaderResourceView> view;view.Attach(raw[i]);if(!view)continue;
            ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
            if(!original&&resource.Get()==expected&&Compose(c,view.Get())){slot=i;original=view;}
        }
        if(original){auto* value=outputView.Get();c->PSSetShaderResources(slot,1,&value);}
    }
    DrawScope::~DrawScope(){if(original){auto* value=original.Get();ctx->PSSetShaderResources(slot,1,&value);}}
}
void P31TargetBound(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* views){p31bridge::Bound(ctx,count,views);}




