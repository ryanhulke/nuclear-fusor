#pragma once

#include "Geometry/Geometry.h"
#include "Math/Math.h"

#include <glad/gl.h>

struct PlasmaParams {
    FusorGeometry geometry;
    float timeSeconds = 0.0f;
    float intensity = 3.0f; // Overall plasma glow brightness multiplier.
    float coreStrength = 1.75f;
    float hazeStrength = 1.35f;
    float sheathStrength = 0.72f;
    Vec3 coreColor{1.0f, 0.98f, 1.0f};
    Vec3 hazeColor{0.58f, 0.36f, 1.0f};
    Vec3 sheathColor{0.82f, 0.52f, 1.0f};
};

struct PlasmaRenderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

PlasmaParams makePlasmaParams(const FusorGeometry& geometry, float timeSeconds);

PlasmaRenderer createPlasmaRenderer(const FusorGeometry& geometry);
void destroyPlasmaRenderer(PlasmaRenderer& renderer);
void drawPlasma(PlasmaRenderer& renderer, const Mat4& mvp, const Vec3& cameraPos, const PlasmaParams& params);
