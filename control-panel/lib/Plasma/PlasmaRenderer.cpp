#include "Plasma/PlasmaRenderer.h"

#include "Render/Render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

struct PlasmaVertex {
    Vec3 position;
    Vec3 local;
    Vec3 normal;
    float layer;
    float seed;
};

constexpr float kLayerCore = 0.0f;
constexpr float kLayerHaze = 1.0f;
constexpr float kLayerSheath = 2.0f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

float hash01(float value) {
    const float raw = std::sin(value * 12.9898f) * 43758.5453f;
    return raw - std::floor(raw);
}

unsigned int addVertex(std::vector<PlasmaVertex>& vertices, const PlasmaVertex& vertex) {
    vertices.push_back(vertex);
    return static_cast<unsigned int>(vertices.size() - 1);
}

void appendQuad(
    std::vector<unsigned int>& indices,
    unsigned int a,
    unsigned int b,
    unsigned int c,
    unsigned int d
) {
    indices.insert(indices.end(), {a, b, c, a, c, d});
}

void appendCoreShell(
    std::vector<PlasmaVertex>& vertices,
    std::vector<unsigned int>& indices,
    const FusorGeometry& geometry,
    float shell,
    float seed
) {
    constexpr int rings = 26;
    constexpr int segments = 56;
    const float xRadius = geometry.shakerOuterRadiusMm * 0.95f;
    const float radialRadius = geometry.shakerOuterRadiusMm * 0.74f;
    const unsigned int base = static_cast<unsigned int>(vertices.size());

    for (int i = 0; i <= rings; ++i) {
        const float polar = kPi * static_cast<float>(i) / static_cast<float>(rings);
        const float x = std::cos(polar);
        const float radial = std::sin(polar);

        for (int j = 0; j <= segments; ++j) {
            const float angle = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
            const Vec3 unit{x, radial * std::cos(angle), radial * std::sin(angle)};
            const Vec3 position{
                unit.x * xRadius * shell,
                unit.y * radialRadius * shell,
                unit.z * radialRadius * shell,
            };
            addVertex(vertices, {position, unit * shell, unit, kLayerCore, seed});
        }
    }

    const unsigned int stride = segments + 1;
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const unsigned int a = base + static_cast<unsigned int>(i * stride + j);
            const unsigned int b = base + static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int c = base + static_cast<unsigned int>((i + 1) * stride + j + 1);
            const unsigned int d = base + static_cast<unsigned int>(i * stride + j + 1);
            appendQuad(indices, a, b, c, d);
        }
    }
}

void appendHazeShell(
    std::vector<PlasmaVertex>& vertices,
    std::vector<unsigned int>& indices,
    const FusorGeometry& geometry,
    float radiusFraction,
    float seed
) {
    constexpr int lengthSegments = 28;
    constexpr int radialSegments = 96;
    const float halfLength = geometry.chamberLengthMm * 0.47f;
    const float radius = geometry.chamberRadiusMm * radiusFraction;
    const unsigned int base = static_cast<unsigned int>(vertices.size());

    for (int i = 0; i <= lengthSegments; ++i) {
        const float xNorm = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(lengthSegments);
        const float x = xNorm * halfLength;

        for (int j = 0; j <= radialSegments; ++j) {
            const float angle = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const Vec3 normal{0.0f, std::cos(angle), std::sin(angle)};
            const Vec3 position{x, radius * normal.y, radius * normal.z};
            addVertex(vertices, {position, {xNorm, radiusFraction, angle / (2.0f * kPi)}, normal, kLayerHaze, seed});
        }
    }

    const unsigned int stride = radialSegments + 1;
    for (int i = 0; i < lengthSegments; ++i) {
        for (int j = 0; j < radialSegments; ++j) {
            const unsigned int a = base + static_cast<unsigned int>(i * stride + j);
            const unsigned int b = base + static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int c = base + static_cast<unsigned int>((i + 1) * stride + j + 1);
            const unsigned int d = base + static_cast<unsigned int>(i * stride + j + 1);
            appendQuad(indices, a, b, c, d);
        }
    }
}

void appendWireSheath(
    std::vector<PlasmaVertex>& vertices,
    std::vector<unsigned int>& indices,
    const FusorGeometry& geometry
) {
    const int samples = geometry.shakerWireTurns * 96 + 1;
    constexpr int tubeSegments = 18;
    constexpr float sheathRadiiMm[] = {2.0f, 3.4f, 5.2f};

    for (int shellIndex = 0; shellIndex < 3; ++shellIndex) {
        const float radius = sheathRadiiMm[shellIndex];
        const float shell = radius / sheathRadiiMm[2];
        const unsigned int base = static_cast<unsigned int>(vertices.size());

        for (int i = 0; i < samples; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
            const Vec3 previousPoint = shakerWirePoint(clamp01(t - 1.0f / static_cast<float>(samples - 1)));
            const Vec3 currentPoint = shakerWirePoint(t);
            const Vec3 nextPoint = shakerWirePoint(clamp01(t + 1.0f / static_cast<float>(samples - 1)));
            const Vec3 tangent = normalized(nextPoint - previousPoint);

            Vec3 normalA{0.0f, currentPoint.y, currentPoint.z};
            normalA = normalA - tangent * dot(normalA, tangent);
            if (length(normalA) < 1.0e-6f) {
                normalA = perpendicularUnit(tangent);
            }
            normalA = normalized(normalA);
            const Vec3 normalB = normalized(cross(tangent, normalA));

            for (int j = 0; j <= tubeSegments; ++j) {
                const float angle = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(tubeSegments);
                const Vec3 normal = std::cos(angle) * normalA + std::sin(angle) * normalB;
                const Vec3 offset = normal * radius;
                addVertex(vertices, {currentPoint + offset, {t, shell, angle / (2.0f * kPi)}, normal, kLayerSheath, shell});
            }
        }

        const unsigned int stride = tubeSegments + 1;
        for (int i = 0; i < samples - 1; ++i) {
            for (int j = 0; j < tubeSegments; ++j) {
                const unsigned int a = base + static_cast<unsigned int>(i * stride + j);
                const unsigned int b = base + static_cast<unsigned int>((i + 1) * stride + j);
                const unsigned int c = base + static_cast<unsigned int>((i + 1) * stride + j + 1);
                const unsigned int d = base + static_cast<unsigned int>(i * stride + j + 1);
                appendQuad(indices, a, b, c, d);
            }
        }
    }
}

void buildPlasmaMesh(
    const FusorGeometry& geometry,
    std::vector<PlasmaVertex>& vertices,
    std::vector<unsigned int>& indices
) {
    vertices.reserve(190000);
    indices.reserve(1050000);

    constexpr int coreShells = 26;
    for (int i = 0; i < coreShells; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(coreShells);
        const float shell = 0.04f + 0.96f * std::pow(t, 1.08f);
        appendCoreShell(vertices, indices, geometry, shell, static_cast<float>(i) * 0.13f);
    }

    constexpr int hazeShells = 32;
    for (int i = 0; i < hazeShells; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(hazeShells);
        const float jitter = (hash01(static_cast<float>(i) + 91.0f) - 0.5f) * 0.010f;
        const float radiusFraction = clamp01(0.06f + 0.92f * std::pow(t, 0.92f) + jitter);
        appendHazeShell(vertices, indices, geometry, radiusFraction, static_cast<float>(i) * 0.19f + 0.4f);
    }

    appendWireSheath(vertices, indices, geometry);
}

const char* plasmaVertexShaderSource() {
    return R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aLocal;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in float aLayer;
layout (location = 4) in float aSeed;

uniform mat4 uMVP;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vLocal;
out vec3 vNormal;
out float vLayer;
out float vSeed;

void main() {
    vec3 pos = aPos;
    if (aLayer < 0.5) {
        float coreBreath = 1.0 + 0.045 * sin(uTime * 0.74 + aSeed * 19.0);
        pos *= coreBreath;
    } else if (aLayer < 1.5) {
        float radialDrift = 0.72 * sin(uTime * 0.28 + aLocal.x * 2.3 + aSeed * 17.0);
        float axialDrift = 0.46 * sin(uTime * 0.22 + aLocal.y * 5.1 + aSeed * 13.0);
        pos += aNormal * radialDrift;
        pos.x += axialDrift;
    } else {
        float sheathFlutter =
            0.24 * sin(uTime * 1.18 + aLocal.x * 38.0 + aSeed * 7.0) +
            0.11 * sin(uTime * 1.92 + aLocal.z * 6.28318 + aLocal.x * 17.0);
        pos += aNormal * sheathFlutter;
    }

    vWorldPos = pos;
    vLocal = aLocal;
    vNormal = normalize(aNormal);
    vLayer = aLayer;
    vSeed = aSeed;
    gl_Position = uMVP * vec4(pos, 1.0);
}
)GLSL";
}

const char* plasmaFragmentShaderSource() {
    return R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vLocal;
in vec3 vNormal;
in float vLayer;
in float vSeed;

uniform float uTime;
uniform vec3 uCameraPos;
uniform float uIntensity;
uniform float uCoreStrength;
uniform float uHazeStrength;
uniform float uSheathStrength;
uniform float uChamberRadius;
uniform float uChamberHalfLength;
uniform vec3 uCoreColor;
uniform vec3 uHazeColor;
uniform vec3 uSheathColor;

out vec4 FragColor;

void main() {
    float chamberR = length(vWorldPos.yz) / uChamberRadius;
    float wallFade = 1.0 - smoothstep(0.84, 1.02, chamberR);
    float endFade = 1.0 - smoothstep(0.82, 1.02, abs(vWorldPos.x) / uChamberHalfLength);
    float corePulse =
        0.925 +
        0.070 * sin(uTime * 0.82 + vSeed * 37.0 + vWorldPos.x * 0.035) +
        0.030 * sin(uTime * 1.63 + vSeed * 11.0);
    float hazePulse =
        0.930 +
        0.055 * sin(uTime * 0.42 + vLocal.z * 18.0 + vSeed * 11.0) +
        0.026 * sin(uTime * 0.76 + vWorldPos.x * 0.050);
    float sheathPulse =
        0.890 +
        0.105 * sin(uTime * 1.28 + vLocal.x * 60.0 + vSeed * 9.0) +
        0.060 * sin(uTime * 2.28 + vLocal.z * 6.28318 + vLocal.x * 24.0);
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float facing = smoothstep(0.04, 0.72, abs(dot(normalize(vNormal), viewDir)));
    float softNoise =
        0.940 +
        0.045 * sin(vWorldPos.x * 0.075 + vWorldPos.y * 0.12 + vWorldPos.z * 0.10 + vSeed * 23.0) +
        0.020 * sin(uTime * 0.58 + vWorldPos.y * 0.17 - vWorldPos.z * 0.11);

    vec3 color = uHazeColor;
    float alpha = 0.0;

    if (vLayer < 0.5) {
        float d = clamp(length(vLocal), 0.0, 1.0);
        float halo = exp(-d * d * 1.35);
        float inner = exp(-d * d * 6.25);
        float hot = exp(-d * d * 18.0);
        float whiteBlend = smoothstep(0.18, 0.96, inner * 0.74 + hot * 0.50);
        color = mix(uHazeColor, uCoreColor, whiteBlend);
        alpha = (0.004 * halo + 0.014 * inner + 0.030 * hot) * uCoreStrength * uIntensity * corePulse * wallFade;
        alpha *= mix(0.12, 1.0, facing) * softNoise;
    } else if (vLayer < 1.5) {
        float r = clamp(vLocal.y, 0.0, 1.0);
        float x = abs(vLocal.x);
        float radialGlow = exp(-r * r * 1.06);
        float axialGlow = exp(-x * x * 1.95);
        float chamberFill = 0.30 + 0.70 * axialGlow;
        float hotCenter = smoothstep(0.48, 0.98, radialGlow * axialGlow);
        color = mix(vec3(0.18, 0.10, 0.50), uHazeColor, smoothstep(0.03, 1.0, radialGlow));
        color = mix(color, uCoreColor, hotCenter * 0.18);
        alpha = 0.0075 * (0.30 + 0.70 * radialGlow) * chamberFill * uHazeStrength * uIntensity * hazePulse * wallFade * endFade;
        alpha *= mix(0.06, 1.0, facing) * softNoise;
    } else {
        float t = clamp(vLocal.x, 0.0, 1.0);
        float tube = clamp(vLocal.y, 0.0, 1.0);
        float endTaper = smoothstep(0.015, 0.055, t) * (1.0 - smoothstep(0.945, 0.985, t));
        float tubeGlow = exp(-tube * tube * 2.15);
        color = mix(uSheathColor, uCoreColor, (1.0 - tube) * 0.28);
        alpha = 0.024 * tubeGlow * endTaper * uSheathStrength * uIntensity * sheathPulse * wallFade;
        alpha *= mix(0.16, 1.0, facing);
    }

    alpha = clamp(alpha, 0.0, 0.20);
    FragColor = vec4(color, alpha);
}
)GLSL";
}

} // namespace

PlasmaParams makePlasmaParams(const FusorGeometry& geometry, float timeSeconds) {
    PlasmaParams params{};
    params.geometry = geometry;
    params.timeSeconds = timeSeconds;
    return params;
}

PlasmaRenderer createPlasmaRenderer(const FusorGeometry& geometry) {
    PlasmaRenderer renderer{};
    renderer.program = createProgram(plasmaVertexShaderSource(), plasmaFragmentShaderSource());

    std::vector<PlasmaVertex> vertices;
    std::vector<unsigned int> indices;
    buildPlasmaMesh(geometry, vertices, indices);
    renderer.indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);
    glGenBuffers(1, &renderer.ebo);

    glBindVertexArray(renderer.vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(PlasmaVertex)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
        indices.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PlasmaVertex), reinterpret_cast<void*>(offsetof(PlasmaVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PlasmaVertex), reinterpret_cast<void*>(offsetof(PlasmaVertex, local)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(PlasmaVertex), reinterpret_cast<void*>(offsetof(PlasmaVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(PlasmaVertex), reinterpret_cast<void*>(offsetof(PlasmaVertex, layer)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(PlasmaVertex), reinterpret_cast<void*>(offsetof(PlasmaVertex, seed)));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
    return renderer;
}

void destroyPlasmaRenderer(PlasmaRenderer& renderer) {
    if (renderer.ebo) {
        glDeleteBuffers(1, &renderer.ebo);
    }
    if (renderer.vbo) {
        glDeleteBuffers(1, &renderer.vbo);
    }
    if (renderer.vao) {
        glDeleteVertexArrays(1, &renderer.vao);
    }
    if (renderer.program) {
        glDeleteProgram(renderer.program);
    }
    renderer = {};
}

void drawPlasma(PlasmaRenderer& renderer, const Mat4& mvp, const Vec3& cameraPos, const PlasmaParams& params) {
    if (renderer.program == 0 || renderer.indexCount == 0) {
        return;
    }

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean previousDepthMask = GL_TRUE;
    GLint blendSrcRgb = GL_SRC_ALPHA;
    GLint blendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint blendSrcAlpha = GL_SRC_ALPHA;
    GLint blendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_CULL_FACE);

    glUseProgram(renderer.program);
    glUniformMatrix4fv(glGetUniformLocation(renderer.program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniform1f(glGetUniformLocation(renderer.program, "uTime"), params.timeSeconds);
    glUniform3f(glGetUniformLocation(renderer.program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform1f(glGetUniformLocation(renderer.program, "uIntensity"), params.intensity);
    glUniform1f(glGetUniformLocation(renderer.program, "uCoreStrength"), params.coreStrength);
    glUniform1f(glGetUniformLocation(renderer.program, "uHazeStrength"), params.hazeStrength);
    glUniform1f(glGetUniformLocation(renderer.program, "uSheathStrength"), params.sheathStrength);
    glUniform1f(glGetUniformLocation(renderer.program, "uChamberRadius"), params.geometry.chamberRadiusMm);
    glUniform1f(glGetUniformLocation(renderer.program, "uChamberHalfLength"), params.geometry.chamberLengthMm * 0.5f);
    glUniform3f(glGetUniformLocation(renderer.program, "uCoreColor"), params.coreColor.x, params.coreColor.y, params.coreColor.z);
    glUniform3f(glGetUniformLocation(renderer.program, "uHazeColor"), params.hazeColor.x, params.hazeColor.y, params.hazeColor.z);
    glUniform3f(glGetUniformLocation(renderer.program, "uSheathColor"), params.sheathColor.x, params.sheathColor.y, params.sheathColor.z);

    glBindVertexArray(renderer.vao);
    glDrawElements(GL_TRIANGLES, renderer.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha);
    glDepthMask(previousDepthMask);
    if (!depthEnabled) {
        glDisable(GL_DEPTH_TEST);
    }
    if (!blendEnabled) {
        glDisable(GL_BLEND);
    }
    if (cullEnabled) {
        glEnable(GL_CULL_FACE);
    }
}
