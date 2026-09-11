#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace p12 {
// Packed exactly like six HLSL float4 registers. Coordinates are camera R/U/F.
struct HeadConstants {
    float right[4];
    float up[4];
    float forward[4];
    float params[4];       // legacy scale, orientation valid, native projection, update camera position
    float projection[4];   // scale X/Y, asymmetric offset X/Y
    float eyeOffset[4];    // XYZ eye displacement in game units; W = native camcorder baseline projection scale
};
static_assert(sizeof(HeadConstants) == 96);

inline bool Projection(float left, float right, float down, float up, float* out) {
    for (float angle : {left, right, down, up})
        if (!std::isfinite(angle) || std::fabs(angle) >= 1.55f) return false;
    const float l = std::tan(left), r = std::tan(right);
    const float d = std::tan(down), u = std::tan(up);
    if (r-l < 0.01f || u-d < 0.01f) return false;
    out[0] = 2.0f/(r-l); out[1] = 2.0f/(u-d);
    out[2] = -(r+l)/(r-l); out[3] = -(u+d)/(u-d);
    return true;
}

inline float RelativeZoom(float sourceProjectionScale, float baselineProjectionScale) {
    if (!std::isfinite(sourceProjectionScale) || !std::isfinite(baselineProjectionScale) ||
        sourceProjectionScale <= 0.0f || baselineProjectionScale < 0.25f) return 1.0f;
    return std::clamp(sourceProjectionScale / baselineProjectionScale, 0.75f, 6.0f);
}

inline void PatchMatrix(float* m, const HeadConstants& h) {
    float r[3] = {m[0],m[4],m[8]}, u[3] = {m[1],m[5],m[9]};
    float z[3] = {m[2],m[6],m[10]}, f[3] = {m[3],m[7],m[11]};
    auto len = [](const float* v) { return std::max(std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]), 1e-6f); };
    const float rs=len(r), us=len(u), fs=len(f);
    const float zs=(z[0]*f[0]+z[1]*f[1]+z[2]*f[2])/(fs*fs);
    const float bias=m[14]-zs*m[15];
    const float t[3]={m[12]/rs,m[13]/us,m[15]/fs};
    const bool native=h.params[2]>0.5f;
    // Keep the headset frustum, but retain the game's relative camcorder zoom.
    // Previously replacing rs/us outright made the flat camera zoom correctly
    // while the headset image remained at one fixed magnification.
    const float zoom=native?RelativeZoom(rs,h.eyeOffset[3]):1.0f;
    const float sx=native?h.projection[0]*zoom:rs*h.params[0];
    const float sy=native?h.projection[1]*zoom:us*h.params[0];
    const float ox=native?h.projection[2]:0.0f, oy=native?h.projection[3]:0.0f;
    float nt[3]{};
    for (int i=0;i<3;++i) {
        r[i]/=rs; u[i]/=us; f[i]/=fs;
        nt[0]+=t[i]*h.right[i]; nt[1]+=t[i]*h.up[i]; nt[2]+=t[i]*h.forward[i];
    }
    if (native) for(int i=0;i<3;++i) nt[i]-=h.eyeOffset[i];
    for (int i=0;i<3;++i) {
        const float nr=r[i]*h.right[0]+u[i]*h.right[1]+f[i]*h.right[2];
        const float nu=r[i]*h.up[0]+u[i]*h.up[1]+f[i]*h.up[2];
        const float nf=r[i]*h.forward[0]+u[i]*h.forward[1]+f[i]*h.forward[2];
        m[i*4]=nr*sx+nf*fs*ox; m[i*4+1]=nu*sy+nf*fs*oy;
        m[i*4+3]=nf*fs; m[i*4+2]=nf*fs*zs;
    }
    m[12]=nt[0]*sx+nt[2]*fs*ox; m[13]=nt[1]*sy+nt[2]*fs*oy;
    m[15]=nt[2]*fs; m[14]=bias+zs*m[15];
}

inline void PatchCameraBytes(void* bytes, size_t count, const HeadConstants& h) {
    if (!bytes || count<128) return;
    if(h.right[3]>0.5f){
        for(size_t offset:{size_t(0),size_t(64)}){
            float m[16];std::memcpy(m,static_cast<char*>(bytes)+offset,64);
            float r[3]={m[0],m[4],m[8]},u[3]={m[1],m[5],m[9]},f[3]={m[3],m[7],m[11]};
            auto len=[](float* v){return std::max(std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]),1e-6f);};
            const float rs=len(r),us=len(u),fs=len(f),zs=(m[2]*f[0]+m[6]*f[1]+m[10]*f[2])/(fs*fs),bias=m[14]-zs*m[15];
            const float t[3]={m[12]/rs,m[13]/us,m[15]/fs};float origin[3];
            for(int i=0;i<3;++i){r[i]/=rs;u[i]/=us;f[i]/=fs;origin[i]=-r[i]*t[0]-u[i]*t[1]-f[i]*t[2];}
            float flat=std::sqrt(f[0]*f[0]+f[1]*f[1]);
            if(flat>0.001f){
                f[0]/=flat;f[1]/=flat;f[2]=0;
                // Preserve the source handedness instead of assuming world yaw sign.
                const float sign=(r[0]*f[1]-r[1]*f[0])>=0?1.0f:-1.0f;
                r[0]=sign*f[1];r[1]=-sign*f[0];r[2]=0;u[0]=u[1]=0;u[2]=1;
                float tr=0,tu=0,tf=0;
                for(int i=0;i<3;++i){tr-=origin[i]*r[i];tu-=origin[i]*u[i];tf-=origin[i]*f[i];m[4*i]=r[i]*rs;m[4*i+1]=u[i]*us;m[4*i+3]=f[i]*fs;m[4*i+2]=f[i]*fs*zs;}
                m[12]=tr*rs;m[13]=tu*us;m[15]=tf*fs;m[14]=bias+zs*m[15];
                std::memcpy(static_cast<char*>(bytes)+offset,m,64);
            }
        }
    }
    if(count>=160&&h.params[2]>0.5f&&h.params[3]>0.5f){
        float m[40];std::memcpy(m,bytes,sizeof(m));
        float r[3]={m[0],m[4],m[8]},u[3]={m[1],m[5],m[9]},f[3]={m[3],m[7],m[11]};
        auto normalize=[](float* v){float n=std::max(std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]),1e-6f);for(int i=0;i<3;++i)v[i]/=n;};
        normalize(r);normalize(u);normalize(f);
        float local[3]{};
        for(int i=0;i<3;++i)local[i]=h.right[i]*h.eyeOffset[0]+h.up[i]*h.eyeOffset[1]+h.forward[i]*h.eyeOffset[2];
        for(int i=0;i<3;++i)m[32+i]+=r[i]*local[0]+u[i]*local[1]+f[i]*local[2];
        std::memcpy(static_cast<char*>(bytes)+128,m+32,12);
        // PreViewTranslation stays at the engine origin; object transforms still use it.
    }
    for (size_t offset : {size_t(0),size_t(64)}) {
        float m[16];
        std::memcpy(m,static_cast<char*>(bytes)+offset,sizeof(m));
        PatchMatrix(m,h);
        std::memcpy(static_cast<char*>(bytes)+offset,m,sizeof(m));
    }
}

inline constexpr const char* kPatchShader = R"HLSL(
RWByteAddressBuffer Data : register(u0);
cbuffer P12Head : register(b0) {
    float4 HeadRight; float4 HeadUp; float4 HeadForward; float4 Params;
    float4 Projection; float4 EyeOffset;
};
float L(uint o) { return asfloat(Data.Load(o)); }
void S(uint o, float v) { Data.Store(o, asuint(v)); }
void PatchMatrix(uint b) {
    float3 R=float3(L(b),L(b+16),L(b+32));
    float3 U=float3(L(b+4),L(b+20),L(b+36));
    float3 Z=float3(L(b+8),L(b+24),L(b+40));
    float3 F=float3(L(b+12),L(b+28),L(b+44));
    float rs=max(length(R),1e-6f), us=max(length(U),1e-6f), fs=max(length(F),1e-6f);
    float zScale=dot(Z,F)/max(dot(F,F),1e-6f);
    float clipBias=L(b+56)-zScale*L(b+60);
    float3 T=float3(L(b+48)/rs,L(b+52)/us,L(b+60)/fs);
    R/=rs; U/=us; F/=fs;
    float3 nR=R*HeadRight.x+U*HeadRight.y+F*HeadRight.z;
    float3 nU=R*HeadUp.x+U*HeadUp.y+F*HeadUp.z;
    float3 nF=R*HeadForward.x+U*HeadForward.y+F*HeadForward.z;
    float3 nT=float3(dot(T,HeadRight.xyz),dot(T,HeadUp.xyz),dot(T,HeadForward.xyz));
    bool native=Params.z>0.5f;
    float zoom=(native&&EyeOffset.w>=0.25f)?clamp(rs/EyeOffset.w,0.75f,6.0f):1.0f;
    float sx=native?Projection.x*zoom:rs*Params.x, sy=native?Projection.y*zoom:us*Params.x;
    float ox=native?Projection.z:0, oy=native?Projection.w:0;
    if(native) nT-=EyeOffset.xyz;
    float3 X=nR*sx+nF*fs*ox, Y=nU*sy+nF*fs*oy;
    S(b,X.x); S(b+16,X.y); S(b+32,X.z);
    S(b+4,Y.x); S(b+20,Y.y); S(b+36,Y.z);
    S(b+12,nF.x*fs); S(b+28,nF.y*fs); S(b+44,nF.z*fs);
    S(b+8,nF.x*fs*zScale); S(b+24,nF.y*fs*zScale); S(b+40,nF.z*fs*zScale);
    S(b+48,nT.x*sx+nT.z*fs*ox); S(b+52,nT.y*sy+nT.z*fs*oy);
    S(b+60,nT.z*fs); S(b+56,clipBias+zScale*nT.z*fs);
}
[numthreads(1,1,1)]
void CSMain(uint3 tid:SV_DispatchThreadID) {
    if(HeadRight.w>0.5f){
        for(uint b=0;b<=64;b+=64){
            float3 r=float3(L(b),L(b+16),L(b+32)),u=float3(L(b+4),L(b+20),L(b+36)),f=float3(L(b+12),L(b+28),L(b+44));
            float rs=max(length(r),1e-6f),us=max(length(u),1e-6f),fs=max(length(f),1e-6f);
            float zs=dot(float3(L(b+8),L(b+24),L(b+40)),f)/(fs*fs),bias=L(b+56)-zs*L(b+60);
            float3 t=float3(L(b+48)/rs,L(b+52)/us,L(b+60)/fs);r/=rs;u/=us;f/=fs;
            float3 origin=-r*t.x-u*t.y-f*t.z;float flat=length(f.xy);
            if(flat>0.001f){
                f=float3(f.xy/flat,0);float handed=(r.x*f.y-r.y*f.x)>=0?1:-1;
                r=float3(handed*f.y,-handed*f.x,0);u=float3(0,0,1);
                float3 nt=float3(-dot(origin,r),-dot(origin,u),-dot(origin,f));
                for(uint i=0;i<3;++i){S(b+16*i,r[i]*rs);S(b+16*i+4,u[i]*us);S(b+16*i+12,f[i]*fs);S(b+16*i+8,f[i]*fs*zs);}
                S(b+48,nt.x*rs);S(b+52,nt.y*us);S(b+60,nt.z*fs);S(b+56,bias+zs*nt.z*fs);
            }
        }
    }
    if(Params.z>0.5f&&Params.w>0.5f){
        float3 R=normalize(float3(L(0),L(16),L(32)));
        float3 U=normalize(float3(L(4),L(20),L(36)));
        float3 F=normalize(float3(L(12),L(28),L(44)));
        float3 local=HeadRight.xyz*EyeOffset.x+HeadUp.xyz*EyeOffset.y+HeadForward.xyz*EyeOffset.z;
        float3 delta=R*local.x+U*local.y+F*local.z;
        S(128,L(128)+delta.x);S(132,L(132)+delta.y);S(136,L(136)+delta.z);
    }
    PatchMatrix(0); PatchMatrix(64);
}
)HLSL";
}
