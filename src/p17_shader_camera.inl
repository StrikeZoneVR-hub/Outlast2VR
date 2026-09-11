constexpr GUID kP17CameraFields={0x88a39f65,0xa781,0x4af2,{0x93,0xa1,0xee,0x7b,0x54,0x92,0x10,0x17}};
std::atomic<uint64_t> g_p17TaggedShaders{0};
uint64_t g_p17FieldMatches=0;
void P17TagPixelShader(const void* bytes,size_t size,ID3D11PixelShader* shader){
    using ReflectFn=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
    static HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    static ReflectFn reflect=compiler?reinterpret_cast<ReflectFn>(GetProcAddress(compiler,"D3DReflect")):nullptr;
    if(!reflect||!shader)return;
    ID3D11ShaderReflection* reflection=nullptr;
    if(FAILED(reflect(bytes,size,__uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(&reflection))))return;
    p17::Layout layout{};D3D11_SHADER_DESC desc{};
    if(SUCCEEDED(reflection->GetDesc(&desc)))for(UINT i=0;i<desc.ConstantBuffers;++i){
        auto* cb=reflection->GetConstantBufferByIndex(i);D3D11_SHADER_BUFFER_DESC bd{};D3D11_SHADER_INPUT_BIND_DESC binding{};
        if(FAILED(cb->GetDesc(&bd))||FAILED(reflection->GetResourceBindingDescByName(bd.Name,&binding))||binding.BindPoint>=14)continue;
        for(UINT j=0;j<bd.Variables&&layout.count<32;++j){
            D3D11_SHADER_VARIABLE_DESC vd{};if(FAILED(cb->GetVariableByIndex(j)->GetDesc(&vd))||!(vd.uFlags&D3D_SVF_USED))continue;
            uint32_t kind=0;
            if(!std::strcmp(vd.Name,"CameraWorldPos")||!std::strcmp(vd.Name,"CameraWorldPosition"))kind=p17::Position;
            if(!std::strcmp(vd.Name,"WorldToViewMatrix"))kind=p17::WorldToView;
            if(!std::strcmp(vd.Name,"ViewToWorldMatrix"))kind=p17::ViewToWorld;
            if(kind){
                D3D11_SHADER_TYPE_DESC type{};uint32_t packing=0;
                if(SUCCEEDED(cb->GetVariableByIndex(j)->GetType()->GetDesc(&type)))
                    packing=type.Class==D3D_SVC_MATRIX_ROWS?1:type.Class==D3D_SVC_MATRIX_COLUMNS?2:0;
                layout.fields[layout.count++]={binding.BindPoint,vd.StartOffset,vd.Size,kind,packing};
            }
        }
    }
    reflection->Release();
    if(layout.count&&SUCCEEDED(shader->SetPrivateData(kP17CameraFields,sizeof(layout),&layout)))++g_p17TaggedShaders;
}
p17::Layout P17GetPixelLayout(ID3D11DeviceContext* ctx){
    p17::Layout layout{};ID3D11PixelShader* shader=nullptr;ctx->PSGetShader(&shader,nullptr,nullptr);
    if(shader){UINT size=sizeof(layout);if(FAILED(shader->GetPrivateData(kP17CameraFields,&size,&layout))||layout.count>32)layout={};shader->Release();}
    return layout;
}
