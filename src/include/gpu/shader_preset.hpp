#pragma once

#include "gpu/shader.hpp"
#include "config/spec.hpp"
#include <string>
#include <filesystem>
#include <vector>
#include <array>

using FileTime = std::filesystem::file_time_type;

struct TextureSlot {
    std::string uniformName = "";   //from frag.glsl
    std::string filename = "";      //from spec.cfg
    GLuint texId = 0;
    int unit = 0;                   //GL texture unit index
    int w = 0;
    int h = 0;
};

struct FontSlot {
    std::string uniformName = "";   //from spec.cfg  (e.g. "myFont")
    std::string filename = "";      //from spec.cfg  (e.g. "cool.ttf")
    GLuint atlasTexId   = 0;       //SDF atlas         — sampler: myFont
    GLuint metricsTexId = 0;       //glyph metrics     — sampler: myFontMetrics
    int atlasUnit   = 0;           //GL texture unit for atlas
    int metricsUnit = 0;           //GL texture unit for metrics
};

struct ShaderPreset {
    std::string name = "";
    std::string shaderDir = "";
    Spec spec;
    Shader shader;
    //vector of these. Size will be 2 + texAmt + fontAmt
    FileTime lastFragWrite;
    FileTime lastSpecWrite;
    std::array<int, 512> errorMessage = {0};
    int errorLen = 0;
    bool hasError = false;
    std::vector<TextureSlot> textures;
    std::vector<FontSlot> fonts;

    void destroyTextures() {
        for (auto& t : textures) {
            if (t.texId) {
                glDeleteTextures(1, &t.texId);
            }
        }
        textures.clear();
    }

    void destroyFonts() {
        for (auto& f : fonts) {
            if (f.atlasTexId) glDeleteTextures(1, &f.atlasTexId);
            if (f.metricsTexId) glDeleteTextures(1, &f.metricsTexId);
        }
        fonts.clear();
    }
};

