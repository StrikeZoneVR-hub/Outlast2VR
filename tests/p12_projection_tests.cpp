#include "p12_projection.h"
#include "p15_pixel_camera.h"
#include "p15_movement.h"
#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <stdexcept>
using Microsoft::WRL::ComPtr;

static void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
static void Hr(HRESULT hr,const char* why) { Check(SUCCEEDED(hr),why); }
static void Near(float a,float b,const char* why) {
    if(!std::isfinite(a)||std::fabs(a-b)>0.00008f*std::max(1.0f,std::fabs(b))) {
        std::fprintf(stderr,"%s: %.8f vs %.8f\n",why,a,b); throw std::runtime_error(why);
    }
}
static std::array<float,4> Mul(const float* m,const std::array<float,4>& v) {
    std::array<float,4> out{};
    for(int j=0;j<4;++j) for(int i=0;i<4;++i) out[j]+=v[i]*m[i*4+j];
    return out;
}
static p12::HeadConstants Identity() {
    p12::HeadConstants h{};
    h.right[0]=h.up[1]=h.forward[2]=1;
    h.params[0]=0.55f; h.params[1]=h.params[2]=1;
    Check(p12::Projection(-0.9f,0.7f,-0.75f,0.85f,h.projection),"valid FOV");
    return h;
}
static std::array<float,40> Source(float yaw,float rs,float us,float depth,float bias) {
    std::array<float,40> m{};
    for(int base:{0,16}) {
        m[base]=std::cos(yaw)*rs; m[base+8]=-std::sin(yaw)*rs;
        m[base+5]=us;
        m[base+3]=std::sin(yaw); m[base+11]=std::cos(yaw);
        m[base+2]=m[base+3]*depth; m[base+10]=m[base+11]*depth;
        m[base+12]=3*rs; m[base+13]=-2*us; m[base+15]=5;
        m[base+14]=bias+depth*5;
    }
    for(int i=32;i<40;++i) m[i]=123.0f+i;
    return m;
}

int main() try {
    for(int i=-180;i<=180;++i){float f=0.8f,r=0.3f;Check(p15::RotateInput(f,r,i*0.0174532925f),"finite movement");Near(f*f+r*r,0.73f,"movement magnitude preserved");}
    {float f=1,r=0;p15::RotateInput(f,r,1.5707963268f);Near(f,0,"90 degree walk forward");Near(r,1,"90 degree walk right");}
    {float f=0,r=0;p15::RotateInput(f,r,2);Check(f==0&&r==0,"no auto walk");}
    {
        const auto down=p15::RotatorUnits(-0.75f);
        Check(down<0,"negative HMD pitch converts to negative UE3 rotator units");
        const int32_t native=123456789;
        const auto patched=p15::AddRotatorUnits(native,down);
        Check(p15::RemoveRotatorUnits(patched,down)==native,
            "temporary native interaction pitch restores exactly");
        Check(p15::RotatorUnits(std::numeric_limits<float>::quiet_NaN())==0,
            "invalid HMD pitch is rejected");
    }
    auto h=Identity();
    float proj[4]{};
    Check(!p12::Projection(0.5f,-0.5f,-0.5f,0.5f,proj),"reject inverted FOV");
    Check(!p12::Projection(-1.6f,0.5f,-0.5f,0.5f,proj),"reject singular FOV");
    Check(!p12::Projection(std::numeric_limits<float>::quiet_NaN(),0.5f,-0.5f,0.5f,proj),"reject NaN FOV");
    Check(!p12::Projection(-0.5f,0.5f,0.5f,-0.5f,proj),"reject inverted vertical FOV");
    // Rays at all four asymmetric FOV boundaries must map exactly to NDC edges.
    for(int edge=0;edge<4;++edge) {
        auto m=Source(0,1.1f,1.9555556f,1.002f,-2.004f);
        p12::PatchMatrix(m.data(),h);
        std::array<float,4> v={-3,2,5,1}; // translated eye-space forward distance is 10
        if(edge<2) v[0]+=10*std::tan(edge==0?-0.9f:0.7f);
        else v[1]+=10*std::tan(edge==2?-0.75f:0.85f);
        auto p=Mul(m.data(),v);
        Near(p[edge<2?0:1]/p[3],edge%2==0?-1.0f:1.0f,"asymmetric FOV edge");
    }
    // Parallel left/right eyes: positive disparity for near objects; no toe-in.
    auto lm=Source(0,1,1.7777778f,0,2), rm=lm;
    auto left=h,right=h; left.eyeOffset[0]=-3.2f; right.eyeOffset[0]=3.2f;
    p12::PatchMatrix(lm.data(),left); p12::PatchMatrix(rm.data(),right);
    const auto l=Mul(lm.data(),{-3,2,95,1}),r=Mul(rm.data(),{-3,2,95,1});
    Near(l[0]/l[3]-r[0]/r[3],6.4f*h.projection[0]/100,"IPD disparity sign/scale");
    auto shortBuffer=Source(0,1,1.7777778f,0,2), untouched=shortBuffer;
    p12::PatchCameraBytes(shortBuffer.data(),127,h);
    Check(shortBuffer==untouched,"short buffer unchanged");

    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
    Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx),"create WARP");
    ComPtr<ID3DBlob> code,errors;
    HRESULT result=D3DCompile(p12::kPatchShader,std::strlen(p12::kPatchShader),"P12 test",nullptr,nullptr,
        "CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
    if(FAILED(result)&&errors) std::fprintf(stderr,"%s\n",static_cast<char*>(errors->GetBufferPointer()));
    Hr(result,"compile production compute shader");
    ComPtr<ID3D11ComputeShader> shader;
    Hr(dev->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader),"create shader");
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=160; bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    ComPtr<ID3D11Buffer> data,readback,cb;
    Hr(dev->CreateBuffer(&bd,nullptr,&data),"create raw buffer");
    bd.Usage=D3D11_USAGE_STAGING; bd.BindFlags=0; bd.MiscFlags=0; bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Hr(dev->CreateBuffer(&bd,nullptr,&readback),"create staging");
    bd.ByteWidth=sizeof(h); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags=0;
    Hr(dev->CreateBuffer(&bd,nullptr,&cb),"create head constants");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format=DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements=40; ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    ComPtr<ID3D11UnorderedAccessView> uav;
    Hr(dev->CreateUnorderedAccessView(data.Get(),&ud,&uav),"create UAV");
    std::mt19937 rng(12); std::uniform_real_distribution<float> random(-1.0f,1.0f);
    for(int test=0;test<240;++test) {
        const float baseYaw=random(rng)*3.0f, headYaw=random(rng)*3.0f;
        const float rs=1.2f+random(rng)*0.2f, us=rs*1.7777778f;
        const float depth=test%2?1.002f:0.0f, bias=test%2?-2.004f:2.0f;
        auto input=Source(baseYaw,rs,us,depth,bias),cpu=input;
        h=Identity();
        // Euler Y/X/Z rotation: independent orthonormal eye basis including pitch and roll.
        const float pitch=random(rng),roll=random(rng);
        const float cy=std::cos(headYaw),sy=std::sin(headYaw),cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
        h.right[0]=cy*cr+sy*sp*sr; h.right[1]=cp*sr; h.right[2]=-sy*cr+cy*sp*sr;
        h.up[0]=-cy*sr+sy*sp*cr; h.up[1]=cp*cr; h.up[2]=sy*sr+cy*sp*cr;
        h.forward[0]=sy*cp; h.forward[1]=-sp; h.forward[2]=cy*cp;
        for(int i=0;i<3;++i) h.eyeOffset[i]=random(rng)*30;
        if(test%5==0) h.params[2]=0; // baseline compatibility path
        if(test%7==0) h.eyeOffset[3]=rs/1.8f; // headset must retain native camcorder magnification
        h.params[3]=test%3==0?1.0f:0.0f;
        p12::PatchCameraBytes(cpu.data(),sizeof(cpu),h);
        for(int i=(h.params[2]&&h.params[3]?35:32);i<40;++i) Check(cpu[i]==input[i],"camera W and pre-view translation preserved");
        if(h.params[2]&&h.params[3]){
            float localOffset[3]{};
            for(int i=0;i<3;++i)localOffset[i]=h.right[i]*h.eyeOffset[0]+h.up[i]*h.eyeOffset[1]+h.forward[i]*h.eyeOffset[2];
            Near(cpu[32]-input[32],std::cos(baseYaw)*localOffset[0]+std::sin(baseYaw)*localOffset[2],"camera position X coherence");
            Near(cpu[33]-input[33],localOffset[1],"camera position Y coherence");
            Near(cpu[34]-input[34],-std::sin(baseYaw)*localOffset[0]+std::cos(baseYaw)*localOffset[2],"camera position Z coherence");
        }
        const std::array<float,4> world={random(rng)*100,random(rng)*100,random(rng)*100,1};
        const auto original=Mul(input.data(),world);
        const float local[3]={original[0]/rs,original[1]/us,original[3]};
        float eye[3]{};
        for(int i=0;i<3;++i) { eye[0]+=local[i]*h.right[i]; eye[1]+=local[i]*h.up[i]; eye[2]+=local[i]*h.forward[i]; }
        if(h.params[2]>0.5f) for(int i=0;i<3;++i) eye[i]-=h.eyeOffset[i];
        const auto actual=Mul(cpu.data(),world);
        const float zoom=h.params[2]?p12::RelativeZoom(rs,h.eyeOffset[3]):1.0f;
        Near(actual[0],eye[0]*(h.params[2]?h.projection[0]*zoom:rs*0.55f)+(h.params[2]?eye[2]*h.projection[2]:0),"rigid transform X");
        Near(actual[1],eye[1]*(h.params[2]?h.projection[1]*zoom:us*0.55f)+(h.params[2]?eye[2]*h.projection[3]:0),"rigid transform Y");
        Near(actual[2],eye[2]*depth+bias,"preserve engine depth mapping"); Near(actual[3],eye[2],"rigid transform W");
        ctx->UpdateSubresource(data.Get(),0,nullptr,input.data(),0,0);
        ctx->UpdateSubresource(cb.Get(),0,nullptr,&h,0,0);
        ctx->CSSetShader(shader.Get(),nullptr,0);
        ID3D11Buffer* cbs[]={cb.Get()}; ctx->CSSetConstantBuffers(0,1,cbs);
        ID3D11UnorderedAccessView* uavs[]={uav.Get()}; ctx->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
        ctx->Dispatch(1,1,1);
        uavs[0]=nullptr; ctx->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
        ctx->CopyResource(readback.Get(),data.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Hr(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"GPU readback");
        const float* gpu=static_cast<float*>(mapped.pData);
        for(int i=0;i<40;++i) Near(gpu[i],cpu[i],"production GPU/CPU agreement");
        ctx->Unmap(readback.Get(),0);
    }
    // Pitch lock tests use the game's Z-up world, keeping the original camera pivot.
    for(int test=0;test<50;++test){
        auto input=Source(0,1.2f,2.133333f,0,2.0f);const float angle=-1.2f+test*0.048f;
        const float cp=std::cos(angle),sp=std::sin(angle);
        for(int b:{0,16}){float* m=input.data()+b;std::fill(m,m+16,0.0f);
            m[0]=1.2f;m[5]=-sp*2.133333f;m[9]=cp*2.133333f;m[7]=cp;m[11]=sp;
            m[12]=-3*1.2f;m[13]=-(-4*sp+5*cp)*2.133333f;m[15]=-(4*cp+5*sp);m[14]=2;
        }
        h=Identity();h.right[3]=1;h.params[3]=1;h.eyeOffset[0]=2;h.eyeOffset[1]=3;h.eyeOffset[2]=4;
        auto cpu=input;p12::PatchCameraBytes(cpu.data(),160,h);
        auto point=Mul(cpu.data(),{5,18,8,1});Near(point[3],10,"level world retains pivot and XYZ offset");
        ctx->UpdateSubresource(data.Get(),0,nullptr,input.data(),0,0);ctx->UpdateSubresource(cb.Get(),0,nullptr,&h,0,0);
        ID3D11UnorderedAccessView* view=uav.Get();ctx->CSSetUnorderedAccessViews(0,1,&view,nullptr);ctx->Dispatch(1,1,1);
        view=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&view,nullptr);ctx->CopyResource(readback.Get(),data.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Hr(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"level readback");
        for(int i=0;i<40;++i)Near(static_cast<float*>(mapped.pData)[i],cpu[i],"level CPU/GPU agreement");ctx->Unmap(readback.Get(),0);
        std::vector<uint8_t> ps(224);std::memcpy(ps.data()+144,input.data(),64);std::memcpy(ps.data()+80,input.data()+32,12);
        Check(p15::PatchPixelCamera(ps,input.data(),h),"matched PS camera patched");
        Check(!std::memcmp(ps.data()+144,cpu.data(),64)&&!std::memcmp(ps.data()+80,cpu.data()+32,12),"PS/VS camera coherence");
        std::vector<uint8_t> inverse(64);float inv[16];Check(p15::Inverse(input.data(),inv),"invert original");std::memcpy(inverse.data(),inv,64);
        Check(p15::PatchPixelCamera(inverse,input.data(),h),"matched world reconstruction patched");
        float invNext[16];Check(p15::Inverse(cpu.data(),invNext),"invert next");
        float actual[16];std::memcpy(actual,inverse.data(),64);Check(p15::SameMatrix(actual,invNext),"world reconstruction matches rendered eye");
        std::vector<uint8_t> unknown(224,0);Check(!p15::PatchPixelCamera(unknown,input.data(),h),"unknown pixel constants untouched");
    }
    std::puts("PASS: asymmetric frustum edges, IPD sign/scale, invalid FOV, short buffers, 240 rigid/depth transforms, 9600 production GPU/CPU values, untouched camera tail.");
    return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }

