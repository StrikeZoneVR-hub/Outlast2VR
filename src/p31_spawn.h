#pragma once
#include "p31_capture_math.h"
#include <cstring>
namespace p31 {
inline constexpr unsigned char SpawnPrologue[]={0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
inline constexpr unsigned char DestroyPrologue[]={0x48,0x8b,0xc4,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8b,0xec};
inline constexpr unsigned char SpawnCall[]={0xe8,0x1e,0x25,0xec,0xff};
inline constexpr unsigned char DestroyCall[]={0xe8,0x6d,0x31,0xec,0xff};
inline constexpr unsigned char WorldLoad[]={0x48,0x8b,0x0d,0xe5,0x53,0xaf,0x01};
inline bool SpawnSignatures(const void* spawn,const void* destroy,const void* spawnCall,const void* destroyCall,const void* worldLoad){
    return spawn&&destroy&&spawnCall&&destroyCall&&worldLoad&&
        !std::memcmp(spawn,SpawnPrologue,sizeof(SpawnPrologue))&&!std::memcmp(destroy,DestroyPrologue,sizeof(DestroyPrologue))&&
        !std::memcmp(spawnCall,SpawnCall,sizeof(SpawnCall))&&!std::memcmp(destroyCall,DestroyCall,sizeof(DestroyCall))&&
        !std::memcmp(worldLoad,WorldLoad,sizeof(WorldLoad));
}
// Argument order comes from AActor::execSpawn at RVA 6A6FA4..6A6FED.
// SceneCapture2DActor defaults to static/no-delete: native bNoFail must be true
// for runtime creation. Caller must make ONLY the new instance dynamic/deletable.
template<class Fn> void* SpawnLens(Fn fn,void* world,void* klass,void* owner,void* instigator,const p20::V& location,const Rotator& rotation){
    return fn(world,klass,uint64_t{0},&location,&rotation,nullptr,int32_t{1},int32_t{0},owner,instigator,int32_t{1});
}
inline uint32_t DynamicCaptureFlags(uint32_t flags){return flags&~uint32_t{5};}
inline constexpr unsigned char StaticReject[]={0xf6,0x80,0xf0,0,0,0,1};
inline constexpr unsigned char NoDeleteReject[]={0xf6,0x80,0xf0,0,0,0,4};
inline constexpr unsigned char ForceBranch[]={0x8b,0x85,0x87,0,0,0,0x85,0xc0,0x0f,0x85,0xd9,0,0,0};
inline constexpr unsigned char CleanupFlags[]={0x8b,0x87,0xf0,0,0,0,0xa8,1,0x75,0x71,0xa8,4,0x75,0x6d};
inline bool LifecycleSignatures(const void* stat,const void* noDelete,const void* force,const void* cleanup){
    return stat&&noDelete&&force&&cleanup&&!std::memcmp(stat,StaticReject,sizeof(StaticReject))&&
        !std::memcmp(noDelete,NoDeleteReject,sizeof(NoDeleteReject))&&!std::memcmp(force,ForceBranch,sizeof(ForceBranch))&&
        !std::memcmp(cleanup,CleanupFlags,sizeof(CleanupFlags));
}
}
