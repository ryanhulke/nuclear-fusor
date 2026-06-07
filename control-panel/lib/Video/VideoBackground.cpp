#include "Video/VideoBackground.h"

#include "Render/Render.h"

#include <algorithm>
#include <array>

namespace {

struct VideoVertex {
    float x;
    float y;
    float u;
    float v;
};

const char* videoVertexShaderSource() {
    return R"GLSL(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;

out vec2 vUv;

void main() {
    vUv = aUv;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";
}

const char* videoFragmentShaderSource() {
    return R"GLSL(
#version 330 core
in vec2 vUv;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    FragColor = texture(uTexture, vUv);
}
)GLSL";
}

std::array<VideoVertex, 6> coverVertices(int viewportWidth, int viewportHeight, int textureWidth, int textureHeight) {
    float u0 = 0.0f;
    float u1 = 1.0f;
    float v0 = 0.0f;
    float v1 = 1.0f;

    if (viewportWidth > 0 && viewportHeight > 0 && textureWidth > 0 && textureHeight > 0) {
        const float viewportAspect = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
        const float textureAspect = static_cast<float>(textureWidth) / static_cast<float>(textureHeight);

        if (textureAspect > viewportAspect) {
            const float visibleWidth = std::clamp(viewportAspect / textureAspect, 0.0f, 1.0f);
            u0 = (1.0f - visibleWidth) * 0.5f;
            u1 = 1.0f - u0;
        } else {
            const float visibleHeight = std::clamp(textureAspect / viewportAspect, 0.0f, 1.0f);
            v0 = (1.0f - visibleHeight) * 0.5f;
            v1 = 1.0f - v0;
        }
    }

    return {
        VideoVertex{-1.0f, -1.0f, u0, v1},
        VideoVertex{1.0f, -1.0f, u1, v1},
        VideoVertex{1.0f, 1.0f, u1, v0},
        VideoVertex{-1.0f, -1.0f, u0, v1},
        VideoVertex{1.0f, 1.0f, u1, v0},
        VideoVertex{-1.0f, 1.0f, u0, v0},
    };
}

} // namespace

VideoBackgroundRenderer createVideoBackgroundRenderer() {
    VideoBackgroundRenderer renderer{};
    renderer.program = createProgram(videoVertexShaderSource(), videoFragmentShaderSource());

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);
    glGenTextures(1, &renderer.texture);

    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(VideoVertex) * 6), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VideoVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VideoVertex), reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return renderer;
}

void destroyVideoBackgroundRenderer(VideoBackgroundRenderer& renderer) {
    if (renderer.texture) {
        glDeleteTextures(1, &renderer.texture);
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

void updateVideoBackgroundTexture(VideoBackgroundRenderer& renderer, const VideoFrameSnapshot& frame) {
    if (renderer.texture == 0 || !frame.hasFrame || frame.rgb.empty() || frame.width <= 0 || frame.height <= 0) {
        return;
    }
    if (renderer.uploadedFrameId == frame.frameId) {
        return;
    }

    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (renderer.textureWidth != frame.width || renderer.textureHeight != frame.height) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGB8,
            frame.width,
            frame.height,
            0,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            frame.rgb.data()
        );
        renderer.textureWidth = frame.width;
        renderer.textureHeight = frame.height;
    } else {
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            frame.width,
            frame.height,
            GL_RGB,
            GL_UNSIGNED_BYTE,
            frame.rgb.data()
        );
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    renderer.uploadedFrameId = frame.frameId;
}

void drawVideoBackground(VideoBackgroundRenderer& renderer, int viewportWidth, int viewportHeight) {
    if (renderer.program == 0 || renderer.texture == 0 || renderer.textureWidth <= 0 || renderer.textureHeight <= 0) {
        return;
    }

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean previousDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const auto vertices = coverVertices(viewportWidth, viewportHeight, renderer.textureWidth, renderer.textureHeight);

    glUseProgram(renderer.program);
    glUniform1i(glGetUniformLocation(renderer.program, "uTexture"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(sizeof(VideoVertex) * vertices.size()), vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDepthMask(previousDepthMask);
    if (depthEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
    if (blendEnabled) {
        glEnable(GL_BLEND);
    }
}
