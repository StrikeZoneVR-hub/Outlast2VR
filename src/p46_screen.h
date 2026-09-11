#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace p46 {
inline DXGI_FORMAT Srgb(DXGI_FORMAT f) {
    switch(f){
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    default:return f;
    }
}
inline DXGI_FORMAT Linear(DXGI_FORMAT f) {
    switch(Srgb(f)){
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:return DXGI_FORMAT_B8G8R8X8_UNORM;
    default:return f;
    }
}
// The UE3 backbuffer contains display-ready bytes. OpenXR distinguishes the
// transfer function through the swapchain format, so tag those unchanged bytes
// as sRGB for the color-parity experiment. The legacy UNORM path remains an
// explicit runtime fallback for A/B testing.
inline DXGI_FORMAT Gameplay(DXGI_FORMAT f, bool srgb) { return srgb ? Srgb(f) : f; }
inline XrVector3f Rotate(XrQuaternionf q,XrVector3f v){
    const XrVector3f t{2*(q.y*v.z-q.z*v.y),2*(q.z*v.x-q.x*v.z),2*(q.x*v.y-q.y*v.x)};
    return {v.x+q.w*t.x+q.y*t.z-q.z*t.y,v.y+q.w*t.y+q.z*t.x-q.x*t.z,v.z+q.w*t.z+q.x*t.y-q.y*t.x};
}
inline XrPosef Anchor(XrPosef head,float distance){
    const auto f=Rotate(head.orientation,{0,0,-1});
    const float yaw=std::atan2(-f.x,-f.z);
    XrPosef a{{0,std::sin(yaw/2),0,std::cos(yaw/2)},head.position};
    auto d=Rotate(a.orientation,{0,0,-distance});
    a.position.x+=d.x;a.position.y+=d.y;a.position.z+=d.z;return a;
}
struct ScreenConstants {float clip[6][4]{};float uv[6][4]{};};
inline bool Geometry(const XrPosef& anchor,const XrView& view,float width,float aspect,ScreenConstants& c){
    const float l=std::tan(view.fov.angleLeft),r=std::tan(view.fov.angleRight);
    const float b=std::tan(view.fov.angleDown),t=std::tan(view.fov.angleUp);
    if(!std::isfinite(r-l)||!std::isfinite(t-b)||r-l<0.01f||t-b<0.01f||
       !std::isfinite(width)||!std::isfinite(aspect)||width<=0||aspect<=0)return false;
    constexpr float uv[6][2]={{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    const auto q=view.pose.orientation;const XrQuaternionf inverse{-q.x,-q.y,-q.z,q.w};
    for(int i=0;i<6;++i){
        auto v=Rotate(anchor.orientation,{(uv[i][0]-.5f)*width,(.5f-uv[i][1])*width/aspect,0});
        v=Rotate(inverse,{v.x+anchor.position.x-view.pose.position.x,v.y+anchor.position.y-view.pose.position.y,v.z+anchor.position.z-view.pose.position.z});
        const float w=-v.z;
        c.clip[i][0]=(2*v.x-(r+l)*w)/(r-l);
        c.clip[i][1]=(2*v.y-(t+b)*w)/(t-b);
        c.clip[i][2]=(100*w-5)/99.95f;c.clip[i][3]=w;
        c.uv[i][0]=uv[i][0];c.uv[i][1]=uv[i][1];
        for(float f:c.clip[i])if(!std::isfinite(f))return false;
    }return true;
}

// The surface is rasterized into two independently projected native XR views.
// A D3D11.1 context-state object isolates every pipeline state from Outlast's
// immediate context. Outlast creates a SINGLETHREADED device, so a deferred
// context is invalid on the real game even though it works on the WARP tests.
class ScreenRenderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext1> immediate;
    Ptr<ID3DDeviceContextState> isolated;
    Ptr<ID3D11VertexShader> vs;Ptr<ID3D11PixelShader> ps;
    Ptr<ID3D11Buffer> cb;Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11RasterizerState> raster;Ptr<ID3D11DepthStencilState> depth;
    Ptr<ID3D11Texture2D> copies[2];Ptr<ID3D11ShaderResourceView> srvs[2];
    bool Copy(ID3D11DeviceContext* ctx,ID3D11Texture2D* source,int slot){
        if(!source){copies[slot].Reset();srvs[slot].Reset();return true;}
        D3D11_TEXTURE2D_DESC s{},d{};source->GetDesc(&s);
        if(s.ArraySize!=1||s.MipLevels!=1)return false;
        if(copies[slot])copies[slot]->GetDesc(&d);
        if(!copies[slot]||s.Width!=d.Width||s.Height!=d.Height||Linear(s.Format)!=d.Format){
            copies[slot].Reset();srvs[slot].Reset();d=s;d.Format=Linear(s.Format);d.SampleDesc={1,0};
            d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.MiscFlags=0;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&copies[slot]))||FAILED(device->CreateShaderResourceView(copies[slot].Get(),nullptr,&srvs[slot])))return false;
        }
        if(s.SampleDesc.Count>1)ctx->ResolveSubresource(copies[slot].Get(),0,source,0,Linear(s.Format));
        else ctx->CopySubresourceRegion(copies[slot].Get(),0,0,0,0,source,0,nullptr);
        return true;
    }
public:
    const char* lastStage="not-started";
    HRESULT Init(ID3D11Device* d){
        if(immediate&&isolated)return S_OK;
        device=d;
        const char* code=R"(
cbuffer C : register(b0) {float4 Positions[6];float4 Texcoords[6];}
struct V {float4 p:SV_Position;float2 uv:TEXCOORD0;};
V VS(uint i:SV_VertexID){V o;o.p=Positions[i];o.uv=Texcoords[i].xy;return o;}
Texture2D Color:register(t0);Texture2D UI:register(t1);SamplerState S:register(s0);
float4 PS(V v):SV_Target{float4 c=Color.SampleLevel(S,v.uv,0);float4 u=UI.SampleLevel(S,v.uv,0);return float4(u.rgb+c.rgb*(1-u.a),1);}
)";
        Ptr<ID3DBlob> vb,pb,err;HRESULT hr;
        lastStage="compile-vs";if(FAILED(hr=D3DCompile(code,std::strlen(code),"world-screen",nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vb,&err)))return hr;
        lastStage="compile-ps";if(FAILED(hr=D3DCompile(code,std::strlen(code),"world-screen",nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&pb,&err)))return hr;
        lastStage="create-vs";if(FAILED(hr=d->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs)))return hr;
        lastStage="create-ps";if(FAILED(hr=d->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps)))return hr;
        D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(ScreenConstants);bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        lastStage="create-cb";if(FAILED(hr=d->CreateBuffer(&bd,nullptr,&cb)))return hr;
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        lastStage="create-sampler";if(FAILED(hr=d->CreateSamplerState(&sd,&sampler)))return hr;
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
        lastStage="create-raster";if(FAILED(hr=d->CreateRasterizerState(&rd,&raster)))return hr;
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.StencilEnable=FALSE;
        lastStage="create-depth-state";if(FAILED(hr=d->CreateDepthStencilState(&dd,&depth)))return hr;
        Ptr<ID3D11Device1> device1;Ptr<ID3D11DeviceContext> baseContext;
        lastStage="query-device1";if(FAILED(hr=d->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(device1.GetAddressOf()))))return hr;
        d->GetImmediateContext(&baseContext);
        lastStage="query-context1";if(!baseContext||FAILED(hr=baseContext->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(immediate.GetAddressOf()))))return FAILED(hr)?hr:E_NOINTERFACE;
        const D3D_FEATURE_LEVEL level=d->GetFeatureLevel();
        const UINT flags=(d->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        lastStage="create-context-state";
        hr=device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&isolated);
        if(SUCCEEDED(hr))lastStage="ready";return hr;
    }
    bool Render(ID3D11DeviceContext* ctx,ID3D11Texture2D* source,ID3D11Texture2D* overlay,
                ID3D11Texture2D* destination,DXGI_FORMAT targetFormat,const XrView* views,const XrPosef& anchor,float meters){
        if(!source||!destination||!immediate||!isolated)return false;
        D3D11_TEXTURE2D_DESC s{},d{};source->GetDesc(&s);destination->GetDesc(&d);
        if(d.ArraySize!=2||d.MipLevels!=1||d.SampleDesc.Count!=1||!s.Height)return false;
        ScreenConstants constants[2]{};Ptr<ID3D11RenderTargetView> targets[2];
        for(int i=0;i<2;++i){
            if(!Geometry(anchor,views[i],meters,float(s.Width)/s.Height,constants[i]))return false;
            D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=Linear(targetFormat);rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rv.Texture2DArray.FirstArraySlice=i;rv.Texture2DArray.ArraySize=1;
            if(FAILED(device->CreateRenderTargetView(destination,&rv,&targets[i])))return false;
        }
        if(!Copy(ctx,source,0)||!Copy(ctx,overlay,1))return false;
        Ptr<ID3DDeviceContextState> previous;
        immediate->SwapDeviceContextState(isolated.Get(),&previous);
        immediate->ClearState();
        immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        immediate->VSSetShader(vs.Get(),nullptr,0);immediate->PSSetShader(ps.Get(),nullptr,0);
        immediate->VSSetConstantBuffers(0,1,cb.GetAddressOf());
        ID3D11ShaderResourceView* resources[]={srvs[0].Get(),srvs[1].Get()};immediate->PSSetShaderResources(0,2,resources);
        immediate->PSSetSamplers(0,1,sampler.GetAddressOf());immediate->RSSetState(raster.Get());
        immediate->OMSetDepthStencilState(depth.Get(),0);
        const D3D11_VIEWPORT viewport{0,0,float(d.Width),float(d.Height),0,1};immediate->RSSetViewports(1,&viewport);
        const float black[4]={0,0,0,1};
        for(int i=0;i<2;++i){
            immediate->OMSetRenderTargets(1,targets[i].GetAddressOf(),nullptr);immediate->ClearRenderTargetView(targets[i].Get(),black);
            immediate->UpdateSubresource(cb.Get(),0,nullptr,&constants[i],0,0);immediate->Draw(6,0);
        }
        immediate->SwapDeviceContextState(previous.Get(),nullptr);return true;
    }
};
}
