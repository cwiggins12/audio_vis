#pragma once

#include "shader_preset.hpp"
#include "spec_parser.hpp"
#include "texture_loader.hpp"
#include <algorithm> //just using this for sort

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

inline const char* errorFragSrc = R"(
void main() {
    vec2 fragPx = toPx();
    vec4 bg = vec4(0.1, 0.0, 0.0, 1.0);
    if (showError == 0) {
        FragColor = bg;
        return;
    }
    float text = renderText(errorChars, errorLen,
                            vec2(16.0, 16.0), 12.0, fragPx);
    FragColor = mix(bg, vec4(1.0, 0.3, 0.3, 1.0), text);
}
)";

inline Shader& getErrorShader() {
    static Shader s(vertexSrc, errorFragSrc);
    return s;
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
        p.spec = Spec{};

        if (std::filesystem::exists(specPath)) {
            if (!parseSpec(specPath.string(), p.spec)) {
                std::cerr << "loadPresets: skipping " << p.name
                          << " - spec parse failed\n";
                continue;
            }
        }
        //set limits on buffer sizes and warn about double dependencies here
        if (p.spec.customFFTSize > 8192) {
            std::cerr << "loadPresets: skipping " << p.name
                      << " - customFFTSize cannot exceed max FFT size (8192)";
        }
        if (p.spec.feedbackBufferSize > 33177600) {
            std::cerr << "loadPresets: skipping " << p.name
                      << " - feedback buffer size cannot exceed 4k frame buffer size (33177600)";
        }
        const int fftScaleMode = p.spec.customFFTSizeScalesWithWindow;
        if (p.spec.fftUsesExprVar[WINDOW_WIDTH] && (fftScaleMode == WIDTH_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "FFT custom size doubly scaled by WINDOW_WIDTH due to scale mode and width expression variable usage\n";
        }
        if (p.spec.fftUsesExprVar[WINDOW_HEIGHT] && (fftScaleMode == HEIGHT_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "FFT custom size doubly scaled by WINDOW_HEIGHT due to scale mode and height expression variable usage\n";
        }
        const int feedbackScaleMode = p.spec.feedbackBufferScalesWithWindow;
        if (p.spec.feedbackUsesExprVar[WINDOW_WIDTH] && (feedbackScaleMode == WIDTH_SCALE || feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "Feedback buffer doubly scaled by WINDOW_WIDTH due to scale mode and width expression variable usage\n";
        }
        if (p.spec.feedbackUsesExprVar[WINDOW_HEIGHT] && (feedbackScaleMode == HEIGHT_SCALE || feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "Feedback buffer doubly scaled by WINDOW_HEIGHT due to scale mode and height expression variable usage\n";
        }

        p.shader = Shader(vertexSrc, fragSrc.c_str());
        if (!p.shader.valid) {
            std::cerr << "loadPresets: skipping " << p.name
                      << " - shader compile failed\n";
            continue;
        }

        std::string loadedName = p.name;
        p.shaderDir = entry.path().string();
        p.fragPath = fragPath.string();
        p.specPath = specPath.string();
        p.lastFragWrite = std::filesystem::last_write_time(fragPath);
        p.lastSpecWrite = std::filesystem::exists(specPath)
                          ? std::filesystem::last_write_time(specPath)
                          : std::filesystem::file_time_type{};
        buildTextures(p);
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
    Spec newSpec{};
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
        p.errorMessage = newShader.errorLog;
        return;
    }

    // success — swap in new shader and spec
    p.shader       = std::move(newShader);
    p.spec         = newSpec;
    p.hasError     = false;
    p.errorMessage = "";
    buildTextures(p);
}

