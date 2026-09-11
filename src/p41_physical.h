#pragma once
#include "p20_ik.h"
#include <algorithm>
#include <cmath>

namespace p41 {

inline float SafeFraction(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f;
}

// OpenXR tracking is stored as right/up/back in metres.  A physical Use press
// is accepted only while the left controller is a plausible arm's reach from
// the headset and is actually in front of it.  Outlast's own centre-screen
// trace remains the final authority over which object can be used.
inline bool ReachGesture(
    const p20::V& headMetres,
    const p20::V& handMetres,
    bool handValid,
    float unitsPerMetre,
    float minimumCentimetres = 25.0f,
    float maximumCentimetres = 115.0f,
    float minimumForwardCentimetres = 12.0f) {
    if (!handValid || !p20::Valid(headMetres) || !p20::Valid(handMetres) ||
        !std::isfinite(unitsPerMetre) || unitsPerMetre < 25.0f || unitsPerMetre > 250.0f)
        return false;
    const auto delta = (handMetres - headMetres) * unitsPerMetre;
    const float distance = p20::Length(delta);
    const float forward = -delta.z;
    return std::isfinite(distance) && distance >= minimumCentimetres &&
        distance <= maximumCentimetres && forward >= minimumForwardCentimetres;
}

template<class Clear>
inline float ClearFraction(Clear&& clear, int iterations = 5, float margin = 0.02f) {
    if (clear(1.0f)) return 1.0f;
    if (!clear(0.0f)) return 0.0f;
    float low = 0.0f, high = 1.0f;
    for (int i = 0; i < std::clamp(iterations, 1, 12); ++i) {
        const float middle = (low + high) * 0.5f;
        if (clear(middle)) low = middle;
        else high = middle;
    }
    return std::clamp(low - std::clamp(margin, 0.0f, 0.25f), 0.0f, 1.0f);
}

inline float SmoothedFraction(float previous, float measured) {
    previous = SafeFraction(previous);
    measured = SafeFraction(measured);
    // Obstructions take effect immediately.  Releasing from a wall is softened
    // to avoid a one-frame hand/head pop when a trace alternates at an edge.
    return measured < previous ? measured : std::min(measured, previous + 0.16f);
}

inline p20::V KeepOutsideCapsule(
    p20::V point,
    const p20::V& segmentStart,
    const p20::V& segmentEnd,
    float radius,
    p20::V fallbackDirection) {
    if (!p20::Valid(point) || !p20::Valid(segmentStart) || !p20::Valid(segmentEnd) ||
        !std::isfinite(radius) || radius <= 0.0f) return point;
    const auto axis = segmentEnd - segmentStart;
    const float axisLengthSquared = p20::Dot(axis, axis);
    const float along = axisLengthSquared > 1e-5f
        ? std::clamp(p20::Dot(point - segmentStart, axis) / axisLengthSquared, 0.0f, 1.0f)
        : 0.0f;
    const auto nearest = segmentStart + axis * along;
    auto outward = point - nearest;
    float distance = p20::Length(outward);
    if (distance >= radius) return point;
    if (distance < 1e-4f) {
        if (axisLengthSquared > 1e-5f)
            fallbackDirection = fallbackDirection - axis *
                (p20::Dot(fallbackDirection, axis) / axisLengthSquared);
        if (p20::Length(fallbackDirection) < 1e-4f) fallbackDirection = {1.0f, 0.0f, 0.0f};
        outward = p20::Unit(fallbackDirection);
    } else {
        outward = outward * (1.0f / distance);
    }
    return nearest + outward * radius;
}

} // namespace p41
