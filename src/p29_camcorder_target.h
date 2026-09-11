#pragma once
#include <cstdint>

namespace p29 {
struct TargetCandidate {
    uint32_t width{};
    uint32_t height{};
    uint32_t samples{};
    bool cameraRaised{};
    bool uiShader{};
    bool singleTarget{};
    bool backbuffer{};
};

inline int Score(const TargetCandidate& c) {
    if (!c.cameraRaised || !c.uiShader || !c.singleTarget || c.backbuffer) return -1000;
    if (c.samples != 1 || c.width < 128 || c.height < 64 || c.width > 4096 || c.height > 4096) return -1000;
    const double aspect = static_cast<double>(c.width) / static_cast<double>(c.height);
    if (aspect < 0.50 || aspect > 3.50) return -1000;
    int score = 10;
    if (c.width >= 256 && c.height >= 128) score += 4;
    if (aspect >= 1.0 && aspect <= 2.25) score += 3;
    if (c.width <= 2048 && c.height <= 2048) score += 2;
    return score;
}

inline bool Accept(const TargetCandidate& c) { return Score(c) >= 0; }

// Native-name/dimension match is evidence for a diagnostic candidate, not
// proof of GPU resource identity. Never use this alone to replace a texture.
inline bool MatchesNativeTarget(uint32_t width,uint32_t height,uint32_t samples,
                               uint32_t nativeWidth,uint32_t nativeHeight,
                               bool singleTarget,bool backbuffer){
    return singleTarget&&!backbuffer&&samples==1&&nativeWidth>=128&&nativeHeight>=64&&
        nativeWidth<=4096&&nativeHeight<=4096&&width==nativeWidth&&height==nativeHeight;
}
}
