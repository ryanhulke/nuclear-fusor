#pragma once

#include "Render/Render.h"

struct FusorGeometry {
    float chamberRadiusMm = 25.0f;
    float chamberLengthMm = 150.0f;
    float kfAdapterLengthMm = 50.0f;
    float kfAdapterSmallRadiusMm = 20.0f;
    float kf16PortLengthMm = 45.0f;
    float kf16PortFadeLengthMm = 10.0f;
    float kf16PortRadiusMm = 8.0f;
    float shakerOuterRadiusMm = 15.0f;
    float shakerWireRadiusMm = 0.75f;
    int shakerWireTurns = 8;
};

const FusorGeometry& fusorGeometry();
Vec3 shakerWirePoint(float t);

GpuMesh buildShakerBallMesh();
GpuMesh buildCylinderMesh();
GpuLines buildCylinderLines();
GpuMesh buildHvFeedthroughConductorMesh();
GpuMesh buildHvFeedthroughExteriorMesh();
