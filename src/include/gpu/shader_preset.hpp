#pragma once

#include "gpu/shader.hpp"
#include "config/spec.hpp"
#include <string>
#include <filesystem>
#include <vector>
#include <array>

inline int texAllocCount = 0;

using FileTime = std::filesystem::file_time_type;

struct TextureSlot {
    std::string uniformName = "";   //from frag.glsl
    std::string filename = "";      //from spec.cfg
    GLuint texId = 0;
    int unit = 0;                   //GL texture unit index
};

struct ShaderPreset {
    std::string name = "";
    std::string shaderDir = "";
    Spec spec;
    Shader shader;
    FileTime lastFragWrite;
    FileTime lastSpecWrite;
    std::array<int, 128> errorMessage = {0};
    int errorLen = 0;
    bool hasError = false;
    std::vector<TextureSlot> textures;

    void destroyTextures() {
        for (auto& t : textures) {
            if (t.texId) {
                glDeleteTextures(1, &t.texId);
                texAllocCount--;
                std::cout << "TEX free: " << t.texId << " (alive: "
                          << texAllocCount << ")\n";
            }
        }
        textures.clear();
    }
};

