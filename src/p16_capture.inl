// Temporary, bounded read-only GPU capture. No reflection or tracking transformations.
constexpr GUID kP16ShaderId={0x88a39f65,0xa781,0x4af2,{0x93,0xa1,0xee,0x7b,0x54,0x92,0x10,0x16}};
std::mutex g_p16FileMutex;
std::map<uint64_t,size_t> g_p16Shaders;
size_t g_p16ShaderBytes=0;
std::wstring P16CaptureDir(){
    static std::wstring path=[](){
        const auto root=ModuleDir()+L"\\outlast2_vr_p16_capture_"+std::to_wstring(GetCurrentProcessId());
        if(!CreateDirectoryW(root.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){Log("P16 CAPTURE DIRECTORY unavailable; no capture written.");return std::wstring{};}
        return root;
    }();return path;
}
bool P16Write(const std::wstring& name,const void* bytes,size_t size){
    const auto directory=P16CaptureDir();if(directory.empty())return false;
    FILE* file=nullptr;if(_wfopen_s(&file,(directory+L"\\"+name).c_str(),L"wb")||!file)return false;
    const bool ok=fwrite(bytes,1,size,file)==size;fclose(file);return ok;
}
void P16Shader(const wchar_t* stage,const void* bytes,size_t size,ID3D11DeviceChild* shader){
    if(!bytes||!shader||size<4||size>1024*1024)return;
    uint64_t hash=14695981039346656037ull;
    const auto* p=static_cast<const uint8_t*>(bytes);for(size_t i=0;i<size;++i){hash^=p[i];hash*=1099511628211ull;}
    shader->SetPrivateData(kP16ShaderId,sizeof(hash),&hash);
    std::lock_guard<std::mutex> lock(g_p16FileMutex);
    if(g_p16Shaders.count(hash)||g_p16Shaders.size()>=8192||g_p16ShaderBytes+size>64*1024*1024)return;
    wchar_t name[80]{};swprintf_s(name,L"%s_%016llx.dxbc",stage,static_cast<unsigned long long>(hash));
    if(P16Write(name,bytes,size)){g_p16Shaders.emplace(hash,size);g_p16ShaderBytes+=size;}
}
std::vector<uint8_t> P16ReadBuffer(ID3D11DeviceContext* ctx,ID3D11Buffer* source){
    if(!ctx||!source)return {};
    D3D11_BUFFER_DESC desc{};source->GetDesc(&desc);if(desc.ByteWidth<16||desc.ByteWidth>4096)return {};
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;desc.StructureByteStride=0;
    ID3D11Device* device=nullptr;ctx->GetDevice(&device);ID3D11Buffer* staging=nullptr;
    const HRESULT hr=device->CreateBuffer(&desc,nullptr,&staging);device->Release();if(FAILED(hr))return {};
    ctx->CopyResource(staging,source);D3D11_MAPPED_SUBRESOURCE mapped{};std::vector<uint8_t> result;
    if(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&mapped))){result.resize(desc.ByteWidth);std::memcpy(result.data(),mapped.pData,desc.ByteWidth);ctx->Unmap(staging,0);}
    staging->Release();return result;
}
unsigned g_p16Epoch=0,g_p16Samples=0,g_p16EpochSamples=0;
unsigned g_p16SceneSamples=0,g_p16ScreenSamples=0;
uint64_t g_p16LastFrame=~0ull;
bool g_p16Armed=false,g_p16KeyDown=false;
std::vector<uint64_t> g_p16Seen;
void P16Arm(bool focused){
    const bool down=focused&&(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(down&&!g_p16KeyDown&&g_p16Epoch<2){
        ++g_p16Epoch;g_p16EpochSamples=0;g_p16SceneSamples=g_p16ScreenSamples=0;g_p16Seen.clear();g_p16Armed=true;
        Log("P16 CAPTURE ARMED: epoch=%u; max 16 distinct pixel shaders, one draw per frame. Hold this view briefly; rendering may hitch during readback.",g_p16Epoch);
    }
    g_p16KeyDown=down;
}
void P16CaptureDraw(ID3D11DeviceContext* ctx){
    if(ctx!=g_probeGameContext||!g_p16Armed||!g_p10GameplayActive.load()||!g_p10ViewProjectionValidated.load())return;
    const auto frame=g_probeFrameCounter.load();if(frame==g_p16LastFrame)return;
    ID3D11DepthStencilView* ds=nullptr;ctx->OMGetRenderTargets(0,nullptr,&ds);
    const bool excluded=ds&&P11AClassifyDepthStencilView(ds)!=P11ADepthPassKind::MainScene;
    const bool hasDepth=ds!=nullptr;if(ds)ds->Release();if(excluded)return;
    ID3D11DepthStencilState* depthState=nullptr;UINT stencilRef=0;ctx->OMGetDepthStencilState(&depthState,&stencilRef);
    D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;
    if(depthState){depthState->GetDesc(&dd);depthState->Release();}
    const bool scene=hasDepth&&dd.DepthEnable;
    if((scene&&g_p16SceneSamples>=4)||(!scene&&g_p16ScreenSamples>=12))return;
    ID3D11PixelShader* ps=nullptr;ctx->PSGetShader(&ps,nullptr,nullptr);if(!ps)return;
    uint64_t id=0;UINT idSize=sizeof(id);ps->GetPrivateData(kP16ShaderId,&idSize,&id);ps->Release();
    if(!id||std::find(g_p16Seen.begin(),g_p16Seen.end(),id)!=g_p16Seen.end())return;
    g_p16Seen.push_back(id);g_p16LastFrame=frame;++g_p16Samples;++g_p16EpochSamples;
    if(scene)++g_p16SceneSamples;else ++g_p16ScreenSamples;
    wchar_t prefix[64]{};swprintf_s(prefix,L"e%u_s%02u",g_p16Epoch,g_p16Samples);
    char line[256]{};sprintf_s(line,"epoch=%u sample=%u frame=%llu ps=%016llx depth=%d\n",g_p16Epoch,g_p16Samples,(unsigned long long)frame,(unsigned long long)id,hasDepth?1:0);
    std::string manifest=line;
    D3D11_VIEWPORT viewport{};UINT viewportCount=1;ctx->RSGetViewports(&viewportCount,&viewport);
    sprintf_s(line,"viewport=%g,%g,%g,%g,%g,%g scene=%d\n",viewport.TopLeftX,viewport.TopLeftY,viewport.Width,viewport.Height,viewport.MinDepth,viewport.MaxDepth,scene?1:0);manifest+=line;
    auto writeBuffer=[&](const std::wstring& label,ID3D11Buffer* buffer){
        auto data=P16ReadBuffer(ctx,buffer);if(data.empty())return;
        const auto name=std::wstring(prefix)+L"_"+label+L".bin";
        if(P16Write(name,data.data(),data.size())){sprintf_s(line,"%ls %zu bytes\n",name.c_str(),data.size());manifest+=line;}
    };
    writeBuffer(L"original_camera",g_p10ViewProjectionBuffer.load());
    ID3D11Buffer* buffers[14]{};ctx->PSGetConstantBuffers(0,14,buffers);
    for(UINT i=0;i<14;++i)if(buffers[i]){writeBuffer(L"ps_b"+std::to_wstring(i),buffers[i]);buffers[i]->Release();}
    std::memset(buffers,0,sizeof(buffers));ctx->VSGetConstantBuffers(0,14,buffers);
    for(UINT i=0;i<14;++i)if(buffers[i]){writeBuffer(L"vs_b"+std::to_wstring(i),buffers[i]);buffers[i]->Release();}
    ID3D11VertexShader* vs=nullptr;ctx->VSGetShader(&vs,nullptr,nullptr);uint64_t vid=0;idSize=sizeof(vid);
    if(vs){vs->GetPrivateData(kP16ShaderId,&idSize,&vid);vs->Release();}
    sprintf_s(line,"vs=%016llx\n",(unsigned long long)vid);manifest+=line;
    const auto head=P11LoadHeadConstants();P16Write(std::wstring(prefix)+L"_head.bin",&head,sizeof(head));
    P16Write(std::wstring(prefix)+L"_manifest.txt",manifest.data(),manifest.size());
    Log("P16 CAPTURE SAMPLE: %ls ps=%016llx; live GPU data, original camera and headset constants saved.",prefix,(unsigned long long)id);
    if(g_p16EpochSamples>=16){g_p16Armed=false;Log("P16 CAPTURE EPOCH COMPLETE: %u. Capture directory: %ls",g_p16Epoch,P16CaptureDir().c_str());}
}
