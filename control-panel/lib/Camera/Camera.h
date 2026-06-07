#pragma once

#include "Math/Math.h"

struct OrbitCamera {
    float yaw = 0.68f;
    float pitch = 0.28f;
    float distance = 285.0f;
    Vec3 target{-45.0f, 0.0f, 0.0f};
    bool dragging = false;
    double lastX = 0.0;
    double lastY = 0.0;

    void reset();
    Vec3 position() const;
};
