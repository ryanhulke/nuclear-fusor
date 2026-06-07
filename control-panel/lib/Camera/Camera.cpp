#include "Camera/Camera.h"

#include <cmath>

void OrbitCamera::reset() {
    yaw = 0.68f;
    pitch = 0.28f;
    distance = 285.0f;
    target = {-45.0f, 0.0f, 0.0f};
}

Vec3 OrbitCamera::position() const {
    const float cp = std::cos(pitch);
    return target + distance * Vec3{
        cp * std::cos(yaw),
        cp * std::sin(yaw),
        std::sin(pitch),
    };
}
