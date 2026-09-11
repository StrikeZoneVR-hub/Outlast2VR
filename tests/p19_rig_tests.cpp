#include "../src/dinput8_proxy.cpp"
#include <stdexcept>
static void Check(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
static int creates=0,destroys=0;static bool failCreate=false,active=true,tracked=true;
static XrTime requested=0;
static XrResult XRAPI_PTR CreateSpace(XrSession,const XrActionSpaceCreateInfo* ci,XrSpace* out){
    Check(ci->poseInActionSpace.orientation.w==1,"identity grip offset");
    if(failCreate&&creates==1)return XR_ERROR_RUNTIME_FAILURE;
    *out=reinterpret_cast<XrSpace>(uintptr_t(++creates));return XR_SUCCESS;
}
static XrResult XRAPI_PTR Destroy(XrSpace){++destroys;return XR_SUCCESS;}
static XrResult XRAPI_PTR StatePose(XrSession,const XrActionStateGetInfo*,XrActionStatePose* out){out->isActive=active;return XR_SUCCESS;}
static XrResult XRAPI_PTR Locate(XrSpace,XrSpace,XrTime time,XrSpaceLocation* out){
    requested=time;out->pose={{0,0,0,1},{.25f,1.2f,-.4f}};
    out->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if(tracked)out->locationFlags|=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    return XR_SUCCESS;
}
template<class T> static void Put(std::vector<unsigned char>& memory,size_t offset,T value){std::memcpy(memory.data()+offset,&value,sizeof(value));}
int main()try{
    std::vector<unsigned char> component(0x800),mesh(0x200),bones(80*60);
    Put(component,0x278,mesh.data());Put(mesh,0xcc,bones.data());Put<int32_t>(mesh,0xd4,60);Put<int32_t>(mesh,0xd8,60);
    P19Skeleton sk;Check(P19FindSkeleton(component.data(),sk)&&sk.count==60&&sk.bones==bones.data(),"validated layout read");
    std::vector<unsigned char> packed(32);Put(packed,4,component.data());bool found=false;
    for(size_t offset=0;offset+8<=packed.size();offset+=kP19CandidateStep){void* candidate=nullptr;if(P19Read(packed.data(),offset,candidate)&&candidate==component.data())found=true;}
    Check(found,"4-byte packed component pointer discovered");
    Check(!P19CaptureReady(4999,5000,true,true)&&P19CaptureReady(5000,5000,true,true),"five-second capture delay");
    Check(!P19CaptureReady(6000,5000,false,true)&&!P19CaptureReady(6000,5000,true,false),"both poses required");
    Check(!P19CaptureReady(25001,5000,true,true)&&!P19CaptureReady(6000,0,true,true),"timeout and unarmed capture rejected");
    Put<int32_t>(mesh,0xd4,513);Check(!P19FindSkeleton(component.data(),sk),"count capped");
    Put<int32_t>(mesh,0xd4,60);Put<int32_t>(mesh,0xd8,59);Check(!P19FindSkeleton(component.data(),sk),"array capacity checked");
    Check(!P19FindSkeleton(nullptr,sk),"null rejected");
    OpenXRQuad q;q.p19TrackingEnabled=true;q.p19PoseBound=true;q.p19CreateSpace=CreateSpace;q.xrDestroySpace=Destroy;
    q.P19CreateSpaces();Check(creates==2&&q.p19Spaces[0]&&q.p19Spaces[1],"both grip spaces");
    q.p19GetPose=StatePose;q.xrLocateSpace=Locate;q.gameplayVr=true;q.state=XR_SESSION_STATE_FOCUSED;q.p12HaveCenter=true;q.p12Pending.valid=true;
    q.p19CaptureCount=3; // tests never trigger live capture/files even if F9 is held
    q.P19Locate(123456,true);Check(q.p19Valid[0]&&q.p19Valid[1]&&requested==123456,"same predicted display time");
    tracked=false;q.P19Locate(123457,true);Check(!q.p19Valid[0]&&!q.p19Valid[1],"untracked poses rejected");
    tracked=true;active=false;q.P19Locate(123458,true);Check(!q.p19Valid[0],"inactive actions rejected");
    active=true;q.P19Locate(123459,false);Check(!q.p19Valid[0],"focus loss clears");
    q.p12Pending.valid=false;q.P19Locate(123460,true);Check(!q.p19Valid[0],"invalid head frame clears");
    OpenXRQuad failed;failed.p19TrackingEnabled=true;failed.p19PoseBound=true;failed.p19CreateSpace=CreateSpace;failed.xrDestroySpace=Destroy;creates=0;failCreate=true;
    failed.P19CreateSpaces();Check(!failed.p19PoseBound&&destroys==1&&!failed.p19Spaces[0],"partial space cleanup");
    puts("PASS P19 bounded skeleton reads, pose timing/tracking/focus validation and partial cleanup.");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}
