#pragma once
#include "p15_pixel_camera.h"
#include <cstdint>
namespace p17 {
enum Kind:uint32_t { Position=1,WorldToView=2,ViewToWorld=3 };
struct Field {uint32_t slot,offset,size,kind;uint32_t packing=0;}; // 1 rows, 2 columns
struct Layout {uint32_t count=0;Field fields[32]{};};
inline bool PatchField(std::vector<uint8_t>& bytes,const Field& field,const p15::PixelCamera& camera){
    if(field.offset>bytes.size()||field.size>bytes.size()-field.offset)return false;
    auto* p=bytes.data()+field.offset;
    if(field.kind==Position&&(field.size==12||field.size==16)){
        float v[3];std::memcpy(v,p,12);
        for(int i=0;i<3;++i)if(!std::isfinite(v[i])||std::fabs(v[i]-camera.oldPosition[i])>0.02f)return false;
        std::memcpy(p,camera.nextPosition,12);return true;
    }
    if((field.kind==WorldToView||field.kind==ViewToWorld)&&field.size==44){
        const bool inverse=field.kind==ViewToWorld;
        // Validate actual packing against the engine camera; preserve padding lanes.
        for(bool transposed:{false,true}){
            if((field.packing==1&&transposed)||(field.packing==2&&!transposed))continue;
            bool match=true;
            for(int r=0;r<3;++r)for(int c=0;c<3;++c){
                const int index=transposed?c*4+r:r*4+c;
                const int source=inverse?c*4+r:r*4+c;
                float v;std::memcpy(&v,p+index*4,4);
                if(!std::isfinite(v)||std::fabs(v-camera.oldView[source])>0.0002f)match=false;
            }
            if(match){for(int r=0;r<3;++r)for(int c=0;c<3;++c){const int index=transposed?c*4+r:r*4+c,source=inverse?c*4+r:r*4+c;
                std::memcpy(p+index*4,camera.nextView+source,4);}return true;}
        }
    }
    return false;
}
}
