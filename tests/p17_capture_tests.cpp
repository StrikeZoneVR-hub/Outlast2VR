#include "p17_camera_fields.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::vector<uint8_t> Read(const fs::path& p){std::ifstream f(p,std::ios::binary);Check(bool(f),"fixture readable");return {std::istreambuf_iterator<char>(f),{}};}
void Near(float a,float b){Check(std::isfinite(a)&&std::fabs(a-b)<=0.003f+std::fabs(b)*0.00002f,"capture GPU/corrected world camera agreement");}
void Test(std::vector<uint8_t> bytes,const float* original,const p12::HeadConstants& head,const float* rendered){
    const auto before=bytes;p15::PixelCamera camera(original,head);
    Check(p15::PatchPixelCameraPrepared(bytes,camera),"absolute-world PS camera recognized");
    float expected[16];p15::PixelCamera::WorldMatrix(rendered,rendered+36,expected);
    float matrix[16];std::memcpy(matrix,bytes.data()+144,64);
    for(int i=0;i<16;++i)Near(matrix[i],expected[i]);
    for(int i=0;i<3;++i){float v;std::memcpy(&v,bytes.data()+80+4*i,4);Near(v,rendered[32+i]-rendered[36+i]);}
    for(size_t i=0;i<bytes.size();++i)if(!(i>=80&&i<92)&&!(i>=128&&i<136)&&!(i>=144&&i<208))Check(bytes[i]==before[i],"unrelated constants and padding unchanged");
    for(int i=0;i<2;++i){float v;std::memcpy(&v,bytes.data()+128+4*i,4);Near(v,1.0f/head.projection[i]);}
    for(bool transposed:{false,true})for(uint32_t kind:{p17::WorldToView,p17::ViewToWorld}){
        std::vector<uint8_t> field(80,0x5a);const auto padding=field;
        for(int r=0;r<3;++r)for(int c=0;c<3;++c){int index=transposed?4*c+r:4*r+c,source=kind==p17::WorldToView?4*r+c:4*c+r;std::memcpy(field.data()+16+4*index,camera.oldView+source,4);}
        Check(p17::PatchField(field,{0,16,44,kind,transposed?2u:1u},camera),"packed view basis matches in either layout");
        for(int r=0;r<3;++r)for(int c=0;c<3;++c){int index=transposed?4*c+r:4*r+c,source=kind==p17::WorldToView?4*r+c:4*c+r;float v;std::memcpy(&v,field.data()+16+4*index,4);Near(v,camera.nextView[source]);}
        for(int i:{28,44})for(int j=0;j<4;++j)Check(field[i+j]==padding[i+j],"float3x3 padding preserved");
    }
}
int main(int argc,char** argv)try{
    if(argc==2){
        fs::path root(argv[1]);unsigned count=0;
        for(auto& entry:fs::directory_iterator(root)){
            const auto name=entry.path().filename().string();const auto suffix=std::string("_manifest.txt");
            if(!name.ends_with(suffix))continue;
            auto manifest=Read(entry.path());std::string text(manifest.begin(),manifest.end());if(text.find("scene=1")==std::string::npos)continue;
            auto stem=name.substr(0,name.size()-suffix.size());auto a=Read(root/(stem+"_original_camera.bin")),b=Read(root/(stem+"_ps_b2.bin")),v=Read(root/(stem+"_vs_b1.bin")),h=Read(root/(stem+"_head.bin"));
            Check(a.size()==160&&b.size()==224&&v.size()==160&&h.size()==96,"captured sizes");
            float old[40],rendered[40];p12::HeadConstants head;std::memcpy(old,a.data(),160);std::memcpy(rendered,v.data(),160);std::memcpy(&head,h.data(),96);
            Test(b,old,head,rendered);++count;
        }
        Check(count>=8,"both captured epochs tested");std::cout<<"PASS real captured world-camera samples: "<<count<<"\n";return 0;
    }
    for(int n=0;n<40;++n){
        float old[40]{};float angle=-1+float(n)*0.05f;
        for(int b:{0,16}){old[b]=0.9f;old[b+5]=-std::sin(angle)*1.6f;old[b+9]=std::cos(angle)*1.6f;old[b+7]=std::cos(angle);old[b+11]=std::sin(angle);old[b+6]=old[b+7]*0.001f;old[b+10]=old[b+11]*0.001f;old[b+14]=1.998f;}
        old[35]=old[39]=1;old[36]=2661.887f;old[37]=6584.982f;old[38]=-2212.362f;
        p12::HeadConstants head{};head.right[0]=head.up[1]=head.forward[2]=1;head.right[3]=1;head.params[2]=head.params[3]=1;
        head.projection[0]=0.93f;head.projection[1]=0.87f;head.projection[2]=0.06f;head.projection[3]=0.02f;head.eyeOffset[0]=3;head.eyeOffset[1]=2;head.eyeOffset[2]=1;
        p15::PixelCamera camera(old,head);std::vector<uint8_t> bytes(224,0x5a);
        std::memcpy(bytes.data()+144,camera.oldWorld[0],64);std::memcpy(bytes.data()+80,camera.oldPosition,12);float scale[2]={1/0.9f,1/1.6f};std::memcpy(bytes.data()+128,scale,8);
        Test(bytes,old,head,camera.next);
    }
    std::cout<<"PASS world camera conversion, pitch sweep, projection scales, packed shading basis and padding\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
