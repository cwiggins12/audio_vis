#pragma once

#include <glad/glad.h>
#include <iostream>

inline const char* vertexSrc = R"(#version 310 es
precision highp float;

out vec2 uv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    vec2 pos = positions[gl_VertexID];
    uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

inline const char* fragmentHeader = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;
uniform float W;
uniform float H;
uniform int numBins;
uniform int numChannels;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
};
layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};
layout(std430, binding = 2) readonly buffer PRHolds {
    float prHolds[];
};
layout(std430, binding = 3) readonly buffer FFTHolds {
    float fftHolds[];
};
layout(std430, binding = 4) readonly buffer FeedbackRead {
    float feedbackIn[];
};
layout(std430, binding = 5) writeonly buffer FeedbackWrite {
    float feedbackOut[];
};

vec2 toPx()     { return vec2(uv.x * W, uv.y * H); }
vec2 toCenter() { return vec2((uv.x - 0.5) * W, (uv.y - 0.5) * H); }
)";

class Shader {
public:
    Shader() : id(0), valid(false) {}

    Shader(const char* vertSrc, const char* fragSrc) {
        std::string fragFinal = std::string(fragmentHeader) + fragSrc;
        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragFinal.c_str());

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);

        GLint success;
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(id, 512, nullptr, log);
            std::cerr << "Shader link error:\n" << log << std::endl;
        } else {
            valid = true;
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() {
        glUseProgram(id);
    }

    GLuint id;
    bool valid = false;

private:
    GLuint compile(GLenum type, const char* src) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << "Shader compile error:\n" << log << std::endl;
        }

        return shader;
    }
};

