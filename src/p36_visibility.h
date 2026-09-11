#pragma once

#include <d3d11.h>
#include <cstdint>
#include <cstring>

namespace p36visibility {

// UE3 consumes D3D11_QUERY_OCCLUSION as a 64-bit visible-sample count. A
// non-zero result keeps the primitive in the render list. Preserve successful
// query completion and only replace the hidden result; this keeps the engine's
// query lifetime/synchronization intact while avoiding stale CPU-view culling
// after the VR renderer changes the final gameplay view.
inline bool ForceOcclusionVisible(D3D11_QUERY type, void* data, UINT dataSize) {
    if (type == D3D11_QUERY_OCCLUSION_PREDICATE) {
        if (!data || dataSize < sizeof(BOOL)) return false;
        BOOL visible = FALSE;
        std::memcpy(&visible, data, sizeof(visible));
        if (visible) return false;
        visible = TRUE;
        std::memcpy(data, &visible, sizeof(visible));
        return true;
    }
    if (type != D3D11_QUERY_OCCLUSION || !data || dataSize < sizeof(std::uint64_t)) return false;
    std::uint64_t samples = 0;
    std::memcpy(&samples, data, sizeof(samples));
    if (samples != 0) return false;
    samples = 1;
    std::memcpy(data, &samples, sizeof(samples));
    return true;
}

} // namespace p36visibility
