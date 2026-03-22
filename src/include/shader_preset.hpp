#pragma once

#include "shader.hpp"
#include "audio_spec.hpp"
#include <string>

struct Uniforms {
    GLint time = -1;
    GLint W = -1;
    GLint H = -1;
    GLint numBins = -1;
    GLint numChannels = -1;
};

struct ShaderPreset {
    std::string name;
    AudioSpec   spec;
    Shader      shader;
    Uniforms    uniforms;
};
