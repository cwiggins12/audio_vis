#pragma once

#include "shader.hpp"
#include "audio_spec.hpp"
#include <string>
#include <filesystem>
#include <vector>

using FileTime = std::filesystem::file_time_type;

struct TextureSlot {
    std::string uniformName = "";   //from frag.glsl
    std::string filename = "";      //from spec.cfg
    GLuint texId = 0;
    int unit = 0;                   //GL texture unit index
};

struct ShaderPreset {
    std::string name = "";
    //TODO: gonna wanna make this cheaper l8r
    std::string fragPath = "";
    std::string specPath = "";
    std::string shaderDir = "";
    AudioSpec   spec;
    Shader      shader;
    FileTime    lastFragWrite;
    FileTime    lastSpecWrite;
    bool        hasError = false;
    std::string errorMessage = "";
    std::vector<TextureSlot> textures;

    void destroyTextures() {
        for (auto& t : textures) {
            if (t.texId) {
                glDeleteTextures(1, &t.texId);
            }
            textures.clear();
        }
    }
};

