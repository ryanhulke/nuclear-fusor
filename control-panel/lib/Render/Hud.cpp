#include "Render/Hud.h"

#include "Render/Render.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Color {
    float r;
    float g;
    float b;
    float a;
};

struct HudVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

using Glyph = std::array<const char*, 7>;

const Glyph& glyphFor(char raw) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));

    static const Glyph blank = {
        "00000",
        "00000",
        "00000",
        "00000",
        "00000",
        "00000",
        "00000",
    };

    switch (c) {
    case '0': {
        static const Glyph glyph = {"11111", "10001", "10011", "10101", "11001", "10001", "11111"};
        return glyph;
    }
    case '1': {
        static const Glyph glyph = {"00100", "01100", "00100", "00100", "00100", "00100", "01110"};
        return glyph;
    }
    case '2': {
        static const Glyph glyph = {"11110", "00001", "00001", "11110", "10000", "10000", "11111"};
        return glyph;
    }
    case '3': {
        static const Glyph glyph = {"11110", "00001", "00001", "01110", "00001", "00001", "11110"};
        return glyph;
    }
    case '4': {
        static const Glyph glyph = {"10010", "10010", "10010", "11111", "00010", "00010", "00010"};
        return glyph;
    }
    case '5': {
        static const Glyph glyph = {"11111", "10000", "10000", "11110", "00001", "00001", "11110"};
        return glyph;
    }
    case '6': {
        static const Glyph glyph = {"01111", "10000", "10000", "11110", "10001", "10001", "01110"};
        return glyph;
    }
    case '7': {
        static const Glyph glyph = {"11111", "00001", "00010", "00100", "01000", "01000", "01000"};
        return glyph;
    }
    case '8': {
        static const Glyph glyph = {"01110", "10001", "10001", "01110", "10001", "10001", "01110"};
        return glyph;
    }
    case '9': {
        static const Glyph glyph = {"01110", "10001", "10001", "01111", "00001", "00001", "11110"};
        return glyph;
    }
    case 'A': {
        static const Glyph glyph = {"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
        return glyph;
    }
    case 'B': {
        static const Glyph glyph = {"11110", "10001", "10001", "11110", "10001", "10001", "11110"};
        return glyph;
    }
    case 'C': {
        static const Glyph glyph = {"01111", "10000", "10000", "10000", "10000", "10000", "01111"};
        return glyph;
    }
    case 'D': {
        static const Glyph glyph = {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
        return glyph;
    }
    case 'E': {
        static const Glyph glyph = {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
        return glyph;
    }
    case 'F': {
        static const Glyph glyph = {"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
        return glyph;
    }
    case 'G': {
        static const Glyph glyph = {"01111", "10000", "10000", "10011", "10001", "10001", "01111"};
        return glyph;
    }
    case 'H': {
        static const Glyph glyph = {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
        return glyph;
    }
    case 'I': {
        static const Glyph glyph = {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
        return glyph;
    }
    case 'K': {
        static const Glyph glyph = {"10001", "10010", "10100", "11000", "10100", "10010", "10001"};
        return glyph;
    }
    case 'L': {
        static const Glyph glyph = {"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
        return glyph;
    }
    case 'M': {
        static const Glyph glyph = {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
        return glyph;
    }
    case 'N': {
        static const Glyph glyph = {"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
        return glyph;
    }
    case 'O': {
        static const Glyph glyph = {"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
        return glyph;
    }
    case 'P': {
        static const Glyph glyph = {"11110", "10001", "10001", "11110", "10000", "10000", "10000"};
        return glyph;
    }
    case 'R': {
        static const Glyph glyph = {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
        return glyph;
    }
    case 'S': {
        static const Glyph glyph = {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
        return glyph;
    }
    case 'T': {
        static const Glyph glyph = {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
        return glyph;
    }
    case 'U': {
        static const Glyph glyph = {"10001", "10001", "10001", "10001", "10001", "10001", "01110"};
        return glyph;
    }
    case 'V': {
        static const Glyph glyph = {"10001", "10001", "10001", "10001", "10001", "01010", "00100"};
        return glyph;
    }
    case 'W': {
        static const Glyph glyph = {"10001", "10001", "10001", "10101", "10101", "10101", "01010"};
        return glyph;
    }
    case '.': {
        static const Glyph glyph = {"00000", "00000", "00000", "00000", "00000", "01100", "01100"};
        return glyph;
    }
    case '-': {
        static const Glyph glyph = {"00000", "00000", "00000", "11110", "00000", "00000", "00000"};
        return glyph;
    }
    case ':': {
        static const Glyph glyph = {"00000", "01100", "01100", "00000", "01100", "01100", "00000"};
        return glyph;
    }
    default:
        return blank;
    }
}

const char* hudVertexShaderSource() {
    return R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec4 aColor;

uniform vec2 uViewport;

out vec4 vColor;

void main() {
    vec2 ndc = vec2((aPos.x / uViewport.x) * 2.0 - 1.0, 1.0 - (aPos.y / uViewport.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";
}

const char* hudFragmentShaderSource() {
    return R"GLSL(
#version 330 core
in vec4 vColor;

out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)GLSL";
}

void addRect(std::vector<HudVertex>& vertices, float x, float y, float width, float height, Color color) {
    const HudVertex a{x, y, color.r, color.g, color.b, color.a};
    const HudVertex b{x + width, y, color.r, color.g, color.b, color.a};
    const HudVertex c{x + width, y + height, color.r, color.g, color.b, color.a};
    const HudVertex d{x, y + height, color.r, color.g, color.b, color.a};
    vertices.insert(vertices.end(), {a, b, c, a, c, d});
}

void addDisk(std::vector<HudVertex>& vertices, float cx, float cy, float radius, Color color) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kSegments = 28;
    const HudVertex center{cx, cy, color.r, color.g, color.b, color.a};

    for (int i = 0; i < kSegments; ++i) {
        const float a0 = (static_cast<float>(i) / static_cast<float>(kSegments)) * 2.0f * kPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(kSegments)) * 2.0f * kPi;
        const HudVertex p0{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color.r, color.g, color.b, color.a};
        const HudVertex p1{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color.r, color.g, color.b, color.a};
        vertices.insert(vertices.end(), {center, p0, p1});
    }
}

void addQuad(
    std::vector<HudVertex>& vertices,
    float ax,
    float ay,
    float bx,
    float by,
    float cx,
    float cy,
    float dx,
    float dy,
    Color color
) {
    const HudVertex a{ax, ay, color.r, color.g, color.b, color.a};
    const HudVertex b{bx, by, color.r, color.g, color.b, color.a};
    const HudVertex c{cx, cy, color.r, color.g, color.b, color.a};
    const HudVertex d{dx, dy, color.r, color.g, color.b, color.a};
    vertices.insert(vertices.end(), {a, b, c, a, c, d});
}

void addSegment(std::vector<HudVertex>& vertices, float x0, float y0, float x1, float y1, float thickness, Color color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
    const float ox = -dy / length * thickness * 0.5f;
    const float oy = dx / length * thickness * 0.5f;

    addQuad(vertices, x0 + ox, y0 + oy, x1 + ox, y1 + oy, x1 - ox, y1 - oy, x0 - ox, y0 - oy, color);
}

float textWidth(const std::string& text, float scale) {
    float width = 0.0f;
    for (char ch : text) {
        width += (ch == ' ') ? 3.0f * scale : 6.0f * scale;
    }
    return std::max(0.0f, width - scale);
}

std::string uppercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

void addText(std::vector<HudVertex>& vertices, const std::string& text, float x, float y, float scale, Color color) {
    float cursor = x;

    for (char ch : text) {
        if (ch == ' ') {
            cursor += 3.0f * scale;
            continue;
        }

        const Glyph& glyph = glyphFor(ch);
        for (std::size_t row = 0; row < glyph.size(); ++row) {
            for (std::size_t col = 0; col < 5; ++col) {
                if (glyph[row][col] == '1') {
                    addRect(
                        vertices,
                        cursor + static_cast<float>(col) * scale,
                        y + static_cast<float>(row) * scale,
                        scale * 0.82f,
                        scale * 0.82f,
                        color
                    );
                }
            }
        }

        cursor += 6.0f * scale;
    }
}

Color statusColor(const HudMetricData& metric) {
    if (metric.live) {
        return {0.26f, 0.92f, 0.72f, 1.0f};
    }
    if (metric.connected) {
        return {1.0f, 0.74f, 0.36f, 1.0f};
    }
    return {0.46f, 0.68f, 0.95f, 1.0f};
}

void uploadAndDraw(HudRenderer& renderer, int viewportWidth, int viewportHeight, const std::vector<HudVertex>& vertices) {
    if (renderer.program == 0 || vertices.empty() || viewportWidth <= 0 || viewportHeight <= 0) {
        return;
    }

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(renderer.program);
    glUniform2f(glGetUniformLocation(renderer.program, "uViewport"), static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));

    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(HudVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);

    glDepthMask(previousDepthMask);
    if (depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
    if (!blendEnabled) {
        glDisable(GL_BLEND);
    }
}

} // namespace

HudRenderer createHudRenderer() {
    HudRenderer renderer{};
    renderer.program = createProgram(hudVertexShaderSource(), hudFragmentShaderSource());

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);

    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HudVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(HudVertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    return renderer;
}

void destroyHudRenderer(HudRenderer& renderer) {
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

SensorHudLayout drawSensorHud(HudRenderer& renderer, int viewportWidth, int viewportHeight, const SensorHudData& data) {
    SensorHudLayout layout{};

    const float widthScale = static_cast<float>(viewportWidth) / 1280.0f;
    const float heightScale = static_cast<float>(viewportHeight) / 800.0f;
    const float viewportScale = std::min(widthScale, heightScale);
    const float uiScale = std::clamp(1.0f + (viewportScale - 1.0f) * 1.85f, 1.0f, 2.05f);

    const float panelHeight = 202.0f * uiScale;
    const float panelWidth = std::min(824.0f * uiScale, static_cast<float>(viewportWidth) - 32.0f);
    const float x = std::max(16.0f, static_cast<float>(viewportWidth) - panelWidth - 24.0f * uiScale);
    const float y = std::max(16.0f, static_cast<float>(viewportHeight) - panelHeight - 24.0f * uiScale);
    layout.panel = {x, y, panelWidth, panelHeight};

    const Color panel{0.035f, 0.044f, 0.057f, 0.80f};
    const Color shadow{0.0f, 0.0f, 0.0f, 0.22f};
    const Color border{0.58f, 0.76f, 0.96f, 0.17f};
    const Color divider{0.66f, 0.80f, 0.96f, 0.14f};
    const Color label{0.72f, 0.82f, 0.92f, 0.78f};
    const Color primary{0.94f, 0.97f, 1.0f, 0.96f};
    const Color muted{0.70f, 0.78f, 0.86f, 0.78f};
    const Color buttonActive{0.075f, 0.093f, 0.118f, 0.88f};
    const Color buttonInactive{0.055f, 0.065f, 0.082f, 0.44f};
    const Color buttonBorderActive{0.58f, 0.76f, 0.96f, 0.24f};
    const Color buttonBorderInactive{0.42f, 0.54f, 0.68f, 0.14f};
    const Color activeText{0.92f, 0.96f, 1.0f, 0.94f};
    const Color inactiveText{0.68f, 0.74f, 0.82f, 0.42f};
    const Color recordRedActive{1.0f, 0.18f, 0.15f, 0.96f};
    const Color recordRedInactive{0.78f, 0.15f, 0.14f, 0.34f};
    const Color cameraOnline{0.26f, 0.92f, 0.72f, 1.0f};
    const Color cameraOffline{1.0f, 0.32f, 0.24f, 0.95f};

    const bool anyLive = data.pressure.live || data.voltage.live || data.current.live;
    const Color hudAccent = anyLive ? Color{0.26f, 0.92f, 0.72f, 1.0f}
                                    : data.loggingAvailable ? Color{1.0f, 0.74f, 0.36f, 1.0f}
                                                            : Color{0.46f, 0.68f, 0.95f, 1.0f};

    std::vector<HudVertex> vertices;
    vertices.reserve(6800);

    addRect(vertices, x + 7.0f * uiScale, y + 9.0f * uiScale, panelWidth, panelHeight, shadow);
    addRect(vertices, x, y, panelWidth, panelHeight, panel);
    addRect(vertices, x, y, panelWidth, 1.0f, border);
    addRect(vertices, x, y + panelHeight - 1.0f, panelWidth, 1.0f, border);
    addRect(vertices, x, y, 1.0f, panelHeight, border);
    addRect(vertices, x + panelWidth - 1.0f, y, 1.0f, panelHeight, border);
    addRect(vertices, x, y + panelHeight - 4.0f * uiScale, panelWidth, 4.0f * uiScale, hudAccent);

    auto scaleToFit = [](const std::string& text, float desiredScale, float maxWidth, float minScale) {
        const float desiredWidth = textWidth(text, desiredScale);
        if (desiredWidth <= maxWidth || desiredWidth <= 0.0f) {
            return desiredScale;
        }
        return std::clamp(desiredScale * maxWidth / desiredWidth, minScale, desiredScale);
    };

    auto metricValueScale = [uiScale](const std::string& value, const std::string& unit, float unitScale, float maxWidth) {
        const float desiredScale = 6.9f * uiScale;
        const float minScale = 2.4f * uiScale;
        const float gap = 10.0f * uiScale;
        const float unitWidth = textWidth(unit, unitScale);
        const float desiredWidth = textWidth(value, desiredScale) + gap + unitWidth;
        if (desiredWidth <= maxWidth) {
            return desiredScale;
        }

        const float availableValueWidth = maxWidth - gap - unitWidth;
        const float desiredValueWidth = textWidth(value, desiredScale);
        if (availableValueWidth <= 0.0f || desiredValueWidth <= 0.0f) {
            return minScale;
        }
        return std::clamp(desiredScale * availableValueWidth / desiredValueWidth, minScale, desiredScale);
    };

    const std::string cameraText = data.cameraConnected ? "CAMERA CONNECTED" : "CAMERA DISCONNECTED";
    const float cameraScale = std::clamp(2.75f * uiScale, 2.75f, 4.15f);
    const float cameraHeight = 48.0f * uiScale;
    const float cameraWidth = textWidth(cameraText, cameraScale) + 66.0f * uiScale;
    const float cameraX = std::max(8.0f, static_cast<float>(viewportWidth) - cameraWidth - 20.0f * uiScale);
    const float cameraY = 20.0f * uiScale;
    const Color cameraPanel{0.035f, 0.044f, 0.057f, 0.62f};
    const Color cameraBorder{0.58f, 0.76f, 0.96f, 0.16f};
    const Color cameraTextColor{0.88f, 0.94f, 1.0f, 0.88f};
    const Color cameraAccent = data.cameraConnected ? cameraOnline : cameraOffline;
    addRect(vertices, cameraX + 4.0f * uiScale, cameraY + 5.0f * uiScale, cameraWidth, cameraHeight, shadow);
    addRect(vertices, cameraX, cameraY, cameraWidth, cameraHeight, cameraPanel);
    addRect(vertices, cameraX, cameraY, cameraWidth, 1.0f, cameraBorder);
    addRect(vertices, cameraX, cameraY + cameraHeight - 1.0f, cameraWidth, 1.0f, cameraBorder);
    addRect(vertices, cameraX, cameraY, 1.0f, cameraHeight, cameraBorder);
    addRect(vertices, cameraX + cameraWidth - 1.0f, cameraY, 1.0f, cameraHeight, cameraBorder);
    addDisk(vertices, cameraX + 25.0f * uiScale, cameraY + cameraHeight * 0.5f, 8.5f * uiScale, cameraAccent);
    addText(vertices, cameraText, cameraX + 48.0f * uiScale, cameraY + 14.0f * uiScale, cameraScale, cameraTextColor);

    const std::array<const HudMetricData*, 3> metrics{&data.pressure, &data.voltage, &data.current};
    const std::array<const char*, 3> fallbackUnits{"TORR", "KV", "MA"};
    const float innerX = x + 22.0f * uiScale;
    const float innerWidth = panelWidth - 44.0f * uiScale;
    const float sectionWidth = innerWidth / 3.0f;
    const float buttonY = y + 153.0f * uiScale;
    const float dividerTop = y + 22.0f * uiScale;
    const float dividerBottom = buttonY - 20.0f * uiScale;

    for (int i = 1; i < 3; ++i) {
        const float dividerX = innerX + sectionWidth * static_cast<float>(i);
        addRect(vertices, dividerX, dividerTop, 1.0f * uiScale, dividerBottom - dividerTop, divider);
    }

    for (int i = 0; i < 3; ++i) {
        const HudMetricData& metric = *metrics[static_cast<std::size_t>(i)];
        const float sectionX = innerX + sectionWidth * static_cast<float>(i);
        const float textX = sectionX + 7.0f * uiScale;
        const std::string value = metric.hasReading && !metric.value.empty() ? metric.value : "--";
        const std::string unit = metric.hasReading && !metric.unit.empty() ? metric.unit : fallbackUnits[static_cast<std::size_t>(i)];
        const std::string status = metric.status.empty() ? "WAITING" : uppercase(metric.status);
        const Color metricAccent = statusColor(metric);
        const Color valueColor = metric.hasReading ? primary : muted;

        addRect(vertices, textX, y + 27.0f * uiScale, 8.5f * uiScale, 8.5f * uiScale, metricAccent);
        const float labelMaxWidth = sectionWidth < 230.0f * uiScale ? sectionWidth * 0.36f : sectionWidth * 0.54f;
        const float labelScale = scaleToFit(metric.label, 2.45f * uiScale, labelMaxWidth, 1.35f * uiScale);
        addText(vertices, metric.label, textX + 18.0f * uiScale, y + 22.0f * uiScale, labelScale, label);

        const float statusMaxWidth = sectionWidth < 230.0f * uiScale ? sectionWidth * 0.40f : sectionWidth * 0.42f;
        const float statusScale = scaleToFit(status, 1.8f * uiScale, statusMaxWidth, 1.05f * uiScale);
        const float statusX = sectionX + sectionWidth - 9.0f * uiScale - textWidth(status, statusScale);
        addText(vertices, status, statusX, y + 25.0f * uiScale, statusScale, muted);

        const float unitScale = scaleToFit(unit, 2.85f * uiScale, sectionWidth * 0.34f, 1.7f * uiScale);
        const float valueScale = metricValueScale(value, unit, unitScale, sectionWidth - 18.0f * uiScale);
        const float valueY = y + 72.0f * uiScale;
        addText(vertices, value, textX, valueY, valueScale, valueColor);
        addText(
            vertices,
            unit,
            textX + textWidth(value, valueScale) + 10.0f * uiScale,
            valueY + 23.0f * uiScale,
            unitScale,
            metricAccent
        );
    }

    const float buttonHeight = 32.0f * uiScale;
    const float maxButtonAreaWidth = std::max(0.0f, panelWidth - 36.0f * uiScale);
    const float desiredButtonAreaWidth = std::clamp(panelWidth * 0.46f, 300.0f * uiScale, 420.0f * uiScale);
    const float buttonAreaWidth = std::min(desiredButtonAreaWidth, maxButtonAreaWidth);
    const float buttonX = x + (panelWidth - buttonAreaWidth) * 0.5f;
    const float startWidth = buttonAreaWidth * 0.5f;
    const float stopWidth = buttonAreaWidth - startWidth;

    layout.startButton = {buttonX, buttonY, startWidth, buttonHeight};
    layout.stopButton = {buttonX + startWidth, buttonY, stopWidth, buttonHeight};

    const bool loggingAvailable = data.loggingAvailable;
    const bool startActive = loggingAvailable && !data.recording;
    const bool stopActive = loggingAvailable && data.recording;
    const Color buttonBorder = loggingAvailable ? buttonBorderActive : buttonBorderInactive;
    addRect(vertices, buttonX, buttonY - 12.0f * uiScale, buttonAreaWidth, 1.0f, border);
    addRect(vertices, layout.startButton.x, layout.startButton.y, layout.startButton.width, layout.startButton.height, startActive ? buttonActive : buttonInactive);
    addRect(vertices, layout.stopButton.x, layout.stopButton.y, layout.stopButton.width, layout.stopButton.height, stopActive ? buttonActive : buttonInactive);
    addRect(vertices, buttonX, buttonY, buttonAreaWidth, 1.0f, buttonBorder);
    addRect(vertices, buttonX, buttonY + buttonHeight - 1.0f, buttonAreaWidth, 1.0f, buttonBorder);
    addRect(vertices, buttonX, buttonY, 1.0f, buttonHeight, buttonBorder);
    addRect(vertices, buttonX + buttonAreaWidth - 1.0f, buttonY, 1.0f, buttonHeight, buttonBorder);
    addRect(vertices, layout.stopButton.x, buttonY, 1.0f, buttonHeight, buttonBorder);

    const float buttonTextScale = 2.15f * uiScale;
    const float iconCenterY = buttonY + buttonHeight * 0.5f;
    const float iconXOffset = 18.0f * uiScale;
    addDisk(
        vertices,
        layout.startButton.x + iconXOffset,
        iconCenterY,
        4.5f * uiScale,
        startActive ? recordRedActive : recordRedInactive
    );
    addRect(
        vertices,
        layout.stopButton.x + iconXOffset - 4.3f * uiScale,
        iconCenterY - 4.3f * uiScale,
        8.6f * uiScale,
        8.6f * uiScale,
        stopActive ? recordRedActive : recordRedInactive
    );
    addText(
        vertices,
        "Start Rec",
        layout.startButton.x + 32.0f * uiScale,
        buttonY + 9.0f * uiScale,
        buttonTextScale,
        startActive ? activeText : inactiveText
    );
    addText(
        vertices,
        "Stop",
        layout.stopButton.x + 32.0f * uiScale,
        buttonY + 9.0f * uiScale,
        buttonTextScale,
        stopActive ? activeText : inactiveText
    );

    const float toastAlpha = std::clamp(data.logSavedAlpha, 0.0f, 1.0f);
    if (toastAlpha > 0.0f) {
        auto fade = [toastAlpha](Color color) {
            color.a *= toastAlpha;
            return color;
        };

        const float toastWidth = 178.0f * uiScale;
        const float toastHeight = 34.0f * uiScale;
        const float toastX = x + panelWidth - toastWidth;
        const float toastY = std::max(8.0f, y - toastHeight - 10.0f * uiScale - data.logSavedLift * uiScale);
        const Color toastPanel{0.035f, 0.049f, 0.052f, 0.88f};
        const Color toastBorder{0.34f, 0.92f, 0.68f, 0.32f};
        const Color toastText{0.90f, 0.98f, 0.94f, 0.95f};
        const Color checkGreen{0.30f, 1.0f, 0.62f, 0.96f};

        addRect(vertices, toastX + 5.0f * uiScale, toastY + 6.0f * uiScale, toastWidth, toastHeight, fade(shadow));
        addRect(vertices, toastX, toastY, toastWidth, toastHeight, fade(toastPanel));
        addRect(vertices, toastX, toastY, toastWidth, 1.0f, fade(toastBorder));
        addRect(vertices, toastX, toastY + toastHeight - 1.0f, toastWidth, 1.0f, fade(toastBorder));
        addRect(vertices, toastX, toastY, 1.0f, toastHeight, fade(toastBorder));
        addRect(vertices, toastX + toastWidth - 1.0f, toastY, 1.0f, toastHeight, fade(toastBorder));

        const float checkX = toastX + 22.0f * uiScale;
        const float checkY = toastY + 18.0f * uiScale;
        addSegment(
            vertices,
            checkX - 7.0f * uiScale,
            checkY - 1.0f * uiScale,
            checkX - 2.0f * uiScale,
            checkY + 5.0f * uiScale,
            3.2f * uiScale,
            fade(checkGreen)
        );
        addSegment(
            vertices,
            checkX - 2.0f * uiScale,
            checkY + 5.0f * uiScale,
            checkX + 8.0f * uiScale,
            checkY - 8.0f * uiScale,
            3.2f * uiScale,
            fade(checkGreen)
        );
        addText(vertices, "Log Saved", toastX + 42.0f * uiScale, toastY + 11.0f * uiScale, 2.25f * uiScale, fade(toastText));
    }

    uploadAndDraw(renderer, viewportWidth, viewportHeight, vertices);
    return layout;
}

namespace {

std::string formatLong(long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%ld", value);
    return buffer;
}

std::string formatClockTime(double seconds) {
    seconds = std::max(0.0, seconds);
    const int total = static_cast<int>(std::round(seconds));
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int secs = total % 60;

    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", minutes, secs);
    }
    return buffer;
}

float fitTextScale(const std::string& text, float desiredScale, float maxWidth, float minScale) {
    const float desiredWidth = textWidth(text, desiredScale);
    if (desiredWidth <= maxWidth || desiredWidth <= 0.0f) {
        return desiredScale;
    }
    return std::clamp(desiredScale * maxWidth / desiredWidth, minScale, desiredScale);
}

const char* controlHudModeName(ControlHudMode mode) {
    switch (mode) {
    case ControlHudMode::Idle: return "IDLE";
    case ControlHudMode::Evacuate: return "EVACUATE";
    case ControlHudMode::Manual: return "MANUAL";
    case ControlHudMode::Fault: return "FAULT";
    }
    return "IDLE";
}

void drawPanelChrome(
    std::vector<HudVertex>& vertices,
    float x,
    float y,
    float width,
    float height,
    float uiScale,
    Color accent
) {
    const Color panel{0.035f, 0.044f, 0.057f, 0.80f};
    const Color shadow{0.0f, 0.0f, 0.0f, 0.22f};
    const Color border{0.58f, 0.76f, 0.96f, 0.17f};

    addRect(vertices, x + 7.0f * uiScale, y + 9.0f * uiScale, width, height, shadow);
    addRect(vertices, x, y, width, height, panel);
    addRect(vertices, x, y, width, 1.0f, border);
    addRect(vertices, x, y + height - 1.0f, width, 1.0f, border);
    addRect(vertices, x, y, 1.0f, height, border);
    addRect(vertices, x + width - 1.0f, y, 1.0f, height, border);
    addRect(vertices, x, y + height - 4.0f * uiScale, width, 4.0f * uiScale, accent);
}

void drawModeButton(
    std::vector<HudVertex>& vertices,
    const HudRect& rect,
    const std::string& label,
    bool active,
    bool enabled,
    float uiScale
) {
    const Color buttonActive{0.085f, 0.132f, 0.125f, 0.94f};
    const Color buttonInactive{0.070f, 0.090f, 0.112f, 0.78f};
    const Color buttonDisabled{0.045f, 0.052f, 0.062f, 0.32f};
    const Color borderActiveColor{0.34f, 0.92f, 0.68f, 0.62f};
    const Color borderInactiveColor{0.58f, 0.76f, 0.96f, 0.40f};
    const Color borderDisabledColor{0.30f, 0.36f, 0.44f, 0.16f};
    const Color textActive{0.94f, 1.0f, 0.97f, 0.98f};
    const Color textInactive{0.88f, 0.94f, 1.0f, 0.92f};
    const Color textDisabled{0.48f, 0.52f, 0.58f, 0.48f};
    const Color accent{0.34f, 0.92f, 0.68f, 0.96f};

    const bool effectiveActive = active && enabled;
    const Color background = !enabled ? buttonDisabled : effectiveActive ? buttonActive : buttonInactive;
    const Color borderColor = !enabled ? borderDisabledColor : effectiveActive ? borderActiveColor : borderInactiveColor;
    const Color textColor = !enabled ? textDisabled : effectiveActive ? textActive : textInactive;

    addRect(vertices, rect.x, rect.y, rect.width, rect.height, background);
    addRect(vertices, rect.x, rect.y, rect.width, 1.0f, borderColor);
    addRect(vertices, rect.x, rect.y + rect.height - 1.0f, rect.width, 1.0f, borderColor);
    addRect(vertices, rect.x, rect.y, 1.0f, rect.height, borderColor);
    addRect(vertices, rect.x + rect.width - 1.0f, rect.y, 1.0f, rect.height, borderColor);

    if (effectiveActive) {
        addRect(vertices, rect.x, rect.y + rect.height - 3.0f * uiScale, rect.width, 3.0f * uiScale, accent);
    } else if (enabled) {
        addRect(vertices, rect.x, rect.y + rect.height - 2.0f * uiScale, rect.width, 2.0f * uiScale, {0.58f, 0.76f, 0.96f, 0.30f});
    }

    const float desiredScale = 2.05f * uiScale;
    const float minScale = 1.2f * uiScale;
    const float maxWidth = rect.width - 12.0f * uiScale;
    float scale = desiredScale;
    const float desiredWidth = textWidth(label, desiredScale);
    if (desiredWidth > maxWidth && desiredWidth > 0.0f) {
        scale = std::clamp(desiredScale * maxWidth / desiredWidth, minScale, desiredScale);
    }
    const float tw = textWidth(label, scale);
    const float tx = rect.x + (rect.width - tw) * 0.5f;
    const float th = 7.0f * scale;
    const float ty = rect.y + (rect.height - th) * 0.5f;
    addText(vertices, label, tx, ty, scale, textColor);
}

void drawReadoutLine(
    std::vector<HudVertex>& vertices,
    float x,
    float y,
    float labelScale,
    float valueScale,
    const std::string& label,
    const std::string& value,
    const std::string& unit,
    Color labelColor,
    Color valueColor,
    Color unitColor,
    float uiScale
) {
    const float labelY = y + (7.0f * valueScale - 7.0f * labelScale) * 0.5f;
    addText(vertices, label, x, labelY, labelScale, labelColor);
    const float labelW = textWidth(label, labelScale);
    addText(vertices, value, x + labelW + 10.0f * uiScale, y, valueScale, valueColor);
    if (!unit.empty()) {
        const float valueW = textWidth(value, valueScale);
        addText(vertices, unit, x + labelW + 10.0f * uiScale + valueW + 6.0f * uiScale, labelY + 2.0f * uiScale, labelScale, unitColor);
    }
}

} // namespace

ControlHudLayout drawControlHud(HudRenderer& renderer, int viewportWidth, int viewportHeight, const HudRect& sensorPanel, const ControlHudData& data) {
    ControlHudLayout layout{};

    if (sensorPanel.width <= 0.0f || sensorPanel.height <= 0.0f) {
        return layout;
    }

    const float widthScale = static_cast<float>(viewportWidth) / 1280.0f;
    const float heightScale = static_cast<float>(viewportHeight) / 800.0f;
    const float viewportScale = std::min(widthScale, heightScale);
    const float uiScale = std::clamp(1.0f + (viewportScale - 1.0f) * 1.85f, 1.0f, 2.05f);

    const float gap = 12.0f * uiScale;
    const float margin = 16.0f * uiScale;
    const float minPanelWidth = 280.0f * uiScale;
    const float desiredPanelWidth = 380.0f * uiScale;
    const float panelHeight = sensorPanel.height;
    const float leftSpace = sensorPanel.x - margin - gap;
    float panelWidth = std::min(desiredPanelWidth, std::max(minPanelWidth, leftSpace));
    float panelX = sensorPanel.x - gap - panelWidth;
    float panelY = sensorPanel.y;
    if (leftSpace < minPanelWidth) {
        panelWidth = std::min(sensorPanel.width, desiredPanelWidth);
        panelX = sensorPanel.x;
        panelY = sensorPanel.y - panelHeight - gap;
        if (panelY < margin) {
            panelY = margin;
        }
    }
    if (panelY + panelHeight > static_cast<float>(viewportHeight) - margin) {
        panelY = std::max(margin, static_cast<float>(viewportHeight) - margin - panelHeight);
    }

    layout.panel = {panelX, panelY, panelWidth, panelHeight};

    const Color label{0.72f, 0.82f, 0.92f, 0.78f};
    const Color primary{0.94f, 0.97f, 1.0f, 0.96f};
    const Color muted{0.70f, 0.78f, 0.86f, 0.78f};
    const Color unitColor{0.50f, 0.66f, 0.82f, 0.82f};
    const Color armedOff{0.34f, 0.92f, 0.68f, 0.96f};
    const Color faultColor{1.0f, 0.36f, 0.30f, 0.98f};
    const Color holdColor{0.34f, 0.92f, 0.68f, 0.96f};
    const Color idleColor{0.46f, 0.68f, 0.95f, 0.95f};

    Color accent;
    switch (data.mode) {
    case ControlHudMode::Fault:    accent = faultColor; break;
    case ControlHudMode::Manual:   accent = holdColor;  break;
    case ControlHudMode::Evacuate: accent = armedOff; break;
    default:                        accent = idleColor;  break;
    }

    std::vector<HudVertex> vertices;
    vertices.reserve(4400);

    drawPanelChrome(vertices, panelX, panelY, panelWidth, panelHeight, uiScale, accent);

    const float innerX = panelX + 16.0f * uiScale;
    const float innerWidth = panelWidth - 32.0f * uiScale;

    const float buttonGap = 6.0f * uiScale;
    const float buttonHeight = 36.0f * uiScale;
    const float buttonY = panelY + 16.0f * uiScale;
    const float buttonRowWidth = innerWidth;
    const float buttonWidth = (buttonRowWidth - buttonGap * 2.0f) / 3.0f;

    const bool inFault = data.mode == ControlHudMode::Fault;

    layout.evacuateButton = {innerX, buttonY, buttonWidth, buttonHeight};
    layout.manualButton = {innerX + buttonWidth + buttonGap, buttonY, buttonWidth, buttonHeight};
    layout.ventButton = {innerX + (buttonWidth + buttonGap) * 2.0f, buttonY, buttonWidth, buttonHeight};
    drawModeButton(vertices, layout.evacuateButton, "EVACUATE", data.evacuateSelected, !inFault && data.evacuateAvailable, uiScale);
    drawModeButton(vertices, layout.manualButton, "MANUAL", data.manualSelected, !inFault && data.manualAvailable, uiScale);

    const char* ventLabel = "VENT";
    if (data.valveLive || data.valveConnected) {
        if (data.ventOpen) {
            ventLabel = "VENTED";
        } else if (data.ventBlockedByPower) {
            ventLabel = "POWER";
        } else if (data.ventMissingLimit) {
            ventLabel = "NO LIMIT";
        }
    }
    drawModeButton(vertices, layout.ventButton, ventLabel, data.ventOpen, !inFault && data.ventAvailable && !data.ventOpen, uiScale);

    const float readoutTop = buttonY + buttonHeight + 17.0f * uiScale;
    const float labelScale = 1.6f * uiScale;
    const float valueScale = 2.4f * uiScale;
    const float lineHeight = 24.0f * uiScale;

    const std::string modeValue = controlHudModeName(data.mode);
    const std::string stepsValue = formatLong(data.virtualPositionSteps);

    drawReadoutLine(vertices, innerX, readoutTop + lineHeight * 0.0f, labelScale, valueScale, "MODE", modeValue, "", label, accent, unitColor, uiScale);

    const Color valveLive{0.34f, 0.92f, 0.68f, 0.96f};
    const Color valveConnectedColor{1.0f, 0.74f, 0.36f, 0.96f};
    const Color valveOffline{1.0f, 0.42f, 0.34f, 0.96f};
    const Color valveDotColor = data.valveLive ? valveLive : data.valveConnected ? valveConnectedColor : valveOffline;
    const Color valveValueColor = data.valveLive ? primary : muted;
    const std::string valveStepsValue = data.valveLive ? stepsValue : (data.valveConnected ? "--" : "OFFLINE");

    addDisk(vertices, innerX - 8.0f * uiScale, readoutTop + lineHeight + 7.0f * valueScale * 0.5f, 4.5f * uiScale, valveDotColor);
    drawReadoutLine(vertices, innerX, readoutTop + lineHeight, labelScale, valueScale, "VALVE", valveStepsValue, data.valveLive ? "STEPS" : "", label, valveValueColor, unitColor, uiScale);

    const float bottomButtonHeight = 28.0f * uiScale;
    const float bottomButtonY = panelY + panelHeight - bottomButtonHeight - 16.0f * uiScale;
    const float statusY = bottomButtonY + 8.0f * uiScale;
    if (inFault) {
        const std::string faultText = "FAULT: " + (data.faultReason.empty() ? std::string("UNKNOWN") : data.faultReason);
        std::string faultUpper;
        faultUpper.reserve(faultText.size());
        for (char ch : faultText) {
            faultUpper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        const float clearWidth = 86.0f * uiScale;
        const float availableWidth = innerWidth - clearWidth - 12.0f * uiScale;
        const float faultScale = fitTextScale(faultUpper, labelScale, availableWidth, 0.95f * uiScale);
        addText(vertices, faultUpper, innerX, statusY, faultScale, faultColor);

        const float clearHeight = bottomButtonHeight;
        const float clearX = panelX + panelWidth - clearWidth - 16.0f * uiScale;
        const float clearY = bottomButtonY;
        layout.clearFaultButton = {clearX, clearY, clearWidth, clearHeight};
        drawModeButton(vertices, layout.clearFaultButton, "CLEAR", false, true, uiScale);
    } else if (!data.statusDetail.empty()) {
        std::string detailUpper;
        detailUpper.reserve(data.statusDetail.size());
        for (char ch : data.statusDetail) {
            detailUpper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        }
        const float detailScale = fitTextScale(detailUpper, labelScale, innerWidth, 0.95f * uiScale);
        addText(vertices, detailUpper, innerX, statusY, detailScale, muted);
    } else if (data.mode == ControlHudMode::Evacuate && data.evacuationComplete) {
        const float completeScale = fitTextScale("EVACUATION COMPLETE", labelScale, innerWidth, 0.95f * uiScale);
        addText(vertices, "EVACUATION COMPLETE", innerX, statusY, completeScale, holdColor);
    }

    uploadAndDraw(renderer, viewportWidth, viewportHeight, vertices);
    return layout;
}

PlaybackTimelineLayout drawPlaybackTimeline(HudRenderer& renderer, int viewportWidth, int viewportHeight, const PlaybackTimelineData& data) {
    PlaybackTimelineLayout layout{};
    if (!data.visible || data.durationSec <= 0.0 || viewportWidth <= 0 || viewportHeight <= 0) {
        return layout;
    }

    const float widthScale = static_cast<float>(viewportWidth) / 1280.0f;
    const float heightScale = static_cast<float>(viewportHeight) / 800.0f;
    const float viewportScale = std::min(widthScale, heightScale);
    const float uiScale = std::clamp(1.0f + (viewportScale - 1.0f) * 1.25f, 1.0f, 1.7f);
    const float alpha = std::clamp(data.alpha, 0.0f, 1.0f);

    const float margin = 24.0f * uiScale;
    const float panelWidth = std::min(900.0f * uiScale, static_cast<float>(viewportWidth) - margin * 2.0f);
    const float panelHeight = 64.0f * uiScale;
    const float panelX = (static_cast<float>(viewportWidth) - panelWidth) * 0.5f;
    const float panelY = static_cast<float>(viewportHeight) - panelHeight - 22.0f * uiScale;
    const float innerX = panelX + 18.0f * uiScale;
    const float innerWidth = panelWidth - 36.0f * uiScale;
    const float trackHeight = 8.0f * uiScale;
    const float trackY = panelY + 30.0f * uiScale;

    layout.panel = {panelX, panelY, panelWidth, panelHeight};
    layout.track = {innerX, trackY, innerWidth, trackHeight};

    const double clampedElapsed = std::clamp(data.elapsedSec, 0.0, data.durationSec);
    const float progress = static_cast<float>(clampedElapsed / data.durationSec);
    const float filledWidth = innerWidth * progress;
    const float handleX = innerX + filledWidth;

    const Color panel{0.025f, 0.032f, 0.043f, 0.72f * alpha};
    const Color border{0.58f, 0.76f, 0.96f, 0.18f * alpha};
    const Color track{0.12f, 0.16f, 0.20f, 0.78f * alpha};
    const Color fill{0.34f, 0.92f, 0.68f, 0.92f * alpha};
    const Color tick{0.86f, 0.95f, 1.0f, 0.88f * alpha};
    const Color muted{0.70f, 0.78f, 0.86f, 0.78f * alpha};

    std::vector<HudVertex> vertices;
    vertices.reserve(1300);

    addRect(vertices, panelX + 6.0f * uiScale, panelY + 7.0f * uiScale, panelWidth, panelHeight, {0.0f, 0.0f, 0.0f, 0.22f * alpha});
    addRect(vertices, panelX, panelY, panelWidth, panelHeight, panel);
    addRect(vertices, panelX, panelY, panelWidth, 1.0f, border);
    addRect(vertices, panelX, panelY + panelHeight - 1.0f, panelWidth, 1.0f, border);
    addRect(vertices, panelX, panelY, 1.0f, panelHeight, border);
    addRect(vertices, panelX + panelWidth - 1.0f, panelY, 1.0f, panelHeight, border);

    addRect(vertices, innerX, trackY, innerWidth, trackHeight, track);
    if (filledWidth > 0.0f) {
        addRect(vertices, innerX, trackY, filledWidth, trackHeight, fill);
    }
    addRect(vertices, handleX - 2.0f * uiScale, trackY - 5.0f * uiScale, 4.0f * uiScale, trackHeight + 10.0f * uiScale, tick);

    const std::string elapsedText = formatClockTime(clampedElapsed);
    const std::string durationText = formatClockTime(data.durationSec);
    const float labelScale = 1.45f * uiScale;
    addText(vertices, elapsedText, innerX, panelY + 12.0f * uiScale, labelScale, tick);

    const float durationWidth = textWidth(durationText, labelScale);
    addText(vertices, durationText, innerX + innerWidth - durationWidth, panelY + 12.0f * uiScale, labelScale, muted);

    if (data.paused) {
        const std::string pausedText = "PAUSED";
        const float pausedWidth = textWidth(pausedText, labelScale);
        addText(vertices, pausedText, innerX + (innerWidth - pausedWidth) * 0.5f, panelY + 12.0f * uiScale, labelScale, fill);
    }

    uploadAndDraw(renderer, viewportWidth, viewportHeight, vertices);
    return layout;
}
