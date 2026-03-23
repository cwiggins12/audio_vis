#pragma once

#include "shader.hpp"
#include "audio_spec.hpp"
#include <string>
#include <filesystem>

using FileTime = std::filesystem::file_time_type;

struct Uniforms {
    GLint time = -1;
    GLint W = -1;
    GLint H = -1;
    GLint numBins = -1;
    GLint numChannels = -1;
    GLint errorChars = -1;
    GLint errorLen = -1;
    GLint showError = -1;
};

struct ShaderPreset {
    std::string name;
    std::string fragPath;
    std::string specPath;
    AudioSpec   spec;
    Shader      shader;
    Uniforms    uniforms;
    FileTime    lastFragWrite;
    FileTime    lastSpecWrite;
    bool        hasError = false;
    std::string errorMessage = "";
};

