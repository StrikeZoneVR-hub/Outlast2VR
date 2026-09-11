#include "../src/dinput8_proxy.cpp"
#include "../src/p31_native_capture.inl"
#include <stdexcept>
#include "../src/p31_lcd_compositor.h"
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void TestCompositor(){
    using Microsoft::WRL::ComPtr;using Pixel=std::array<float,4>;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)),"WARP device");
    p31lcd::Compositor compositor;Check(SUCCEEDED(compositor.Initialize(dev.Get(),&D3DCompile)),"compositor init");
    std::array<Pixel,32> scene{},hud{};
    for(int y=0;y<4;++y)for(int x=0;x<8;++x){float a=x<2?0.f:x<6?.5f:1.f;scene[y*8+x]={x/7.f,y/3.f,.2f,.25f};hud[y*8+x]={.8f*a,.4f*a,.1f*a,a};}
    D3D11_TEXTURE2D_DESC td{};td.Width=8;td.Height=4;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    auto texture=[&](const Pixel* pixels){ComPtr<ID3D11Texture2D> t;D3D11_SUBRESOURCE_DATA data{pixels,8*sizeof(Pixel),0};Check(SUCCEEDED(dev->CreateTexture2D(&td,pixels?&data:nullptr,&t)),"texture");return t;};
    auto lens=texture(scene.data()),overlay=texture(hud.data()),output=texture(nullptr),sentinel=texture(nullptr);
    ComPtr<ID3D11ShaderResourceView> ls,hs;ComPtr<ID3D11RenderTargetView> target,prior,alias;
    Check(SUCCEEDED(dev->CreateShaderResourceView(lens.Get(),nullptr,&ls)),"lens SRV");
    Check(SUCCEEDED(dev->CreateShaderResourceView(overlay.Get(),nullptr,&hs)),"HUD SRV");
    Check(SUCCEEDED(dev->CreateRenderTargetView(output.Get(),nullptr,&target)),"output RTV");
    Check(SUCCEEDED(dev->CreateRenderTargetView(sentinel.Get(),nullptr,&prior)),"prior RTV");
    Check(SUCCEEDED(dev->CreateRenderTargetView(lens.Get(),nullptr,&alias)),"alias RTV");
    auto read=[&](ID3D11Texture2D* input){
        D3D11_TEXTURE2D_DESC d{};input->GetDesc(&d);d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;Check(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&staging)),"staging");ctx->CopyResource(staging.Get(),input);
        D3D11_MAPPED_SUBRESOURCE map{};Check(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"readback");
        std::array<Pixel,32> result{};for(int y=0;y<4;++y)std::memcpy(result.data()+y*8,static_cast<char*>(map.pData)+map.RowPitch*y,8*sizeof(Pixel));
        ctx->Unmap(staging.Get(),0);return result;
    };
    auto* priorRaw=prior.Get();ctx->OMSetRenderTargets(1,&priorRaw,nullptr);auto* srvRaw=ls.Get();ctx->PSSetShaderResources(5,1,&srvRaw);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);D3D11_VIEWPORT oldVp{2,3,17,19,.2f,.8f};ctx->RSSetViewports(1,&oldVp);
    for(auto mode:{p31lcd::HudAlpha::Premultiplied,p31lcd::HudAlpha::Straight}){
        if(mode==p31lcd::HudAlpha::Straight){for(auto& p:hud){p[0]=.8f;p[1]=.4f;p[2]=.1f;}ctx->UpdateSubresource(overlay.Get(),0,nullptr,hud.data(),8*sizeof(Pixel),0);}
        Check(SUCCEEDED(compositor.Render(ctx.Get(),ls.Get(),hs.Get(),target.Get(),mode)),"LCD render");auto pixels=read(output.Get());
        for(size_t i=0;i<pixels.size();++i){for(int c=0;c<3;++c){float expected=hud[i][c]*(mode==p31lcd::HudAlpha::Straight?hud[i][3]:1)+scene[i][c]*(1-hud[i][3]);Check(std::fabs(pixels[i][c]-expected)<.0001f,"LCD orientation/alpha pixels");}Check(pixels[i][3]==1.f,"LCD opaque");}
        Check(read(lens.Get())==scene&&read(overlay.Get())==hud,"inputs preserved");
        ComPtr<ID3D11RenderTargetView> restored;ctx->OMGetRenderTargets(1,&restored,nullptr);Check(restored.Get()==prior.Get(),"RT state restored");
        ComPtr<ID3D11ShaderResourceView> restoredSrv;ctx->PSGetShaderResources(5,1,&restoredSrv);Check(restoredSrv.Get()==ls.Get(),"SRV restored");
        D3D11_PRIMITIVE_TOPOLOGY topo{};ctx->IAGetPrimitiveTopology(&topo);Check(topo==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"topology restored");
        D3D11_VIEWPORT vp{};UINT count=1;ctx->RSGetViewports(&count,&vp);Check(count==1&&!std::memcmp(&vp,&oldVp,sizeof(vp)),"viewport restored");
    }
    Check(FAILED(compositor.Render(ctx.Get(),ls.Get(),hs.Get(),alias.Get(),p31lcd::HudAlpha::Straight)),"feedback rejected");
    td.Width=4;auto wrongTexture=texture(nullptr);ComPtr<ID3D11RenderTargetView> wrong;Check(SUCCEEDED(dev->CreateRenderTargetView(wrongTexture.Get(),nullptr,&wrong)),"wrong-aspect RTV");
    Check(FAILED(compositor.Render(ctx.Get(),ls.Get(),hs.Get(),wrong.Get(),p31lcd::HudAlpha::Straight)),"aspect distortion rejected");
    Check(read(lens.Get())==scene,"rejected feedback leaves source intact");
    Check(FAILED(compositor.Initialize(nullptr,&D3DCompile)),"invalid init rejected");
    Check(FAILED(compositor.Render(ctx.Get(),ls.Get(),hs.Get(),target.Get(),p31lcd::HudAlpha::Straight)),"failed init clears compositor");
    p31bridge::device=dev;p31bridge::lens=lens;p31bridge::lensView=ls;
    p31bridge::output=output;p31bridge::outputView=hs;p31bridge::outputTarget=target;
    p31bridge::boundLens=p31bridge::drewLens=true;p31bridge::lensTick=123;
    p31native::compositeTick.store(123);
    p31bridge::Reset();
    Check(!p31bridge::device&&!p31bridge::lens&&!p31bridge::lensView&&
          !p31bridge::output&&!p31bridge::outputView&&!p31bridge::outputTarget,"bridge reset releases GPU references");
    Check(!p31bridge::boundLens&&!p31bridge::drewLens&&!p31bridge::lensTick&&
          !p31native::compositeTick.load(),"bridge reset invalidates material readiness");
    // The unarmed bridge must not mutate the game pipeline at all, even with
    // stale lens bookkeeping left by a prior activation.
    auto* previousContext=g_probeGameContext;g_probeGameContext=ctx.Get();
    p31native::runtimeEnabled.store(false);
    p31bridge::boundLens=true;p31bridge::drewLens=false;
    auto* priorRtv=prior.Get();ctx->OMSetRenderTargets(1,&priorRtv,nullptr);
    auto* priorSrv=ls.Get();ctx->PSSetShaderResources(5,1,&priorSrv);
    p31bridge::Bound(ctx.Get(),0,nullptr);
    {p31bridge::DrawScope inactive(ctx.Get());}
    ComPtr<ID3D11RenderTargetView> untouched;ctx->OMGetRenderTargets(1,&untouched,nullptr);
    ComPtr<ID3D11ShaderResourceView> untouchedSrv;ctx->PSGetShaderResources(5,1,&untouchedSrv);
    Check(untouched.Get()==prior.Get()&&untouchedSrv.Get()==ls.Get(),"unarmed bridge preserves render target and texture");
    Check(!p31bridge::drewLens,"unarmed bridge does not mark capture rendered");
    p31bridge::Reset();g_probeGameContext=previousContext;
}
static p20::Q Axis(p20::V axis,float angle){return {axis.x*std::sin(angle/2),axis.y*std::sin(angle/2),axis.z*std::sin(angle/2),std::cos(angle/2)};}
int main()try{
    Check(p31::SpawnSignatures(p31::SpawnPrologue,p31::DestroyPrologue,p31::SpawnCall,p31::DestroyCall,p31::WorldLoad),"native actor signatures accepted");
    auto changed=std::array<unsigned char,sizeof(p31::SpawnPrologue)>{};
    std::memcpy(changed.data(),p31::SpawnPrologue,changed.size());changed[0]^=1;
    Check(!p31::SpawnSignatures(changed.data(),p31::DestroyPrologue,p31::SpawnCall,p31::DestroyCall,p31::WorldLoad),"changed native entry rejected");
    int worldToken=0,classToken=0,ownerToken=0,instigatorToken=0,actorToken=0;
    p20::V spawnLocation{1,2,3};p31::Rotator spawnRotation{4,5,6};
    auto mockSpawn=[&](void* w,void* k,uint64_t name,const p20::V* loc,const p31::Rotator* rot,void* templ,int32_t noCollision,int32_t remote,void* owner,void* instigator,int32_t noFail)->void*{
        Check(w==&worldToken&&k==&classToken&&owner==&ownerToken&&instigator==&instigatorToken,"spawn object argument order");
        Check(name==0&&loc==&spawnLocation&&rot==&spawnRotation&&!templ&&noCollision==1&&remote==0&&noFail==1,"spawn flags/pose argument order");
        return &actorToken;
    };
    Check(p31::SpawnLens(mockSpawn,&worldToken,&classToken,&ownerToken,&instigatorToken,spawnLocation,spawnRotation)==&actorToken,"spawn result preserved");
    Check(p31::LifecycleSignatures(p31::StaticReject,p31::NoDeleteReject,p31::ForceBranch,p31::CleanupFlags),"capture lifecycle signatures");
    for(uint32_t flags:{0u,5u,0xffffffffu,0xa5a5a5a5u}){
        const auto dynamic=p31::DynamicCaptureFlags(flags);
        Check(!(dynamic&5u)&&((dynamic^flags)&~5u)==0,"only static/no-delete instance flags cleared");
    }
    p31::ScanBudget budget{100};int visited=0;
    while(budget.Take(100))++visited;
    Check(visited==128,"scan work cap prevents one-frame full-table traversal");
    budget={200};Check(budget.Take(200)&&budget.Take(201)&&!budget.Take(202),"scan yields when time budget expires");
    budget={300};Check(budget.Take(300),"scan can resume with next frame budget");
    p31::Activation gate;
    Check(!gate.Update(1,false,true,10),"loading cannot activate capture");
    Check(!gate.Update(1,true,true,100),"held key cannot activate on entry");
    Check(!gate.Update(1,true,true,2200),"held key cannot activate after wait");
    Check(!gate.Update(1,true,false,2300),"key release does not activate");
    Check(gate.Update(1,true,true,2400),"fresh gameplay press activates");
    Check(gate.Update(1,true,false,2500),"enabled trial persists");
    Check(!gate.Update(1,true,true,2600),"second press disables");
    gate.Update(1,true,false,2700);gate.Update(1,true,true,2800);
    Check(!gate.Update(2,true,false,2900),"pawn transition disables");
    gate.Update(2,true,true,5000);
    Check(!gate.Update(2,false,false,5100),"scripted movement/focus loss disables");
    TestCompositor();
    constexpr float radians=6.283185307179586f/65536.f;
    for(int i=0;i<1000;++i){
        auto q=p20::Normalize({std::sin(i*.71f),std::cos(i*.23f),std::sin(i*.33f),std::cos(i*.51f)});
        p20::V origin{12,34,56},lens;p31::Rotator r;
        Check(p31::LensPose(origin,q,lens,r),"valid lens pose rejected");
        Check(p20::Length(origin-lens)<1e-5f,"lens origin moved");
        auto reconstructed=p20::Mul(p20::Mul(Axis({0,0,1},r.yaw*radians),Axis({0,1,0},-r.pitch*radians)),Axis({1,0,0},-r.roll*radians));
        for(auto axis:{p20::V{1,0,0},p20::V{0,1,0},p20::V{0,0,1}})
            Check(p20::Length(p20::Rotate(q,axis)-p20::Rotate(reconstructed,axis))<.0003f,"lens orientation roundtrip");
    }
    p20::V lens;p31::Rotator r;
    Check(!p31::LensPose({NAN,0,0},{},lens,r),"invalid lens position accepted");
    Check(!p31::LensPose({},{0,0,0,0},lens,r),"invalid lens rotation accepted");
    p31native::Args args;void* value=reinterpret_cast<void*>(uintptr_t{0x123456789});
    args.Set(4,value);Check(args.Get<void*>(4)==value,"packed UE parameter pointer lost");
    args.Set(12,p20::V{1,2,3});Check(args.Get<p20::V>(12).z==3,"packed vector lost");
    puts("PASS GPU LCD pixels/opacity/state restoration/source integrity, lens poses and packed parameters. Native game rendering is NOT tested.");return 0;
}catch(const std::exception& error){fprintf(stderr,"FAIL %s\n",error.what());return 1;}
