#pragma once

#include "shader.hpp"
#include "audio_spec.hpp"
#include <string>
#include <unordered_map>

struct ShaderPreset {
    std::string name;
    AudioSpec   spec;
    Shader      shader;
    std::unordered_map<std::string, GLint> uniforms;
};
