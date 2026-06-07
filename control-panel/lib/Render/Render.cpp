#include "Render/Render.h"

#include <cstddef>
#include <iostream>

namespace {

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error:\n" << log << "\n";
    }
    return shader;
}

const char* meshVertexShaderSource() {
    return R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in float aAlpha;

uniform mat4 uMVP;

out vec3 vNormal;
out vec3 vWorldPos;
out float vAlpha;

void main() {
    vNormal = normalize(aNormal);
    vWorldPos = aPos;
    vAlpha = aAlpha;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";
}

const char* meshFragmentShaderSource() {
    return R"GLSL(
#version 330 core
in vec3 vNormal;
in vec3 vWorldPos;
in float vAlpha;

uniform vec4 uColor;
uniform vec3 uCameraPos;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.35, -0.45, 0.85));
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float diffuse = 0.42 + 0.58 * abs(dot(normal, lightDir));
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    vec3 rimColor = vec3(0.34, 0.68, 0.95) * rim * 0.28;
    vec3 color = uColor.rgb * diffuse + rimColor;
    FragColor = vec4(color, uColor.a * vAlpha);
}
)GLSL";
}

const char* lineVertexShaderSource() {
    return R"GLSL(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aAlpha;

uniform mat4 uMVP;

out float vAlpha;

void main() {
    vAlpha = aAlpha;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";
}

const char* lineFragmentShaderSource() {
    return R"GLSL(
#version 330 core
in float vAlpha;

uniform vec4 uColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(uColor.rgb, uColor.a * vAlpha);
}
)GLSL";
}

} // namespace

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << "\n";
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

GLuint createMeshProgram() {
    return createProgram(meshVertexShaderSource(), meshFragmentShaderSource());
}

GLuint createLineProgram() {
    return createProgram(lineVertexShaderSource(), lineFragmentShaderSource());
}

GpuMesh uploadMesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    GpuMesh mesh{};
    mesh.indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
        indices.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, alpha)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    return mesh;
}

GpuLines uploadLines(const std::vector<LineVertex>& vertices) {
    GpuLines lines{};
    lines.vertexCount = static_cast<GLsizei>(vertices.size());

    glGenVertexArrays(1, &lines.vao);
    glGenBuffers(1, &lines.vbo);

    glBindVertexArray(lines.vao);
    glBindBuffer(GL_ARRAY_BUFFER, lines.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, alpha)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    return lines;
}

GpuLines uploadLines(const std::vector<Vec3>& vertices) {
    std::vector<LineVertex> lineVertices;
    lineVertices.reserve(vertices.size());
    for (const Vec3& vertex : vertices) {
        lineVertices.push_back({vertex, 1.0f});
    }
    return uploadLines(lineVertices);
}

void destroyMesh(GpuMesh& mesh) {
    if (mesh.ebo) {
        glDeleteBuffers(1, &mesh.ebo);
    }
    if (mesh.vbo) {
        glDeleteBuffers(1, &mesh.vbo);
    }
    if (mesh.vao) {
        glDeleteVertexArrays(1, &mesh.vao);
    }
    mesh = {};
}

void destroyLines(GpuLines& lines) {
    if (lines.vbo) {
        glDeleteBuffers(1, &lines.vbo);
    }
    if (lines.vao) {
        glDeleteVertexArrays(1, &lines.vao);
    }
    lines = {};
}

void drawMesh(GLuint program, const GpuMesh& mesh, const Mat4& mvp, const Vec3& cameraPos, float r, float g, float b, float a) {
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniform3f(glGetUniformLocation(program, "uCameraPos"), cameraPos.x, cameraPos.y, cameraPos.z);
    glUniform4f(glGetUniformLocation(program, "uColor"), r, g, b, a);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void drawLines(GLuint program, const GpuLines& lines, const Mat4& mvp, float r, float g, float b, float a, float width) {
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniform4f(glGetUniformLocation(program, "uColor"), r, g, b, a);
    glLineWidth(width);
    glBindVertexArray(lines.vao);
    glDrawArrays(GL_LINES, 0, lines.vertexCount);
    glBindVertexArray(0);
}
