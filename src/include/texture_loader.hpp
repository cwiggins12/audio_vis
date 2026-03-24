#pragma once

#define STB_IMAGE_IMPLEMENTATION

#include "shader_preset.hpp"
#include "stb_image.h"
#include <iostream>

inline GLuint uploadTexture(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "uploadTexture: stbi_load failed for " << path
                  << ": " << stbi_failure_reason() << "\n";
        return 0;
    }
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    std::cout << "uploadTexture: loaded " << path
              << " (" << w << "x" << h << ")\n";
    return id;
}


// Build TextureSlots for a preset after shader compile and spec parse.
// Call this once during load and again on hot reload.
inline void buildTextures(ShaderPreset& p) {
    p.destroyTextures();

    int unit = 0;
    for (auto& [uniformName, filename] : p.spec.textures) {
        auto fullPath = std::filesystem::path(p.shaderDir) / filename;
        GLuint texId = uploadTexture(fullPath.string());
        if (!texId) {
            std::cerr << "buildTextures: skipping " << uniformName
                      << " -> " << filename << "\n";
            continue;
        }
        TextureSlot slot;
        slot.uniformName = uniformName;
        slot.filename    = filename;
        slot.texId       = texId;
        slot.unit        = unit++;
        p.textures.push_back(slot);
    }

    // Tell the shader which texture unit each sampler uses
    if (!p.textures.empty()) {
        std::vector<std::string> names;
        for (auto& t : p.textures) names.push_back(t.uniformName);
        p.shader.resolveSamplerLocations(names);

        p.shader.use();
        for (auto& t : p.textures) {
            auto it = p.shader.samplerLocations.find(t.uniformName);
            if (it != p.shader.samplerLocations.end() && it->second != -1)
                glUniform1i(it->second, t.unit);
        }
    }
}

// Bind all textures for a preset. Call once per frame before glDrawArrays.
inline void bindTextures(const ShaderPreset& p) {
    for (auto& t : p.textures) {
        glActiveTexture(GL_TEXTURE0 + t.unit);
        glBindTexture(GL_TEXTURE_2D, t.texId);
    }
}

inline void unbindTextures(const ShaderPreset& p) {
    for (auto& t : p.textures) {
        glActiveTexture(GL_TEXTURE0 + t.unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
