#pragma once

#include <glad/glad.h>
#include <iostream>
#include "fragment_header.hpp"

struct BaseUniforms {
    GLint time = -1;
    GLint W = -1;
    GLint H = -1;
    GLint numBins = -1;
    GLint numChannels = -1;
    GLint errorChars = -1;
    GLint errorLen = -1;
    GLint showError = -1;
};

//this gets moved, which is fine, but maybe putting a basic rule of 5 here
//would add some clarity
class Shader {
public:
    GLuint          id       = 0;
    bool            valid    = false;
    std::string     errorLog = "";
    BaseUniforms    uniforms;

    Shader() = default;

    Shader(const char* vertSrc, const char* fragSrc) {
        std::string fragFinal = std::string(fragmentHeader) + fragSrc;
        std::string vertErr, fragErr;
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
            errorLog += "LINK: " + std::string(log);
            std::cerr << "Shader link error:\n" << log << "\n";
        } else if (errorLog.empty()) {
            valid = true;
            uniforms.time        = glGetUniformLocation(id, "time");
            uniforms.W           = glGetUniformLocation(id, "W");
            uniforms.H           = glGetUniformLocation(id, "H");
            uniforms.numBins     = glGetUniformLocation(id, "numBins");
            uniforms.numChannels = glGetUniformLocation(id, "numChannels");
            uniforms.errorChars  = glGetUniformLocation(id, "errorChars");
            uniforms.errorLen    = glGetUniformLocation(id, "errorLen");
            uniforms.showError   = glGetUniformLocation(id, "showError");
        }

        glDeleteShader(vert);
        glDeleteShader(frag);
    }

    void use() { glUseProgram(id); }

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
            std::cerr << "Shader compile error:\n" << log << "\n";
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

