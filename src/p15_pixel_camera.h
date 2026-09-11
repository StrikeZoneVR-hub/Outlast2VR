#pragma once
#include "p12_projection.h"
#include <vector>
namespace p15 {
inline bool SameMatrix(const float* a,const float* b){
    for(int i=0;i<16;++i)if(!std::isfinite(a[i])||!std::isfinite(b[i])||std::fabs(a[i]-b[i])>0.00002f*std::max(1.0f,std::fabs(b[i])))return false;
    return true;
}
inline bool Inverse(const float* m,float* out){
    double a[4][8]{};for(int r=0;r<4;++r)for(int c=0;c<4;++c){a[r][c]=m[4*r+c];a[r][4+c]=r==c?1:0;}
    for(int c=0;c<4;++c){
        int best=c;for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[best][c]))best=r;
        if(!std::isfinite(a[best][c])||std::fabs(a[best][c])<1e-10)return false;
        for(int j=0;j<8;++j)std::swap(a[c][j],a[best][j]);
        const double scale=a[c][c];for(int j=0;j<8;++j)a[c][j]/=scale;
        for(int r=0;r<4;++r)if(r!=c){const double v=a[r][c];for(int j=0;j<8;++j)a[r][j]-=v*a[c][j];}
    }
    for(int r=0;r<4;++r)for(int c=0;c<4;++c){out[4*r+c]=static_cast<float>(a[r][4+c]);if(!std::isfinite(out[4*r+c]))return false;}return true;
}
struct PixelCamera {
    float old[40]{},next[40]{},before[2][16]{},after[2][16]{};bool valid[2]{};
    float oldWorld[2][16]{},nextWorld[2][16]{},worldBefore[2][16]{},worldAfter[2][16]{};
    float oldPosition[3]{},nextPosition[3]{},oldView[16]{},nextView[16]{};
    bool worldValid[2]{};
    static void WorldMatrix(const float* m,const float* translation,float* out){
        std::memcpy(out,m,64);
        for(int j=0;j<4;++j){double v=m[12+j];for(int i=0;i<3;++i)v+=double(translation[i])*m[4*i+j];out[12+j]=static_cast<float>(v);}
    }
    static void ViewMatrix(const float* m,float* out){
        const float fs=std::max(std::sqrt(m[3]*m[3]+m[7]*m[7]+m[11]*m[11]),1e-6f);
        // Recover asymmetric projection offsets by projecting X/Y onto forward.
        const float ox=(m[0]*m[3]+m[4]*m[7]+m[8]*m[11])/(fs*fs);
        const float oy=(m[1]*m[3]+m[5]*m[7]+m[9]*m[11])/(fs*fs);
        float sx=0,sy=0;
        for(int i=0;i<3;++i){const float x=m[4*i]-ox*m[4*i+3],y=m[4*i+1]-oy*m[4*i+3];sx+=x*x;sy+=y*y;}
        sx=std::max(std::sqrt(sx),1e-6f);sy=std::max(std::sqrt(sy),1e-6f);
        for(int i=0;i<4;++i){out[4*i]=(m[4*i]-ox*m[4*i+3])/sx;out[4*i+1]=(m[4*i+1]-oy*m[4*i+3])/sy;out[4*i+2]=m[4*i+3]/fs;out[4*i+3]=i==3?1.0f:0.0f;}
    }
    PixelCamera(const float* camera,const p12::HeadConstants& head){
        std::memcpy(old,camera,160);std::memcpy(next,camera,160);p12::PatchCameraBytes(next,160,head);
        for(int i=0;i<2;++i)valid[i]=Inverse(old+16*i,before[i])&&Inverse(next+16*i,after[i]);
        for(int i=0;i<2;++i){
            WorldMatrix(old+16*i,old+36,oldWorld[i]);WorldMatrix(next+16*i,next+36,nextWorld[i]);
            worldValid[i]=Inverse(oldWorld[i],worldBefore[i])&&Inverse(nextWorld[i],worldAfter[i]);
        }
        for(int i=0;i<3;++i){oldPosition[i]=old[32+i]-old[36+i];nextPosition[i]=next[32+i]-next[36+i];}
        ViewMatrix(oldWorld[0],oldView);ViewMatrix(nextWorld[0],nextView);
    }
};
// Require numeric identity with the current validated camera, never buffer size alone.
inline bool PatchPixelCameraPrepared(std::vector<uint8_t>& bytes,const PixelCamera& camera){
    if(bytes.size()<64)return false;
    const float* oldCamera=camera.old;const float* next=camera.next;
    bool changed=false;
    if(bytes.size()==224){
        float matrix[16];std::memcpy(matrix,bytes.data()+144,64);
        int worldWhich=SameMatrix(matrix,camera.oldWorld[0])?0:SameMatrix(matrix,camera.oldWorld[1])?1:-1;
        if(worldWhich>=0){
            bool positionMatches=true;
            for(int i=0;i<3;++i){float v;std::memcpy(&v,bytes.data()+80+4*i,4);
                if(!std::isfinite(v)||std::fabs(v-camera.oldPosition[i])>0.02f)positionMatches=false;}
            if(positionMatches){
                std::memcpy(bytes.data()+144,camera.nextWorld[worldWhich],64);
                std::memcpy(bytes.data()+80,camera.nextPosition,12);changed=true;
                // Captured PSOffsetConstants stores reciprocal projection scales here.
                for(int j=0;j<2;++j){
                    float oldScale=0,newScale=0;
                    for(int i=0;i<3;++i){oldScale+=oldCamera[4*i+j]*oldCamera[4*i+j];
                        const float f=camera.nextView[4*i+2];
                        float offset=0;for(int k=0;k<3;++k)offset+=next[4*k+j]*camera.nextView[4*k+2];
                        const float v=next[4*i+j]-offset*f;newScale+=v*v;}
                    oldScale=1/std::sqrt(oldScale);newScale=1/std::sqrt(newScale);
                    float v;std::memcpy(&v,bytes.data()+128+4*j,4);
                    if(std::isfinite(v)&&std::fabs(v-oldScale)<0.0001f&&std::isfinite(newScale))std::memcpy(bytes.data()+128+4*j,&newScale,4);
                }
            }
        }
        int which=SameMatrix(matrix,oldCamera)?0:SameMatrix(matrix,oldCamera+16)?16:-1;
        if(which>=0&&!changed){
            // This layout is corroborated by the game's PSOffsetConstants metadata.
            for(int i=0;i<3;++i){float v;std::memcpy(&v,bytes.data()+80+4*i,4);
                if(!std::isfinite(v)||std::fabs(v-oldCamera[32+i])>0.001f)return false;
            }
            std::memcpy(bytes.data()+144,next+which,64);changed=true;
            std::memcpy(bytes.data()+80,next+32,12);
        }
    }
    for(int which:{0,1}){
        if(!camera.worldValid[which])continue;
        for(size_t o=0;o+64<=bytes.size();o+=16){float candidate[16];std::memcpy(candidate,bytes.data()+o,64);
            if(SameMatrix(candidate,camera.worldBefore[which])){std::memcpy(bytes.data()+o,camera.worldAfter[which],64);changed=true;o+=48;}}
    }
    // Exact inverse VP copies used by screen-to-world reconstruction are corrected.
    // Screen-scaled/packed variants that do not match are deliberately left alone.
    for(int which:{0,1}){
        if(!camera.valid[which])continue;
        const float* before=camera.before[which];const float* after=camera.after[which];
        for(size_t o=0;o+64<=bytes.size();o+=16){
            float candidate[16];std::memcpy(candidate,bytes.data()+o,64);
            if(SameMatrix(candidate,before)){std::memcpy(bytes.data()+o,after,64);changed=true;o+=48;}
        }
    }
    return changed;
}
inline bool PatchPixelCamera(std::vector<uint8_t>& bytes,const float* oldCamera,const p12::HeadConstants& head){
    if(head.params[2]<0.5f)return false;
    return PatchPixelCameraPrepared(bytes,PixelCamera(oldCamera,head));
}
}
