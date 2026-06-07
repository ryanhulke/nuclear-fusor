#pragma once

constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4 {
    float m[16] = {};
};

Vec3 operator+(const Vec3& a, const Vec3& b);
Vec3 operator-(const Vec3& a, const Vec3& b);
Vec3 operator*(float scalar, const Vec3& v);
Vec3 operator*(const Vec3& v, float scalar);
Vec3 operator/(const Vec3& v, float scalar);

float dot(const Vec3& a, const Vec3& b);
Vec3 cross(const Vec3& a, const Vec3& b);
float length(const Vec3& v);
Vec3 normalized(const Vec3& v);
Vec3 perpendicularUnit(const Vec3& tangent);

Mat4 identity();
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane);
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint);
