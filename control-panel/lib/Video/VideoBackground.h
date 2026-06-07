#pragma once

#include "Video/VideoStream.h"

#include <glad/gl.h>

struct VideoBackgroundRenderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint texture = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    std::uint64_t uploadedFrameId = 0;
};

VideoBackgroundRenderer createVideoBackgroundRenderer();
void destroyVideoBackgroundRenderer(VideoBackgroundRenderer& renderer);
void updateVideoBackgroundTexture(VideoBackgroundRenderer& renderer, const VideoFrameSnapshot& frame);
void drawVideoBackground(VideoBackgroundRenderer& renderer, int viewportWidth, int viewportHeight);
