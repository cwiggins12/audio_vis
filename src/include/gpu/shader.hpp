#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "gpu/fragment_header.hpp"

static const int headerLines = 205;

//be sure to call init immediately upon construction!!!
class Shader {
public:
    Shader() = default;
    ~Shader() { if (id) glDeleteProgram(id); }
    Shader(Shader&& o) noexcept : id(o.id), samplerLocations(std::move(o.samplerLocations)) {
        o.id = 0;
    }

    Shader& operator=(Shader&& o) noexcept {
        if (this != &o) {
            if (id) glDeleteProgram(id);
            id = o.id; o.id = 0;
            samplerLocations = std::move(o.samplerLocations);
        }
        return *this;
    }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    std::string init(const char* vertSrc, const char* fragSrc) {
        std::string fragFinal = std::string(fragmentHeader) + fragSrc;
        std::string vertErr, fragErr, errorLog;
        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc, vertErr);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragFinal.c_str(), fragErr);

        if (!vertErr.empty()) errorLog += "VERT: " + vertErr;
        if (!fragErr.empty()) errorLog += "FRAG: " + fragErr;

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);

        GLint success;
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(id, 512, nullptr, log);
            errorLog += std::string(log) + "\n";
        }

        glDeleteShader(vert);
        glDeleteShader(frag);

        return errorLog;
    }

    void resolveSamplerLocations(const std::vector<std::string>& names) {
        samplerLocations.clear();
        for (auto& name : names) {
            samplerLocations[name] = glGetUniformLocation(id, name.c_str());
        }
    }
    void use() { glUseProgram(id); }

    GLuint id = 0;
    std::unordered_map<std::string, GLint> samplerLocations;

private:
    GLuint compile(GLenum type, const char* src, std::string& errOut) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            errOut = std::string(log);
        }
        return shader;
    }
};

inline const char* vertexSrc = R"(#version 310 es
precision highp float;
out vec2 v_pos;
void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    v_pos = positions[gl_VertexID];
    gl_Position = vec4(v_pos, 0.0, 1.0);
}
)";

inline const char* errorFragSrc = R"(
void main() {
    vec2 fragPx = toPx();
    vec4 bg = vec4(0.1, 0.0, 0.0, 1.0);
    if (showError == 0) {
        FragColor = bg;
        return;
    }
    float spacing = 20.0;
    float fontSize = 24.0;
    int charAmt = int((W - spacing * 2.0) / fontSize);
    float lineH = spacing + fontSize;
    int loops = (errorLen + charAmt - 1) / charAmt;

    float text = 0.0;
    for (int i = 0; i < loops; i++) {
        int offset = i * charAmt;
        int count = min(charAmt, errorLen - offset);
        text = max(text, renderText(errorChars, count,
                                    vec2(spacing, H - (lineH * float(i + 1))),
                                    fontSize, fragPx, charAmt * i));
    }
    FragColor = mix(bg, vec4(1.0, 1.0, 1.0, 1.0), text);
}
)";

