#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdint>
#include <utility>

namespace p31lcd {
using Microsoft::WRL::ComPtr;
enum class HudAlpha { Premultiplied, Straight };
inline constexpr char Shader[]=R"(
Texture2D lens : register(t0);
Texture2D hud : register(t1);
SamplerState linearClamp : register(s0);
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex VS(uint id:SV_VertexID) {
    Vertex v; v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 Premultiplied(Vertex v):SV_Target {
    float4 overlay=hud.SampleLevel(linearClamp,v.uv,0);
    float3 scene=lens.SampleLevel(linearClamp,v.uv,0).rgb;
    return float4(overlay.rgb+scene*(1-saturate(overlay.a)),1);
}
float4 Straight(Vertex v):SV_Target {
    float4 overlay=hud.SampleLevel(linearClamp,v.uv,0);
    float3 scene=lens.SampleLevel(linearClamp,v.uv,0).rgb;
    return float4(lerp(scene,overlay.rgb,saturate(overlay.a)),1);
}
)";
// Inputs must be resolved, full-frame, same-aspect lens/HUD images. There is
// deliberately no headset camera matrix, crop, zoom, NV tint, or recursive input.
class Compositor {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deferred;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> premultiplied,straight;
    ComPtr<ID3D11SamplerState> sampler;
    using Compiler=decltype(&D3DCompile);
    static HRESULT Compile(Compiler compiler,const char* entry,const char* target,ComPtr<ID3DBlob>& blob){
        return compiler(Shader,std::strlen(Shader),"LCD compositor",nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,nullptr);
    }
    bool SameDevice(ID3D11DeviceChild* child)const{
        if(!child)return false;ComPtr<ID3D11Device> other;child->GetDevice(&other);return other.Get()==device.Get();
    }
    static bool Texture(ID3D11View* view,ComPtr<ID3D11Resource>& resource,D3D11_TEXTURE2D_DESC& desc){
        if(!view)return false;view->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture)))return false;texture->GetDesc(&desc);
        return desc.Width&&desc.Height&&desc.ArraySize==1&&desc.MipLevels==1&&desc.SampleDesc.Count==1;
    }
public:
    HRESULT Initialize(ID3D11Device* input,Compiler compiler){
        *this=Compositor{};if(!input||!compiler)return E_INVALIDARG;
        Compositor next;next.device=input;ComPtr<ID3DBlob> code;
        HRESULT hr=input->CreateDeferredContext(0,&next.deferred);
        if(SUCCEEDED(hr))hr=Compile(compiler,"VS","vs_5_0",code);
        if(SUCCEEDED(hr))hr=input->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&next.vs);
        code.Reset();if(SUCCEEDED(hr))hr=Compile(compiler,"Premultiplied","ps_5_0",code);
        if(SUCCEEDED(hr))hr=input->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&next.premultiplied);
        code.Reset();if(SUCCEEDED(hr))hr=Compile(compiler,"Straight","ps_5_0",code);
        if(SUCCEEDED(hr))hr=input->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&next.straight);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.MaxAnisotropy=1;
        if(SUCCEEDED(hr))hr=input->CreateSamplerState(&sd,&next.sampler);
        if(SUCCEEDED(hr))*this=std::move(next);return hr;
    }
    HRESULT Render(ID3D11DeviceContext* immediate,ID3D11ShaderResourceView* lens,ID3D11ShaderResourceView* hud,
                   ID3D11RenderTargetView* output,HudAlpha alpha){
        if(!deferred||!SameDevice(immediate)||!SameDevice(lens)||!SameDevice(hud)||!SameDevice(output)||
           immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_INVALIDARG;
        if(alpha!=HudAlpha::Premultiplied&&alpha!=HudAlpha::Straight)return E_INVALIDARG;
        ComPtr<ID3D11Resource> lr,hr,orr;D3D11_TEXTURE2D_DESC ld{},hd{},od{};
        if(!Texture(lens,lr,ld)||!Texture(hud,hr,hd)||!Texture(output,orr,od)||
           lr.Get()==orr.Get()||hr.Get()==orr.Get()||lr.Get()==hr.Get())return E_INVALIDARG;
        if(uint64_t(ld.Width)*od.Height!=uint64_t(od.Width)*ld.Height||
           uint64_t(hd.Width)*od.Height!=uint64_t(od.Width)*hd.Height)return E_INVALIDARG;
        D3D11_SHADER_RESOURCE_VIEW_DESC ls{},hs{};D3D11_RENDER_TARGET_VIEW_DESC os{};
        lens->GetDesc(&ls);hud->GetDesc(&hs);output->GetDesc(&os);
        if(ls.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||hs.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||
           os.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D)return E_INVALIDARG;
        // A separate command list prevents interference with the game's render
        // state and draw-hook caches. TRUE restores the entire immediate state.
        deferred->ClearState();
        D3D11_VIEWPORT vp{0,0,float(od.Width),float(od.Height),0,1};deferred->RSSetViewports(1,&vp);
        deferred->OMSetRenderTargets(1,&output,nullptr);
        deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        deferred->VSSetShader(vs.Get(),nullptr,0);
        deferred->PSSetShader(alpha==HudAlpha::Premultiplied?premultiplied.Get():straight.Get(),nullptr,0);
        ID3D11ShaderResourceView* inputs[]={lens,hud};deferred->PSSetShaderResources(0,2,inputs);
        ID3D11SamplerState* sampling=sampler.Get();deferred->PSSetSamplers(0,1,&sampling);
        deferred->Draw(3,0);
        ComPtr<ID3D11CommandList> commands;HRESULT result=deferred->FinishCommandList(FALSE,&commands);
        if(FAILED(result))return result;
        immediate->ExecuteCommandList(commands.Get(),TRUE);return device->GetDeviceRemovedReason();
    }
};
}
