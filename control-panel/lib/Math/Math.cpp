#include "Math/Math.h"

#include <cmath>

Vec3 operator+(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 operator-(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 operator*(float scalar, const Vec3& v) {
    return {scalar * v.x, scalar * v.y, scalar * v.z};
}

Vec3 operator*(const Vec3& v, float scalar) {
    return scalar * v;
}

Vec3 operator/(const Vec3& v, float scalar) {
    return {v.x / scalar, v.y / scalar, v.z / scalar};
}

float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float length(const Vec3& v) {
    return std::sqrt(dot(v, v));
}

Vec3 normalized(const Vec3& v) {
    const float len = length(v);
    if (len < 1.0e-8f) {
        return {1.0f, 0.0f, 0.0f};
    }
    return v / len;
}

Vec3 perpendicularUnit(const Vec3& tangent) {
    const Vec3 trial = std::abs(tangent.x) < 0.8f ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 1.0f, 0.0f};
    return normalized(cross(tangent, trial));
}

Mat4 identity() {
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.m[col * 4 + row] =
                a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                a.m[3 * 4 + row] * b.m[col * 4 + 3];
        }
    }
    return out;
}

Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
    Mat4 out{};
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return out;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint) {
    const Vec3 forward = normalized(target - eye);
    const Vec3 side = normalized(cross(forward, upHint));
    const Vec3 up = cross(side, forward);

    Mat4 out = identity();
    out.m[0] = side.x;
    out.m[4] = side.y;
    out.m[8] = side.z;
    out.m[1] = up.x;
    out.m[5] = up.y;
    out.m[9] = up.z;
    out.m[2] = -forward.x;
    out.m[6] = -forward.y;
    out.m[10] = -forward.z;
    out.m[12] = -dot(side, eye);
    out.m[13] = -dot(up, eye);
    out.m[14] = dot(forward, eye);
    return out;
}
