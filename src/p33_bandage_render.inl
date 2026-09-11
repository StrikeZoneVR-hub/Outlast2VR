// Members of OpenXRQuad. P33's bandage renderer draws into the current game
// backbuffer immediately before that eye is copied. A deferred context and
// ExecuteCommandList(..., TRUE) preserve UE3's immediate-context state.
struct P33GpuVertex {float x,y,z,w,u,v,shade;};
Microsoft::WRL::ComPtr<ID3D11DeviceContext> p33Deferred;
Microsoft::WRL::ComPtr<ID3D11Buffer> p33VB;
Microsoft::WRL::ComPtr<ID3D11VertexShader> p33VS;
Microsoft::WRL::ComPtr<ID3D11PixelShader> p33PS;
Microsoft::WRL::ComPtr<ID3D11InputLayout> p33Layout;
Microsoft::WRL::ComPtr<ID3D11SamplerState> p33Sampler;
Microsoft::WRL::ComPtr<ID3D11BlendState> p33Blend;
Microsoft::WRL::ComPtr<ID3D11DepthStencilState> p33Depth;
Microsoft::WRL::ComPtr<ID3D11RasterizerState> p33Raster;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> p33Texture;
std::vector<p33::PskVertex> p33Mesh;
bool p33RendererTried=false,p33RendererReady=false,p33AssetLogged=false;
size_t p33VbCapacity=0;

std::wstring P33Ini(){return ModuleDir()+L"\\outlast2_vr_p33.ini";}
std::wstring P33AssetPath(const wchar_t* key){
    wchar_t value[1024]{};GetPrivateProfileStringW(L"VR",key,L"",value,1024,P33Ini().c_str());
    if(!value[0])return {};
    std::wstring path=value;
    if(path.size()>2&&path[1]==L':')return path;
    return ModuleDir()+L"\\"+path;
}
bool P33ReadFile(const std::wstring& path,std::vector<uint8_t>& out){
    FILE* f=nullptr;if(_wfopen_s(&f,path.c_str(),L"rb")||!f)return false;
    _fseeki64(f,0,SEEK_END);const auto n=_ftelli64(f);_fseeki64(f,0,SEEK_SET);
    if(n<=0||n>128ll*1024*1024){fclose(f);return false;}
    out.resize(size_t(n));const bool ok=fread(out.data(),1,out.size(),f)==out.size();fclose(f);return ok;
}
bool P33CreateFallbackTexture(){
    const uint32_t pixels[16]={
        0xffd8e5ee,0xffc7d7e3,0xffd8e5ee,0xffc7d7e3,
        0xffc7d7e3,0xffe8eff4,0xffc7d7e3,0xffe8eff4,
        0xffd8e5ee,0xffc7d7e3,0xffd8e5ee,0xffc7d7e3,
        0xffc7d7e3,0xffe8eff4,0xffc7d7e3,0xffe8eff4};
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels,16,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    return SUCCEEDED(device->CreateTexture2D(&d,&data,&tex))&&SUCCEEDED(device->CreateShaderResourceView(tex.Get(),nullptr,&p33Texture));
}
bool P33LoadTextureWic(const std::wstring& path){
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);(void)init;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr))return false;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)))return false;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return false;
    UINT w=0,h=0;if(FAILED(frame->GetSize(&w,&h))||!w||!h||w>8192||h>8192)return false;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter)))return false;
    if(FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
    std::vector<uint8_t> rgba(size_t(w)*h*4);
    if(FAILED(converter->CopyPixels(nullptr,w*4,UINT(rgba.size()),rgba.data())))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{rgba.data(),w*4,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    return SUCCEEDED(device->CreateTexture2D(&d,&data,&tex))&&SUCCEEDED(device->CreateShaderResourceView(tex.Get(),nullptr,&p33Texture));
}
bool P33EnsureRenderer(){
    if(p33RendererTried)return p33RendererReady;p33RendererTried=true;
    if(!device||!context)return false;
    std::vector<uint8_t> bytes;const auto meshPath=P33AssetPath(L"BandageMesh");
    if(!meshPath.empty()&&P33ReadFile(meshPath,bytes)&&p33::ParsePsk(bytes,p33Mesh)){
        Log("P33 BANDAGE ASSET: loaded UModel PSK/PSKX mesh with %zu triangle vertices.",p33Mesh.size());
    }else{
        p33::MakeFallbackRoll(p33Mesh);
        Log("P33 BANDAGE ASSET: exported mesh unavailable/unreadable; using procedural roll geometry with the exported bandage texture.");
    }
    const auto texturePath=P33AssetPath(L"BandageTexture");
    if(texturePath.empty()||!P33LoadTextureWic(texturePath)){
        if(!P33CreateFallbackTexture())return false;
        Log("P33 BANDAGE TEXTURE: UModel texture unavailable; using neutral cloth fallback.");
    }else Log("P33 BANDAGE TEXTURE: loaded UModel-exported diffuse texture.");
    if(FAILED(device->CreateDeferredContext(0,&p33Deferred)))return false;
    static const char* shader=R"(
Texture2D tex0:register(t0);SamplerState samp0:register(s0);
struct I{float4 p:POSITION;float2 uv:TEXCOORD0;float shade:TEXCOORD1;};
struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD0;float shade:TEXCOORD1;};
O VS(I i){O o;o.p=i.p;o.uv=i.uv;o.shade=i.shade;return o;}
float4 PS(O i):SV_Target{float4 c=tex0.Sample(samp0,i.uv);clip(c.a-0.03);c.rgb*=lerp(.70,1.08,saturate(i.shade));return c;}
)";
    Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,err;
    if(FAILED(D3DCompile(shader,strlen(shader),"P33Bandage",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&err)))return false;
    if(FAILED(D3DCompile(shader,strlen(shader),"P33Bandage",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&err)))return false;
    if(FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&p33VS))||
       FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p33PS)))return false;
    D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(device->CreateInputLayout(elements,3,vs->GetBufferPointer(),vs->GetBufferSize(),&p33Layout)))return false;
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sd,&p33Sampler)))return false;
    D3D11_BLEND_DESC bd{};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&bd,&p33Blend)))return false;
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.StencilEnable=FALSE;if(FAILED(device->CreateDepthStencilState(&dd,&p33Depth)))return false;
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;if(FAILED(device->CreateRasterizerState(&rd,&p33Raster)))return false;
    p33RendererReady=true;return true;
}
bool P33Clip(p20::V relative,p20::V normal,const P20Tracked& tracked,const XrView& eye,float u,float v,P33GpuVertex& out){
    const p20::Q cq{tracked.center.orientation.x,tracked.center.orientation.y,tracked.center.orientation.z,tracked.center.orientation.w};
    const p20::V cp{tracked.center.position.x,tracked.center.position.y,tracked.center.position.z};
    const p20::Q eq{eye.pose.orientation.x,eye.pose.orientation.y,eye.pose.orientation.z,eye.pose.orientation.w};
    const p20::V ep{eye.pose.position.x,eye.pose.position.y,eye.pose.position.z};
    const auto absolute=cp+p20::Rotate(cq,relative);const auto view=p20::Rotate(p20::Conjugate(eq),absolute-ep);
    const float depth=-view.z;if(depth<.025f)return false;
    const float l=std::tan(eye.fov.angleLeft),r=std::tan(eye.fov.angleRight),d=std::tan(eye.fov.angleDown),up=std::tan(eye.fov.angleUp);
    if(r-l<.01f||up-d<.01f)return false;
    const float nx=(2.0f*(view.x/depth)-(r+l))/(r-l),ny=(2.0f*(view.y/depth)-(up+d))/(up-d);
    const auto vn=p20::Rotate(p20::Conjugate(eq),p20::Rotate(cq,normal));const float shade=.55f+.45f*std::fabs(vn.z);
    out={nx,ny,.5f,1,u,v,shade};return std::isfinite(nx)&&std::isfinite(ny);
}
void P33AppendMesh(std::vector<P33GpuVertex>& gpu,const P20Tracked& tracked,p20::V center,p20::Q rotation,float scale=1.0f){
    if(!p12Pending.valid)return;const auto& eye=p12Pending.view;
    for(size_t i=0;i+2<p33Mesh.size();i+=3){
        P33GpuVertex tri[3]{};bool ok=true;
        for(int k=0;k<3;++k){
            const auto& s=p33Mesh[i+k];
            const auto p=center+p20::Rotate(rotation,s.p*scale);const auto n=p20::Rotate(rotation,s.n);
            if(!P33Clip(p,n,tracked,eye,s.u,s.v,tri[k])){ok=false;break;}
        }
        if(ok){gpu.push_back(tri[0]);gpu.push_back(tri[1]);gpu.push_back(tri[2]);}
    }
}
void P33AppendRibbon(std::vector<P33GpuVertex>& gpu,const P20Tracked& tracked,float progress){
    if(progress<=.005f||!p12Pending.valid)return;
    const auto elbow=p33::EstimatedElbow(tracked.position[0],tracked.position[1]);
    const auto wrist=tracked.position[1];const auto axis=p20::Unit(elbow-wrist);const float length=std::min(.19f,p20::Length(elbow-wrist)*.78f);
    auto u=p20::Cross(axis,std::fabs(axis.y)<.8f?p20::V{0,1,0}:p20::V{1,0,0});u=p20::Unit(u);const auto v=p20::Cross(axis,u);
    const float totalTurns=std::max(.5f,p33runtime::wrapTurns)*progress;const int segments=std::max(2,int(std::ceil(totalTurns*30)));
    const float bandWidth=.032f,radius=.047f;
    for(int i=0;i<segments;++i){
        const float t0=float(i)/segments,t1=float(i+1)/segments;
        const float a0=2*p33::Pi*totalTurns*t0,a1=2*p33::Pi*totalTurns*t1;
        const auto c0=wrist+axis*(length*t0),c1=wrist+axis*(length*t1);
        const auto r0=u*std::cos(a0)*radius+v*std::sin(a0)*radius;
        const auto r1=u*std::cos(a1)*radius+v*std::sin(a1)*radius;
        const p20::V a=c0+r0-axis*(bandWidth*.5f),b=c0+r0+axis*(bandWidth*.5f);
        const p20::V c=c1+r1+axis*(bandWidth*.5f),d0=c1+r1-axis*(bandWidth*.5f);
        const auto normal=p20::Unit((r0+r1)*.5f,{0,0,1});P33GpuVertex q[4]{};
        if(P33Clip(a,normal,tracked,p12Pending.view,t0,1,q[0])&&P33Clip(b,normal,tracked,p12Pending.view,t0,0,q[1])&&
           P33Clip(c,normal,tracked,p12Pending.view,t1,0,q[2])&&P33Clip(d0,normal,tracked,p12Pending.view,t1,1,q[3])){
            gpu.push_back(q[0]);gpu.push_back(q[1]);gpu.push_back(q[2]);gpu.push_back(q[0]);gpu.push_back(q[2]);gpu.push_back(q[3]);
        }
    }
}
void P33RenderBandages(ID3D11Texture2D* backbuffer){
    if(!backbuffer||!p12Pending.valid||!p33runtime::enabled||!P33EnsureRenderer())return;
    const int count=p33runtime::Count();if(count<=0)return;
    P20Tracked tracked;{std::lock_guard<std::mutex> lock(g_p20PoseMutex);tracked=g_p20Tracked;}
    const auto now=GetTickCount64();if(!tracked.tick||now<tracked.tick||now-tracked.tick>250)return;
    std::vector<P33GpuVertex> gpu;gpu.reserve(p33Mesh.size()*4+512);
    const bool equipped=p33runtime::Equipped();const int visible=p33::HolsterVisibleCount(count,equipped);
    const auto hip=p33runtime::HipPosition(tracked);
    for(int i=0;i<visible;++i)P33AppendMesh(gpu,tracked,hip+p20::V{0.0f,float(i)*.064f,0.0f},p20::Q{},.88f);
    if(equipped&&tracked.handValid[1]){
        const auto q=tracked.rotation[2];const auto hand=tracked.position[2]+p20::Rotate(q,p20::V{0,-.015f,-.055f});
        P33AppendMesh(gpu,tracked,hand,q,.92f);P33AppendRibbon(gpu,tracked,p33runtime::Progress());
    }
    if(gpu.empty())return;
    if(gpu.size()>p33VbCapacity){
        p33VB.Reset();p33VbCapacity=1;while(p33VbCapacity<gpu.size())p33VbCapacity*=2;
        D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(p33VbCapacity*sizeof(P33GpuVertex));d.Usage=D3D11_USAGE_DYNAMIC;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateBuffer(&d,nullptr,&p33VB))){p33VbCapacity=0;return;}
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(p33Deferred->Map(p33VB.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return;
    std::memcpy(mapped.pData,gpu.data(),gpu.size()*sizeof(P33GpuVertex));p33Deferred->Unmap(p33VB.Get(),0);
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;if(FAILED(device->CreateRenderTargetView(backbuffer,nullptr,&rtv)))return;
    D3D11_TEXTURE2D_DESC td{};backbuffer->GetDesc(&td);D3D11_VIEWPORT vp{0,0,float(td.Width),float(td.Height),0,1};
    auto* target=rtv.Get();p33Deferred->OMSetRenderTargets(1,&target,nullptr);p33Deferred->RSSetViewports(1,&vp);p33Deferred->RSSetState(p33Raster.Get());
    const UINT stride=sizeof(P33GpuVertex),offset=0;auto* vb=p33VB.Get();p33Deferred->IASetInputLayout(p33Layout.Get());p33Deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p33Deferred->IASetVertexBuffers(0,1,&vb,&stride,&offset);p33Deferred->VSSetShader(p33VS.Get(),nullptr,0);p33Deferred->PSSetShader(p33PS.Get(),nullptr,0);
    auto* srv=p33Texture.Get();auto* samp=p33Sampler.Get();p33Deferred->PSSetShaderResources(0,1,&srv);p33Deferred->PSSetSamplers(0,1,&samp);
    const float blendFactor[4]{};p33Deferred->OMSetBlendState(p33Blend.Get(),blendFactor,0xffffffff);p33Deferred->OMSetDepthStencilState(p33Depth.Get(),0);
    p33Deferred->Draw(UINT(gpu.size()),0);
    Microsoft::WRL::ComPtr<ID3D11CommandList> list;if(SUCCEEDED(p33Deferred->FinishCommandList(FALSE,&list)))context->ExecuteCommandList(list.Get(),TRUE);
    if(!p33AssetLogged){p33AssetLogged=true;Log("P33 BANDAGE RENDER READY: holster stack + equipped roll + progressive forearm wrap are rendered into each matched eye before capture.");}
}
