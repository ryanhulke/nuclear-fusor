#include "Geometry/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {

struct ProfilePoint {
    float x;
    float radius;
};

float mix(float a, float b, float t) {
    return a + (b - a) * t;
}

float smoothstep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void appendLine(std::vector<LineVertex>& lines, const Vec3& a, const Vec3& b, float alphaA = 1.0f, float alphaB = 1.0f) {
    lines.push_back({a, alphaA});
    lines.push_back({b, alphaB});
}

void appendRing(std::vector<LineVertex>& lines, float x, float radius, int radialSegments) {
    for (int i = 0; i < radialSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(radialSegments);
        const float b = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(radialSegments);
        appendLine(
            lines,
            {x, radius * std::cos(a), radius * std::sin(a)},
            {x, radius * std::cos(b), radius * std::sin(b)}
        );
    }
}

bool insideVerticalPortOpening(const Vec3& position, const FusorGeometry& geometry) {
    if (std::abs(position.z) < geometry.chamberRadiusMm * 0.55f) {
        return false;
    }

    return position.x * position.x + position.y * position.y <=
        geometry.kf16PortRadiusMm * geometry.kf16PortRadiusMm;
}

float verticalPortIntersectionZ(float sign, float y, const FusorGeometry& geometry) {
    const float zSquared = std::max(0.0f, geometry.chamberRadiusMm * geometry.chamberRadiusMm - y * y);
    return sign * std::sqrt(zSquared);
}

float verticalPortFadeAlpha(float sign, float z, const FusorGeometry& geometry) {
    const float fadeStartZ = sign * (geometry.chamberRadiusMm + geometry.kf16PortLengthMm - geometry.kf16PortFadeLengthMm);
    const float distancePastFadeStart = std::max(0.0f, sign * (z - fadeStartZ));
    return 1.0f - std::clamp(
        distancePastFadeStart / std::max(geometry.kf16PortFadeLengthMm, 0.001f),
        0.0f,
        1.0f
    );
}

void appendVerticalPortSurface(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const FusorGeometry& geometry,
    float sign,
    int lengthSegments,
    int radialSegments
) {
    const unsigned int base = static_cast<unsigned int>(vertices.size());
    const float endZ = sign * (geometry.chamberRadiusMm + geometry.kf16PortLengthMm);
    const float fadeStartZ = sign * (geometry.chamberRadiusMm + geometry.kf16PortLengthMm - geometry.kf16PortFadeLengthMm);

    for (int i = 0; i <= lengthSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(lengthSegments);
        for (int j = 0; j <= radialSegments; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const Vec3 normal{std::cos(theta), std::sin(theta), 0.0f};
            const float y = geometry.kf16PortRadiusMm * normal.y;
            const float startZ = verticalPortIntersectionZ(sign, y, geometry);
            const float z = startZ + (endZ - startZ) * t;
            const float alpha = verticalPortFadeAlpha(sign, z, geometry);
            vertices.push_back({
                {geometry.kf16PortRadiusMm * normal.x, y, z},
                normal,
                alpha,
            });
        }
    }

    const int stride = radialSegments + 1;
    for (int i = 0; i < lengthSegments; ++i) {
        for (int j = 0; j < radialSegments; ++j) {
            const unsigned int a = base + static_cast<unsigned int>(i * stride + j);
            const unsigned int b = base + static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int c = base + static_cast<unsigned int>((i + 1) * stride + j + 1);
            const unsigned int d = base + static_cast<unsigned int>(i * stride + j + 1);
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }
}

void appendVerticalRing(std::vector<LineVertex>& lines, float z, float radius, int radialSegments, float alpha = 1.0f) {
    for (int i = 0; i < radialSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(radialSegments);
        const float b = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(radialSegments);
        appendLine(
            lines,
            {radius * std::cos(a), radius * std::sin(a), z},
            {radius * std::cos(b), radius * std::sin(b), z},
            alpha,
            alpha
        );
    }
}

void appendVerticalPortLines(
    std::vector<LineVertex>& lines,
    const FusorGeometry& geometry,
    float sign,
    int radialSegments,
    int ringCount,
    int longLineCount
) {
    const float fadeStartZ = sign * (geometry.chamberRadiusMm + geometry.kf16PortLengthMm - geometry.kf16PortFadeLengthMm);
    const float endZ = sign * (geometry.chamberRadiusMm + geometry.kf16PortLengthMm);

    for (int i = 1; i < ringCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(ringCount - 1);
        const float startZ = sign * geometry.chamberRadiusMm;
        const float z = startZ + (endZ - startZ) * t;
        appendVerticalRing(lines, z, geometry.kf16PortRadiusMm, radialSegments, verticalPortFadeAlpha(sign, z, geometry));
    }

    for (int i = 0; i < longLineCount; ++i) {
        const float theta = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(longLineCount);
        const float x = geometry.kf16PortRadiusMm * std::cos(theta);
        const float y = geometry.kf16PortRadiusMm * std::sin(theta);
        const float startZ = verticalPortIntersectionZ(sign, y, geometry);
        appendLine(lines, {x, y, startZ}, {x, y, fadeStartZ});
        appendLine(lines, {x, y, fadeStartZ}, {x, y, endZ}, 1.0f, 0.0f);
    }

    for (int i = 0; i < radialSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(radialSegments);
        const float b = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(radialSegments);
        const float ay = geometry.kf16PortRadiusMm * std::sin(a);
        const float by = geometry.kf16PortRadiusMm * std::sin(b);
        appendLine(
            lines,
            {geometry.kf16PortRadiusMm * std::cos(a), ay, verticalPortIntersectionZ(sign, ay, geometry)},
            {geometry.kf16PortRadiusMm * std::cos(b), by, verticalPortIntersectionZ(sign, by, geometry)}
        );
    }
}

void appendLineSkippingOpenings(
    std::vector<LineVertex>& lines,
    const Vec3& a,
    const Vec3& b,
    const FusorGeometry& geometry,
    int segments
) {
    for (int i = 0; i < segments; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(segments);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
        const Vec3 p0 = a * (1.0f - t0) + b * t0;
        const Vec3 p1 = a * (1.0f - t1) + b * t1;
        const Vec3 midpoint = (p0 + p1) * 0.5f;
        if (!insideVerticalPortOpening(midpoint, geometry)) {
            appendLine(lines, p0, p1);
        }
    }
}

void appendRingSkippingOpenings(
    std::vector<LineVertex>& lines,
    const FusorGeometry& geometry,
    float x,
    float radius,
    int radialSegments
) {
    for (int i = 0; i < radialSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(radialSegments);
        const float b = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(radialSegments);
        const Vec3 p0{x, radius * std::cos(a), radius * std::sin(a)};
        const Vec3 p1{x, radius * std::cos(b), radius * std::sin(b)};
        const Vec3 midpoint = (p0 + p1) * 0.5f;
        if (!insideVerticalPortOpening(midpoint, geometry)) {
            appendLine(lines, p0, p1);
        }
    }
}

void appendXAxisRod(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    float startX,
    float endX,
    float radius,
    int radialSegments
) {
    const unsigned int base = static_cast<unsigned int>(vertices.size());

    for (float x : {startX, endX}) {
        for (int j = 0; j < radialSegments; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const Vec3 normal{0.0f, std::cos(theta), std::sin(theta)};
            vertices.push_back({
                {x, radius * normal.y, radius * normal.z},
                normal,
            });
        }
    }

    const unsigned int leftCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({{startX, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}});
    const unsigned int rightCenter = static_cast<unsigned int>(vertices.size());
    vertices.push_back({{endX, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}});

    for (int j = 0; j < radialSegments; ++j) {
        const int nextJ = (j + 1) % radialSegments;
        const unsigned int a = base + static_cast<unsigned int>(j);
        const unsigned int b = base + static_cast<unsigned int>(radialSegments + j);
        const unsigned int c = base + static_cast<unsigned int>(radialSegments + nextJ);
        const unsigned int d = base + static_cast<unsigned int>(nextJ);

        indices.insert(indices.end(), {a, b, c, a, c, d});
        indices.insert(indices.end(), {leftCenter, d, a});
        indices.insert(indices.end(), {rightCenter, b, c});
    }
}

void appendLatheProfile(
    std::vector<Vertex>& vertices,
    std::vector<unsigned int>& indices,
    const std::vector<ProfilePoint>& profile,
    int radialSegments
) {
    if (profile.size() < 2) {
        return;
    }

    const unsigned int base = static_cast<unsigned int>(vertices.size());

    for (std::size_t i = 0; i < profile.size(); ++i) {
        const ProfilePoint& point = profile[i];
        const ProfilePoint& previous = profile[i > 0 ? i - 1 : i];
        const ProfilePoint& next = profile[i + 1 < profile.size() ? i + 1 : i];
        const float dx = next.x - previous.x;
        const float dr = next.radius - previous.radius;

        for (int j = 0; j <= radialSegments; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            Vec3 normal = normalized({-dr, dx * c, dx * s});
            if (length(normal) < 1.0e-6f) {
                normal = {0.0f, c, s};
            }
            vertices.push_back({
                {point.x, point.radius * c, point.radius * s},
                normal,
            });
        }
    }

    const int stride = radialSegments + 1;
    for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
        for (int j = 0; j < radialSegments; ++j) {
            const unsigned int a = base + static_cast<unsigned int>(i * stride + j);
            const unsigned int b = base + static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int c = base + static_cast<unsigned int>((i + 1) * stride + j + 1);
            const unsigned int d = base + static_cast<unsigned int>(i * stride + j + 1);
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }
}

} // namespace

const FusorGeometry& fusorGeometry() {
    static const FusorGeometry geometry{};
    return geometry;
}

Vec3 shakerWirePoint(float t) {
    const FusorGeometry& geometry = fusorGeometry();
    const float centerRadius = geometry.shakerOuterRadiusMm - geometry.shakerWireRadiusMm;
    const float theta = kPi * t;
    const float spin = 2.0f * kPi * static_cast<float>(geometry.shakerWireTurns) * t;
    const float x = centerRadius * std::cos(theta);
    const float radial = centerRadius * std::sin(theta);
    return {
        x,
        radial * std::cos(spin),
        radial * std::sin(spin),
    };
}

GpuMesh buildShakerBallMesh() {
    const FusorGeometry& geometry = fusorGeometry();
    const int samples = geometry.shakerWireTurns * 112 + 1;
    constexpr int tubeSegments = 14;

    std::vector<Vec3> path(samples);
    for (int i = 0; i < samples; ++i) {
        path[i] = shakerWirePoint(static_cast<float>(i) / static_cast<float>(samples - 1));
    }

    std::vector<Vertex> vertices;
    vertices.resize(static_cast<std::size_t>(samples * tubeSegments + 2));

    for (int i = 0; i < samples; ++i) {
        const Vec3 previousPoint = path[std::max(0, i - 1)];
        const Vec3 currentPoint = path[i];
        const Vec3 nextPoint = path[std::min(samples - 1, i + 1)];
        const Vec3 tangent = normalized(nextPoint - previousPoint);

        Vec3 radial{0.0f, currentPoint.y, currentPoint.z};
        Vec3 normalA = radial - tangent * dot(radial, tangent);
        if (length(normalA) < 1.0e-6f) {
            normalA = perpendicularUnit(tangent);
        }
        normalA = normalized(normalA);
        Vec3 normalB = normalized(cross(tangent, normalA));

        for (int j = 0; j < tubeSegments; ++j) {
            const float phi = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(tubeSegments);
            const Vec3 tubeNormal = std::cos(phi) * normalA + std::sin(phi) * normalB;
            const int idx = i * tubeSegments + j;
            vertices[idx] = {
                currentPoint + geometry.shakerWireRadiusMm * tubeNormal,
                tubeNormal,
            };
        }
    }

    const int startCenterIndex = samples * tubeSegments;
    const int endCenterIndex = startCenterIndex + 1;
    vertices[startCenterIndex] = {path.front(), normalized(path.front() - path[1])};
    vertices[endCenterIndex] = {path.back(), normalized(path.back() - path[samples - 2])};

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<std::size_t>((samples - 1) * tubeSegments * 6 + tubeSegments * 6));

    for (int i = 0; i < samples - 1; ++i) {
        const int nextI = i + 1;
        for (int j = 0; j < tubeSegments; ++j) {
            const int nextJ = (j + 1) % tubeSegments;
            const unsigned int a = static_cast<unsigned int>(i * tubeSegments + j);
            const unsigned int b = static_cast<unsigned int>(nextI * tubeSegments + j);
            const unsigned int c = static_cast<unsigned int>(nextI * tubeSegments + nextJ);
            const unsigned int d = static_cast<unsigned int>(i * tubeSegments + nextJ);
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }

    for (int j = 0; j < tubeSegments; ++j) {
        const int nextJ = (j + 1) % tubeSegments;
        indices.insert(indices.end(), {
            static_cast<unsigned int>(startCenterIndex),
            static_cast<unsigned int>(nextJ),
            static_cast<unsigned int>(j),
        });

        const unsigned int a = static_cast<unsigned int>((samples - 1) * tubeSegments + j);
        const unsigned int b = static_cast<unsigned int>((samples - 1) * tubeSegments + nextJ);
        indices.insert(indices.end(), {
            static_cast<unsigned int>(endCenterIndex),
            a,
            b,
        });
    }

    const float chamberHalfLength = geometry.chamberLengthMm * 0.5f;
    const float feedthroughStartX = -chamberHalfLength - geometry.kfAdapterLengthMm;
    const float ballLeftTipX = path.back().x;
    appendXAxisRod(vertices, indices, feedthroughStartX, ballLeftTipX, geometry.shakerWireRadiusMm, tubeSegments);

    return uploadMesh(vertices, indices);
}

GpuMesh buildCylinderMesh() {
    const FusorGeometry& geometry = fusorGeometry();
    constexpr int radialSegments = 192;
    constexpr int lengthSegments = 192;
    constexpr int adapterLengthSegments = 24;
    constexpr int portLengthSegments = 16;
    const float halfLength = geometry.chamberLengthMm * 0.5f;
    const float adapterStartX = -halfLength - geometry.kfAdapterLengthMm;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>((lengthSegments + adapterLengthSegments + 2 + (portLengthSegments + 1) * 2) * (radialSegments + 1)));

    for (int i = 0; i <= adapterLengthSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(adapterLengthSegments);
        const float x = adapterStartX + geometry.kfAdapterLengthMm * t;
        const float radius = geometry.kfAdapterSmallRadiusMm + (geometry.chamberRadiusMm - geometry.kfAdapterSmallRadiusMm) * t;
        const float slope = (geometry.chamberRadiusMm - geometry.kfAdapterSmallRadiusMm) / geometry.kfAdapterLengthMm;
        for (int j = 0; j <= radialSegments; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const Vec3 normal = normalized({-slope, std::cos(theta), std::sin(theta)});
            vertices.push_back({
                {x, radius * std::cos(theta), radius * std::sin(theta)},
                normal,
            });
        }
    }

    for (int i = 0; i <= lengthSegments; ++i) {
        const float x = -halfLength + geometry.chamberLengthMm * static_cast<float>(i) / static_cast<float>(lengthSegments);
        for (int j = 0; j <= radialSegments; ++j) {
            const float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(radialSegments);
            const Vec3 normal{0.0f, std::cos(theta), std::sin(theta)};
            vertices.push_back({
                {x, geometry.chamberRadiusMm * normal.y, geometry.chamberRadiusMm * normal.z},
                normal,
            });
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(static_cast<std::size_t>((lengthSegments + adapterLengthSegments + 1) * radialSegments * 6));
    const int stride = radialSegments + 1;
    const int rowCount = adapterLengthSegments + lengthSegments + 2;
    for (int i = 0; i < rowCount - 1; ++i) {
        for (int j = 0; j < radialSegments; ++j) {
            const unsigned int a = static_cast<unsigned int>(i * stride + j);
            const unsigned int b = static_cast<unsigned int>((i + 1) * stride + j);
            const unsigned int c = static_cast<unsigned int>((i + 1) * stride + j + 1);
            const unsigned int d = static_cast<unsigned int>(i * stride + j + 1);
            const Vec3 center = (
                vertices[a].position +
                vertices[b].position +
                vertices[c].position +
                vertices[d].position
            ) * 0.25f;
            if (insideVerticalPortOpening(center, geometry)) {
                continue;
            }
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }

    appendVerticalPortSurface(
        vertices,
        indices,
        geometry,
        1.0f,
        portLengthSegments,
        radialSegments
    );
    appendVerticalPortSurface(
        vertices,
        indices,
        geometry,
        -1.0f,
        portLengthSegments,
        radialSegments
    );

    return uploadMesh(vertices, indices);
}

GpuLines buildCylinderLines() {
    const FusorGeometry& geometry = fusorGeometry();
    constexpr int radialSegments = 192;
    constexpr int ringCount = 9;
    constexpr int adapterRingCount = 5;
    constexpr int longLineCount = 24;
    const float halfLength = geometry.chamberLengthMm * 0.5f;
    const float adapterStartX = -halfLength - geometry.kfAdapterLengthMm;

    std::vector<LineVertex> lines;

    for (int i = 0; i < adapterRingCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(adapterRingCount - 1);
        const float x = adapterStartX + geometry.kfAdapterLengthMm * t;
        const float radius = geometry.kfAdapterSmallRadiusMm + (geometry.chamberRadiusMm - geometry.kfAdapterSmallRadiusMm) * t;
        if (i != adapterRingCount - 1) {
            appendRing(lines, x, radius, radialSegments);
        }
    }

    for (int i = 0; i < ringCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(ringCount - 1);
        appendRingSkippingOpenings(
            lines,
            geometry,
            -halfLength + t * geometry.chamberLengthMm,
            geometry.chamberRadiusMm,
            radialSegments
        );
    }

    for (int i = 0; i < longLineCount; ++i) {
        const float theta = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(longLineCount);
        const float adapterStartY = geometry.kfAdapterSmallRadiusMm * std::cos(theta);
        const float adapterStartZ = geometry.kfAdapterSmallRadiusMm * std::sin(theta);
        const float y = geometry.chamberRadiusMm * std::cos(theta);
        const float z = geometry.chamberRadiusMm * std::sin(theta);
        appendLine(lines, {adapterStartX, adapterStartY, adapterStartZ}, {-halfLength, y, z});
        appendLineSkippingOpenings(lines, {-halfLength, y, z}, {halfLength, y, z}, geometry, 192);
    }

    appendVerticalPortLines(lines, geometry, 1.0f, radialSegments, 10, longLineCount);
    appendVerticalPortLines(lines, geometry, -1.0f, radialSegments, 10, longLineCount);

    for (int i = 0; i < 16; ++i) {
        const float theta = 2.0f * kPi * static_cast<float>(i) / 16.0f;
        appendLine(lines, {halfLength, 0.0f, 0.0f}, {halfLength, geometry.chamberRadiusMm * std::cos(theta), geometry.chamberRadiusMm * std::sin(theta)});
    }
    appendRing(lines, halfLength, geometry.chamberRadiusMm * 0.5f, radialSegments / 2);

    return uploadLines(lines);
}

GpuMesh buildHvFeedthroughConductorMesh() {
    const FusorGeometry& geometry = fusorGeometry();
    constexpr int radialSegments = 64;
    const float chamberHalfLength = geometry.chamberLengthMm * 0.5f;
    const float adapterEndX = -chamberHalfLength - geometry.kfAdapterLengthMm;

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    appendLatheProfile(vertices, indices, {
        {adapterEndX - 92.0f, 0.0f},
        {adapterEndX - 92.0f, 1.15f},
        {adapterEndX - 81.5f, 1.15f},
        {adapterEndX - 80.8f, 1.8f},
        {adapterEndX - 77.5f, 2.9f},
        {adapterEndX - 73.0f, 4.1f},
        {adapterEndX - 68.0f, 4.1f},
        {adapterEndX - 64.2f, 5.8f},
        {adapterEndX - 61.8f, 5.8f},
        {adapterEndX - 61.8f, 1.55f},
        {adapterEndX, 1.55f},
        {adapterEndX, 0.0f},
    }, radialSegments);

    return uploadMesh(vertices, indices);
}

GpuMesh buildHvFeedthroughExteriorMesh() {
    const FusorGeometry& geometry = fusorGeometry();
    constexpr int radialSegments = 96;
    constexpr int profileSegments = 144;
    constexpr int ribCount = 6;
    const float chamberHalfLength = geometry.chamberLengthMm * 0.5f;
    const float adapterEndX = -chamberHalfLength - geometry.kfAdapterLengthMm;
    const float startX = adapterEndX - 67.0f;
    const float endX = adapterEndX - 19.0f;
    const float lengthMm = endX - startX;
    const float valleyRadius = 7.1f;
    const float ribRadius = 12.7f;

    std::vector<ProfilePoint> profile;
    profile.reserve(profileSegments + 1);

    for (int i = 0; i <= profileSegments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(profileSegments);
        const float x = startX + lengthMm * u;
        float radius = valleyRadius;

        for (int rib = 0; rib < ribCount; ++rib) {
            const float center = (static_cast<float>(rib) + 0.5f) / static_cast<float>(ribCount);
            const float normalizedDistance = (u - center) / 0.078f;
            const float lobe = std::exp(-normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance);
            radius = std::max(radius, valleyRadius + (ribRadius - valleyRadius) * lobe);
        }

        const float endTaper =
            smoothstep(0.0f, 0.08f, u) *
            (1.0f - smoothstep(0.92f, 1.0f, u));
        radius = mix(6.2f, radius, endTaper);
        profile.push_back({x, radius});
    }

    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    appendLatheProfile(vertices, indices, {
        {adapterEndX - 20.0f, 0.0f},
        {adapterEndX - 20.0f, 6.8f},
        {adapterEndX - 16.0f, 6.8f},
        {adapterEndX - 14.2f, 10.4f},
        {adapterEndX - 8.2f, 10.4f},
        {adapterEndX - 7.0f, 13.8f},
        {adapterEndX - 2.2f, 13.8f},
        {adapterEndX - 2.2f, 18.2f},
        {adapterEndX, 18.2f},
        {adapterEndX, geometry.kfAdapterSmallRadiusMm},
    }, radialSegments);

    appendLatheProfile(vertices, indices, profile, radialSegments);
    return uploadMesh(vertices, indices);
}
