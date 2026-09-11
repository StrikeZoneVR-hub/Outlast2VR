#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace p27 {
// Exact retail x64 raise-camera call site. ESI is zeroed at EABDB7.
// Change only the immediate in lea edx,[rsi+1]: native mode 2 renders
// OLCamcorderHud to CameraHudPro-tex. No battery or camera-state writes.
inline constexpr std::size_t RaiseRva=0xeabdd1;
inline constexpr std::size_t ModeByte=9;
inline constexpr uint8_t RaiseBytes[]={
    0x48,0x8b,0x8b,0x80,0x66,0,0,0x8d,0x56,0x01,
    0x48,0x8b,0x89,0x40,0x0c,0,0,0xe8,0xd9,0x2e,0x06,0};
inline bool MatchesRaise(const void* bytes,std::size_t size){
    return bytes&&size>=sizeof(RaiseBytes)&&!std::memcmp(bytes,RaiseBytes,sizeof(RaiseBytes));
}
}
