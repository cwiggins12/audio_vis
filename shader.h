#pragma once

#include <glad/gles2.h>
#include <iostream>

class Shader {
public:
    GLuint id;

    Shader(const char* vertSrc, const char* fragSrc) {
        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);

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
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() {
        glUseProgram(id);
    }

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

inline const char* fragmentSrc = R"(#version 310 es
precision highp float;

in vec2 uv;
out vec4 FragColor;

uniform float time;
uniform int numBins;

layout(std430, binding = 0) readonly buffer PeakRMS {
    float peakRmsData[];
};

layout(std430, binding = 1) readonly buffer FFTBins {
    float fftData[];
};

void main() {
    //map uv.x to a bin index
    int binIndex = int(uv.x * float(numBins));
    binIndex = clamp(binIndex, 0, numBins - 1);

    float db = fftData[binIndex];

    //normalize -96..0 to 0..1
    float normalized =(db + 96.0) / 96.0;
    normalized = clamp(normalized, 0.0, 1.0);

    //draw bar - 1.0 if uv.y is below the bar height, 0.0 otherwise
    float bar = step(uv.y, normalized);

    //color: gradient from blue at bottom to cyan at top
    vec3 color = mix(vec3(0.0, 0.2, 0.8), vec3(0.0, 1.0, 0.9), uv.y) * bar;

    //dark background where there's no bar
    color += vec3(0.05, 0.07, 0.12) * (1.0 - bar);

    FragColor = vec4(color, 1.0);
}
)";

