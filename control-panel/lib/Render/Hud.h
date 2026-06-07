#pragma once

#include <glad/gl.h>

#include <string>

struct HudRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool contains(float px, float py) const {
        return width > 0.0f && height > 0.0f && px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

struct HudMetricData {
    std::string label;
    std::string value;
    std::string unit;
    std::string status;
    bool hasReading = false;
    bool connected = false;
    bool live = false;
};

struct SensorHudData {
    HudMetricData pressure;
    HudMetricData voltage;
    HudMetricData current;
    bool cameraConnected = false;
    bool recording = false;
    bool loggingAvailable = false;
    float logSavedAlpha = 0.0f;
    float logSavedLift = 0.0f;
};

struct HudRenderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
};

struct SensorHudLayout {
    HudRect panel;
    HudRect startButton;
    HudRect stopButton;
};

enum class ControlHudMode {
    Idle,
    Evacuate,
    Manual,
    Fault,
};

struct ControlHudData {
    ControlHudMode mode = ControlHudMode::Idle;
    long virtualPositionSteps = 0;
    bool evacuationComplete = false;
    bool valveConnected = false;
    bool valveLive = false;
    bool evacuateAvailable = false;
    bool evacuateSelected = false;
    bool ventAvailable = false;
    bool ventOpen = false;
    bool ventBlockedByPower = false;
    bool ventMissingLimit = false;
    bool manualAvailable = false;
    bool manualSelected = false;
    std::string faultReason;
    std::string statusDetail;
};

struct ControlHudLayout {
    HudRect panel;
    HudRect evacuateButton;
    HudRect manualButton;
    HudRect ventButton;
    HudRect clearFaultButton;
};

struct PlaybackTimelineData {
    bool visible = false;
    bool paused = false;
    double elapsedSec = 0.0;
    double durationSec = 0.0;
    float alpha = 1.0f;
};

struct PlaybackTimelineLayout {
    HudRect panel;
    HudRect track;
};

HudRenderer createHudRenderer();
void destroyHudRenderer(HudRenderer& renderer);
SensorHudLayout drawSensorHud(HudRenderer& renderer, int viewportWidth, int viewportHeight, const SensorHudData& data);
ControlHudLayout drawControlHud(HudRenderer& renderer, int viewportWidth, int viewportHeight, const HudRect& sensorPanel, const ControlHudData& data);
PlaybackTimelineLayout drawPlaybackTimeline(HudRenderer& renderer, int viewportWidth, int viewportHeight, const PlaybackTimelineData& data);
