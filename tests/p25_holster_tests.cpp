#include "../src/dinput8_proxy.cpp"
#include <stdexcept>
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    p20::Skeleton before{},after{};
    before[2].p={0,0,95};after=before;
    auto untouched=after;p25::Attachment a;a.component=123;
    Check(p25::Holster(a,before,after,100,456),"valid holster");
    Check(a.component==123&&a.bone==456,"identity retained/root selected");
    Check(p20::Length(a.translation-p20::V{2,23,89})<.001f,"right hip offset");
    Check(!std::memcmp(&after,&untouched,sizeof(after)),"body and hands untouched");
    p20::Q yaw{0,0,std::sqrt(.5f),std::sqrt(.5f)};
    after[2].q=yaw;after[2].p={10,20,95};
    Check(p25::Holster(a,before,after,100,456),"turned holster");
    Check(p20::Length(a.translation-p20::V{-13,22,89})<.001f,"yaw and roomscale follow hip");
    Check(a.yaw==16384&&a.pitch==-16384,"native rotator units");
    Check(!p25::Holster(a,before,after,NAN,456),"reject NaN scale");
    Check(!p25::Holster(a,before,after,0,456),"reject zero scale");
    after[0].scale=2;Check(!p25::Holster(a,before,after,100,456),"reject unsupported root scale");
    // Real native TArray byte layout; duplicate identities must fail closed.
    std::array<unsigned char,0x500> parent{};p25::Attachment records[300]{};records[0].component=123;records[1].component=999;
    auto* ptr=records;int count=2;std::memcpy(parent.data()+0x45c,&ptr,8);
    std::memcpy(parent.data()+0x464,&count,4);std::memcpy(parent.data()+0x468,&count,4);
    Check(P25Record(parent.data(),reinterpret_cast<void*>(123))==records,"record lookup");
    // P34's runtime default intentionally keeps the native camera hidden. The
    // unit test is specifically exercising the legacy record-restore branch.
    g_p32HideCamcorder=false;
    g_p25Parent=parent.data();g_p25Camera=reinterpret_cast<void*>(123);
    g_p25Before=records[0];g_p25After=records[0];g_p25After.translation.x=25;records[0]=g_p25After;g_p25HaveRecord=true;
    P25RestoreRecord(parent.data());Check(records[0].translation.x==0,"restore own transform");
    records[0]=g_p25After;records[0].translation.y=12;g_p25HaveRecord=true;
    P25RestoreRecord(parent.data());Check(records[0].translation.y==12,"preserve engine changes");
    count=50;std::memcpy(parent.data()+0x464,&count,4);std::memcpy(parent.data()+0x468,&count,4);
    Check(P25Record(parent.data(),reinterpret_cast<void*>(123))==records,"actual Hero 50-attachment count accepted");
    records[1].component=123;Check(!P25Record(parent.data(),reinterpret_cast<void*>(123)),"reject duplicate component");
    count=257;std::memcpy(parent.data()+0x464,&count,4);
    Check(!P25Record(parent.data(),reinterpret_cast<void*>(123)),"reject excessive attachment count");
    puts("P25 holster math, identity, restoration and bounds passed.");
}
