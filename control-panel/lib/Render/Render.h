#pragma once

#include "Math/Math.h"

#include <glad/gl.h>

#include <vector>

struct Vertex {
    Vec3 position;
    Vec3 normal;
    float alpha = 1.0f;
};

struct LineVertex {
    Vec3 position;
    float alpha = 1.0f;
};

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct GpuLines {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;
};

GLuint createProgram(const char* vertexSource, const char* fragmentSource);
GLuint createMeshProgram();
GLuint createLineProgram();

GpuMesh uploadMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
GpuLines uploadLines(const std::vector<LineVertex>& vertices);
GpuLines uploadLines(const std::vector<Vec3>& vertices);

void destroyMesh(GpuMesh& mesh);
void destroyLines(GpuLines& lines);

void drawMesh(GLuint program, const GpuMesh& mesh, const Mat4& mvp, const Vec3& cameraPos, float r, float g, float b, float a);
void drawLines(GLuint program, const GpuLines& lines, const Mat4& mvp, float r, float g, float b, float a, float width);
