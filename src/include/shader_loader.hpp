#pragma once

#include "shader_preset.hpp"
#include "spec_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <algorithm>

inline std::string loadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "loadFile: could not open " << path << "\n";
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

//TODO if a possible logic error could happen in the spec settings, handle it here
//currently does nothing to not have to adjust on every iteration
//put new line at end of any spec error pls
inline std::string specAssertHell(AudioSpec& spec) {
    return "";
}

inline std::vector<ShaderPreset> loadPresets(const std::string& shadersDir) {
    std::vector<ShaderPreset> presets;

    if (!std::filesystem::exists(shadersDir)) {
        std::cerr << "loadPresets: directory not found: " << shadersDir << "\n";
        return presets;
    }

    std::vector<std::filesystem::directory_entry> entries;
    for (auto& entry : std::filesystem::directory_iterator(shadersDir)) {
        if (entry.is_directory()) {
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end());

    for (auto& entry : entries) {
        if (!entry.is_directory()) continue;

        auto fragPath = entry.path() / "frag.glsl";
        auto specPath = entry.path() / "spec.cfg";

        if (!std::filesystem::exists(fragPath)) {
            std::cerr << "loadPresets: skipping " << entry.path().filename()
                      << " - no frag.glsl found\n";
            continue;
        }

        std::string fragSrc = loadFile(fragPath.string());
        if (fragSrc.empty()) continue;

        ShaderPreset p;
        p.name = entry.path().filename().string();
        p.spec = AudioSpec{};

        if (std::filesystem::exists(specPath)) {
            if (!parseSpec(specPath.string(), p.spec)) {
                std::cerr << "loadPresets: skipping " << p.name
                          << " - spec parse failed\n";
                continue;
            }
            std::string specErr = specAssertHell(p.spec);
            if (specErr != "") {
                std::cerr << "loadPresets: skipping " << p.name
                          << " - spec logic error found - " <<
                          specErr;
            }
        }

        p.shader = Shader(vertexSrc, fragSrc.c_str());
        if (!p.shader.valid) {
            std::cerr << "loadPresets: skipping " << p.name
                      << " - shader compile failed\n";
            continue;
        }
        p.uniforms.time = glGetUniformLocation(p.shader.id, "time");
        p.uniforms.W = glGetUniformLocation(p.shader.id, "W");
        p.uniforms.H = glGetUniformLocation(p.shader.id, "H");
        p.uniforms.numBins = glGetUniformLocation(p.shader.id, "numBins");
        p.uniforms.numChannels = glGetUniformLocation(p.shader.id, "numChannels");
        p.uniforms.errorChars = glGetUniformLocation(p.shader.id, "errorChars");
        p.uniforms.errorLen = glGetUniformLocation(p.shader.id, "errorLen");
        p.uniforms.showError = glGetUniformLocation(p.shader.id, "showError");
        std::string loadedName = p.name;
        p.fragPath = fragPath.string();
        p.specPath = specPath.string();
        p.lastFragWrite = std::filesystem::last_write_time(fragPath);
        p.lastSpecWrite = std::filesystem::exists(specPath)
                          ? std::filesystem::last_write_time(specPath)
                          : std::filesystem::file_time_type{};
        presets.push_back(std::move(p));
        std::cout << "loadPresets: loaded " << loadedName << "\n";
    }

    return presets;
}

inline void reloadPreset(ShaderPreset& p) {
    std::string fragSrc = loadFile(p.fragPath);
    if (fragSrc.empty()) {
        p.hasError     = true;
        p.errorMessage = "failed to read frag.glsl";
        return;
    }

    // re-parse spec if it exists
    AudioSpec newSpec{};
    if (!p.specPath.empty() && std::filesystem::exists(p.specPath)) {
        if (!parseSpec(p.specPath, newSpec)) {
            p.hasError     = true;
            p.errorMessage = "spec parse failed";
            return;
        }
    }

    Shader newShader(vertexSrc, fragSrc.c_str());
    if (!newShader.valid) {
        p.hasError     = true;
        p.errorMessage = "shader compile failed";
        return;
    }

    // success — swap in new shader and spec
    p.shader       = std::move(newShader);
    p.spec         = newSpec;
    p.hasError     = false;
    p.errorMessage = "";

    p.uniforms.time        = glGetUniformLocation(p.shader.id, "time");
    p.uniforms.W           = glGetUniformLocation(p.shader.id, "W");
    p.uniforms.H           = glGetUniformLocation(p.shader.id, "H");
    p.uniforms.numBins     = glGetUniformLocation(p.shader.id, "numBins");
    p.uniforms.numChannels = glGetUniformLocation(p.shader.id, "numChannels");
    p.uniforms.errorChars  = glGetUniformLocation(p.shader.id, "errorChars");
    p.uniforms.errorLen    = glGetUniformLocation(p.shader.id, "errorLen");
    p.uniforms.showError   = glGetUniformLocation(p.shader.id, "showError");
}
