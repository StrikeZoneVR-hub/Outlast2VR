struct P15PixelShadow {ID3D11Buffer* source=nullptr;ID3D11Buffer* shadow=nullptr;std::vector<uint8_t> last;};
std::vector<P15PixelShadow> g_p15PixelShadows;
std::atomic<bool> g_p15ReflectionEnabled{true};
uint64_t g_p15PixelMatches=0;
static bool P15IsRetainedSceneDepth(ID3D11DepthStencilView* depth) {
    if(!depth||P11AClassifyDepthStencilView(depth)!=P11ADepthPassKind::MainScene)return false;
    ID3D11Resource *actual=nullptr,*expected=nullptr;
    depth->GetResource(&actual);
    {std::lock_guard<std::mutex> lock(g_earlyDepthMutex);
        if(g_earlyFullResDsv)g_earlyFullResDsv->GetResource(&expected);}
    const bool same=actual&&expected&&actual==expected;
    if(actual)actual->Release();if(expected)expected->Release();
    return same;
}
struct P15PixelScope {
    ID3D11DeviceContext* ctx;ID3D11Buffer* originals[14]{};bool replaced[14]{};
    explicit P15PixelScope(ID3D11DeviceContext* c):ctx(c){
        if(c!=g_probeGameContext||!g_p15ReflectionEnabled.load()||!g_p10GameplayActive.load()||!g_p10ViewProjectionValidated.load())return;
        ID3D11DepthStencilView* ds=nullptr;c->OMGetRenderTargets(0,nullptr,&ds);
        // Only a validated scene depth target may receive the correction.
        // No-depth postprocessing/UI and shadow/reflection capture passes keep
        // their own camera constants, even when they reuse the same buffers.
        const bool excluded=!P15IsRetainedSceneDepth(ds);
        if(ds)ds->Release();if(excluded)return;
        float camera[40]{};
        {
            std::lock_guard<std::mutex> lock(g_cbProbeMutex);
            auto it=g_cbProbeStates.find(g_p10ViewProjectionBuffer.load());
            if(it==g_cbProbeStates.end()||it->second.bytes.size()!=160||it->second.lastFrame!=g_probeFrameCounter.load())return;
            std::memcpy(camera,it->second.bytes.data(),160);
        }
        const auto head=P11LoadHeadConstants();if(head.params[2]<0.5f)return;
        static p15::PixelCamera prepared(camera,head);static p12::HeadConstants savedHead{};
        if(std::memcmp(prepared.old,camera,160)||std::memcmp(&savedHead,&head,sizeof(head))){prepared=p15::PixelCamera(camera,head);savedHead=head;}
        c->PSGetConstantBuffers(0,14,originals);
        const auto layout=P17GetPixelLayout(c);
        for(UINT i=0;i<14;++i){
            if(!originals[i])continue;std::vector<uint8_t> bytes;
            {
                std::lock_guard<std::mutex> lock(g_cbProbeMutex);auto it=g_cbProbeStates.find(originals[i]);
                if(it==g_cbProbeStates.end()||it->second.lastFrame!=g_probeFrameCounter.load())continue;
                bytes=it->second.bytes;
            }
            bool changed=p15::PatchPixelCameraPrepared(bytes,prepared);
            for(UINT f=0;f<layout.count;++f)if(layout.fields[f].slot==i&&p17::PatchField(bytes,layout.fields[f],prepared)){changed=true;++g_p17FieldMatches;}
            if(!changed)continue;
            P15PixelShadow* entry=nullptr;
            for(auto& item:g_p15PixelShadows)if(item.source==originals[i]){entry=&item;break;}
            if(!entry){
                if(g_p15PixelShadows.size()>=64)continue;
                D3D11_BUFFER_DESC desc{};originals[i]->GetDesc(&desc);
                if(desc.ByteWidth!=bytes.size())continue;
                desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                ID3D11Device* device=nullptr;c->GetDevice(&device);ID3D11Buffer* shadow=nullptr;
                const HRESULT hr=device->CreateBuffer(&desc,nullptr,&shadow);device->Release();if(FAILED(hr))continue;
                originals[i]->AddRef();g_p15PixelShadows.push_back({originals[i],shadow,{}});entry=&g_p15PixelShadows.back();
            }
            if(entry->last!=bytes){g_realUpdateSubresource(c,entry->shadow,0,nullptr,bytes.data(),0,0);entry->last=bytes;}
            c->PSSetConstantBuffers(i,1,&entry->shadow);replaced[i]=true;
            if(++g_p15PixelMatches<=8)Log("P17 WORLD CAMERA PATCH ACTIVE: PSslot=%u bytes=%zu; absolute world/translated camera conversion validated, per-eye shading constants bound.",i,bytes.size());
        }
    }
    ~P15PixelScope(){for(UINT i=0;i<14;++i){if(replaced[i])ctx->PSSetConstantBuffers(i,1,&originals[i]);if(originals[i])originals[i]->Release();}}
};
