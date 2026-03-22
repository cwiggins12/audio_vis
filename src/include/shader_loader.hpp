#pragma once

#include "shader_preset.hpp"
#include "spec_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

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

    for (auto& entry : std::filesystem::directory_iterator(shadersDir)) {
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
        p.name   = entry.path().filename().string();
        p.spec   = AudioSpec{};

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

        presets.push_back(std::move(p));
        std::cout << "loadPresets: loaded " << p.name << "\n";
    }

    return presets;
}

