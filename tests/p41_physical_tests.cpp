#include "p41_physical.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

static void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void Near(float a, float b, const char* message) {
    if (!std::isfinite(a) || std::fabs(a - b) > 0.001f) throw std::runtime_error(message);
}

int main() try {
    const p20::V head{0.0f, 1.65f, 0.0f};
    Check(p41::ReachGesture(head, {0.25f, 1.35f, -0.45f}, true, 100.0f),
        "forward left-hand reach accepted");
    Check(!p41::ReachGesture(head, {0.05f, 1.60f, -0.04f}, true, 100.0f),
        "hand at face rejected");
    Check(!p41::ReachGesture(head, {0.25f, 1.35f, 0.45f}, true, 100.0f),
        "hand behind head rejected");
    Check(!p41::ReachGesture(head, {0.25f, 1.35f, -0.45f}, false, 100.0f),
        "untracked hand rejected");

    Near(p41::ClearFraction([](float f) { return f <= 0.60f; }, 8, 0.0f), 0.59765625f,
        "binary obstruction boundary");
    Near(p41::ClearFraction([](float) { return true; }), 1.0f, "clear trace unchanged");
    Near(p41::ClearFraction([](float) { return false; }), 0.0f, "blocked origin safe");
    Near(p41::SmoothedFraction(1.0f, 0.3f), 0.3f, "wall contact immediate");
    Near(p41::SmoothedFraction(0.3f, 1.0f), 0.46f, "wall release softened");

    const p20::V start{0, 0, 0}, end{0, 0, 100};
    auto outside = p41::KeepOutsideCapsule({2, 0, 50}, start, end, 15.0f, {1, 0, 0});
    Near(p20::Length(outside - p20::V{0, 0, 50}), 15.0f, "torso penetration pushed out");
    auto unchanged = p41::KeepOutsideCapsule({30, 0, 50}, start, end, 15.0f, {1, 0, 0});
    Near(p20::Length(unchanged - p20::V{30, 0, 50}), 0.0f, "clear hand unchanged");

    std::puts("PASS MC6A reach gate, wall collision, smoothing, and body capsule limits; hand-to-hand separation is absent.");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL %s\n", error.what());
    return 1;
}
