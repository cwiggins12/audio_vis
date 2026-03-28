#pragma once

#include "shader_preset.hpp"
#include "spec_parser.hpp"
#include "texture_loader.hpp"
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

inline const char* errorFragSrc = R"(
void main() {
    vec2 fragPx = toPx();
    vec4 bg = vec4(0.1, 0.0, 0.0, 1.0);
    if (showError == 0) {
        FragColor = bg;
        return;
    }
    float text = renderText(errorChars, errorLen, vec2(16.0, 16.0), 24.0, fragPx);
    FragColor = mix(bg, vec4(1.0, 0.1, 0.1, 1.0), text);
}
)";

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

        std::string ret = "";
        if (!std::filesystem::exists(fragPath)) {
            std::cerr << "loadPresets: Error in " << entry.path().filename()
                      << " - no frag.glsl found. Skipping\n";
            continue;
        }
        std::string fragSrc = loadFile(fragPath.string());
        if (fragSrc.empty()) {
            std::cerr << "loadPresets: Error in " << entry.path().filename()
                      << " - frag.glsl could not be opened. Skipping\n";
            continue;
        }

        ShaderPreset p;
        p.name = entry.path().filename().string();
        p.spec = Spec{};

        if (std::filesystem::exists(specPath)) {
            ret = parseSpec(specPath.string(), p.spec);
            if (ret != "") {
                ret = "loadPresets: Error in " + p.name +
                      " spec.cfg - " + ret;
                std::cerr << ret;
                p.errorMessage = ret;
                p.hasError = true;
                presets.push_back(std::move(p));
                std::cout << "loadPresets: using ErrorShader in" << p.name << "\n";
                continue;
            }
        }
        const int fftScaleMode = p.spec.customFFTSizeScalesWithWindow;
        if (p.spec.fftUsesExprVar[WINDOW_WIDTH] &&
           (fftScaleMode == WIDTH_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "FFT custom size doubly scaled by WINDOW_WIDTH due to scale mode and width expression variable usage\n";
        }
        if (p.spec.fftUsesExprVar[WINDOW_HEIGHT] &&
           (fftScaleMode == HEIGHT_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "FFT custom size doubly scaled by WINDOW_HEIGHT due to scale mode and height expression variable usage\n";
        }
        const int feedbackScaleMode = p.spec.feedbackBufferScalesWithWindow;
        if (p.spec.feedbackUsesExprVar[WINDOW_WIDTH] &&
           (feedbackScaleMode == WIDTH_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "Feedback buffer doubly scaled by WINDOW_WIDTH due to scale mode and width expression variable usage\n";
        }
        if (p.spec.feedbackUsesExprVar[WINDOW_HEIGHT] &&
           (feedbackScaleMode == HEIGHT_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                "Feedback buffer doubly scaled by WINDOW_HEIGHT due to scale mode and height expression variable usage\n";
        }

        p.shader = Shader(vertexSrc, fragSrc.c_str());
        if (!p.shader.valid) {
            ret = "loadPresets: skipping " + p.name +
                  " - shader compile failed - " + p.shader.errorLog;
            std::cerr << ret;
            p.errorMessage = ret;
            p.hasError = true;
            presets.push_back(std::move(p));
            std::cout << "loadPresets: using ErrorShader in" << p.name << "\n";
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
        p.errorMessage = "Hot Reload - " + p.name + 
                         " failed to open file - frag.glsl\n";
        return;
    }

    Spec newSpec{};
    std::string errLog = "";
    if (!p.specPath.empty() && std::filesystem::exists(p.specPath)) {
        errLog = parseSpec(p.specPath, newSpec);
        if (errLog != "") {
            p.hasError     = true;
            p.errorMessage = "Hot Reload - " + p.name + 
                             " spec parse failed - " + errLog;
            p.spec = newSpec;
            return;
        }
    }

    Shader newShader(vertexSrc, fragSrc.c_str());
    if (!newShader.valid) {
        p.hasError     = true;
        p.errorMessage = "Hot Reload - " + p.name + newShader.errorLog + "\n";
        p.spec = newSpec;
        return;
    }

    // success — swap in new shader and spec
    p.shader       = std::move(newShader);
    p.spec         = newSpec;
    p.hasError     = false;
    p.errorMessage = "";
    buildTextures(p);
}

inline bool assertUserDefinedBufferSizes(ShaderPreset& p) {
    std::string ret = "";
    //set limits on buffer sizes and warn about double dependencies here
    if (p.spec.customFFTSize > 8192) {
        ret = "loadPresets: skipping " + p.name +
              " - customFFTSize cannot exceed max FFT size (8192)\n";
        std::cerr << ret;
        p.errorMessage = ret;
        p.hasError = true;
        std::cout << "loadPresets: using ErrorShader in" << p.name << "\n";
        return false;
    }
    if (p.spec.feedbackBufferSize > 33177600) {
        ret = "loadPresets: skipping " + p.name +
              " - feedback buffer size cannot exceed 4k frame buffer size (33177600)\n";
        std::cerr << ret;
        p.errorMessage = ret;
        p.hasError = true;
        std::cout << "loadPresets: using ErrorShader in" << p.name << "\n";
        return false;
    }
    return true;
}
