#pragma once

#include "shader.hpp"
#include "audio_spec.hpp"
#include <string>

struct ShaderPreset {
    std::string name;
    AudioSpec   spec;
    Shader      shader;
};
