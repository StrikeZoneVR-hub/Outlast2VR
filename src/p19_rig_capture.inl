// Read-only, user-triggered discovery. No skeletal transforms are written.
std::atomic<unsigned> g_p19CaptureRequest{0};
constexpr size_t kP19CandidateStep=4;
bool P19CaptureReady(ULONGLONG now,ULONGLONG after,bool left,bool right){return after&&now>=after&&now<=after+20000&&left&&right;}
std::wstring P19Directory(){return ModuleDir()+L"\\outlast2_vr_p19_rig_"+std::to_wstring(GetCurrentProcessId());}
template<class T> bool P19Read(const void* object,size_t offset,T& value){
    const uintptr_t address=reinterpret_cast<uintptr_t>(object);
    if(!address||offset>UINTPTR_MAX-address)return false;
    const auto* p=reinterpret_cast<const void*>(address+offset);
    if(!P15Readable(p,sizeof(T)))return false;
    std::memcpy(&value,p,sizeof(T));return true;
}
struct P19Skeleton {void* mesh=nullptr;void* bones=nullptr;int32_t count=0;};
bool P19FindSkeleton(void* component,P19Skeleton& out){
    out={};
    // GetBoneName/GetBoneLocation in the exact shipping executable establish
    // Component.SkeletalMesh +278; mesh.RefSkeleton pointer +CC/count +D4; stride80.
    if(!P19Read(component,0x278,out.mesh)||!out.mesh)return false;
    if(!P19Read(out.mesh,0xcc,out.bones)||!P19Read(out.mesh,0xd4,out.count))return false;
    int32_t capacity=0;if(!P19Read(out.mesh,0xd8,capacity))return false;
    return out.count>=16&&out.count<=512&&capacity>=out.count&&capacity<=2048&&
        P15Readable(out.bones,static_cast<size_t>(out.count)*80);
}
bool P19WriteBytes(const std::wstring& path,const void* data,size_t size){
    if(!P15Readable(data,size))return false;
    FILE* f=nullptr;if(_wfopen_s(&f,path.c_str(),L"wb")||!f)return false;
    const bool ok=fwrite(data,1,size,f)==size;fclose(f);return ok;
}
bool P19RigSignatures(){
    const auto* base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    const unsigned char meshLoad[]={0x48,0x8b,0x96,0x78,0x02,0,0};
    const unsigned char boneCount[]={0x3b,0x82,0xd4,0,0,0};
    const unsigned char bonePtr[]={0x48,0x8b,0x82,0xcc,0,0,0};
    const unsigned char namesLoad[]={0x48,0x8b,0x05,0x9f,0x9d,0x12,0x02};
    return base&&P15Readable(base+0x7390ab,7)&&P15Readable(base+0xb8fda,7)&&
        !std::memcmp(base+0xb8fda,namesLoad,sizeof(namesLoad))&&
        !std::memcmp(base+0x73908a,meshLoad,sizeof(meshLoad))&&
        !std::memcmp(base+0x73909f,boneCount,sizeof(boneCount))&&
        !std::memcmp(base+0x7390ab,bonePtr,sizeof(bonePtr));
}
std::string P19BoneName(const void* bone){
    int32_t index=0,number=0;
    if(!P19Read(bone,0,index)||!P19Read(bone,4,number)||index<0||index>1000000)return "<invalid-name>";
    const auto* base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    void* table=nullptr;void* entry=nullptr;unsigned char flags=0;
    if(!P19Read(base,0x21e2d80,table)||!P19Read(table,static_cast<size_t>(index)*8,entry)||!P19Read(entry,8,flags))return "<unreadable-name>";
    std::string name;
    for(size_t i=0;i<128;++i){
        uint16_t c=0;
        if(flags&1){if(!P19Read(entry,0x14+i*2,c))return "<unreadable-wide-name>";}
        else{unsigned char a=0;if(!P19Read(entry,0x14+i,a))return "<unreadable-name>";c=a;}
        if(!c){if(number>0)name+="_"+std::to_string(number-1);return name;}
        name+=c>=32&&c<127?static_cast<char>(c):'?';
    }
    return "<overlong-name>";
}
void P19InspectPawn(void* self){
    if(!g_p19CaptureRequest.load()||!P15Focused()||!g_p10GameplayActive.load())return;
    void* pawn=nullptr;if(!P19Read(self,0xc38,pawn)||!P15Readable(pawn,0x800))return;
    const unsigned request=g_p19CaptureRequest.exchange(0);if(!request)return;
    if(!P19RigSignatures()){Log("P19 rig capture refused: bone-layout signatures differ.");return;}
    const auto dir=P19Directory();CreateDirectoryW(dir.c_str(),nullptr);
    const auto prefix=dir+L"\\sample_"+std::to_wstring(request);
    FILE* report=nullptr;if(_wfopen_s(&report,(prefix+L"_rig.txt").c_str(),L"w")||!report){Log("P19 cannot create rig report.");return;}
    fprintf(report,"P19 READ-ONLY CANDIDATES; not confirmed player mesh identities\ncontroller=%p pawn=%p\n",self,pawn);
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const uintptr_t end=base+nt->OptionalHeader.SizeOfImage;
    size_t candidates=0;std::vector<void*> seen;
    // Only direct object pointers within the player pawn. Bounded to 8KiB and16 candidates.
    // UE3 has 4-byte packed pointer fields even in this x64 executable.
    for(size_t offset=0;offset<0x2000&&candidates<16;offset+=kP19CandidateStep){
        void* component=nullptr;if(!P19Read(pawn,offset,component))break;
        if(!component||std::find(seen.begin(),seen.end(),component)!=seen.end())continue;
        uintptr_t vt=0;if(!P19Read(component,0,vt)||vt<base||vt>=end)continue;
        P19Skeleton sk;if(!P19FindSkeleton(component,sk))continue;
        seen.push_back(component);++candidates;
        const auto name=prefix+L"_candidate_"+std::to_wstring(candidates);
        const bool ref=P19WriteBytes(name+L"_refbones.bin",sk.bones,static_cast<size_t>(sk.count)*80);
        const bool comp=P19WriteBytes(name+L"_component.bin",component,0x800);
        fprintf(report,"candidate=%zu pawnOffset=%zx component=%p vtableRva=%llx mesh=%p bones=%p count=%d refSaved=%d componentSaved=%d\n",candidates,offset,component,static_cast<unsigned long long>(vt-base),sk.mesh,sk.bones,sk.count,ref,comp);
        for(int i=0;i<sk.count;++i)fprintf(report,"bone=%d name=%s\n",i,P19BoneName(static_cast<const unsigned char*>(sk.bones)+static_cast<size_t>(i)*80).c_str());
    }
    fprintf(report,"candidates=%zu\nNo bone writes performed.\n",candidates);fclose(report);
    Log("P19 RIG CAPTURE: sample=%u candidateMeshes=%zu; candidate layouts only, no bone writes. Folder=%ls",request,candidates,dir.c_str());
}
