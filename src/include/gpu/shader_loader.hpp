#pragma once

#include "gpu/shader_preset.hpp"
#include "config/spec_parser.hpp"
#include "gpu/texture_loader.hpp"
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
                std::cerr << "loadPresets: Error in " + p.name +
                             " spec.cfg - " + ret;
                p.errorMessage = "loadPresets: Error in " + p.name +
                      " spec.cfg. Check log.txt for more details.";
                p.hasError = true;
                presets.push_back(std::move(p));
                std::cout << "loadPresets: using ErrorShader in " << p.name << "\n";
                continue;
            }
        }
        const int fftScaleMode = p.spec.customFFTSizeScalesWithWindow;
        if (p.spec.fftUsesExprVar[WINDOW_WIDTH] &&
           (fftScaleMode == WIDTH_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                " FFT custom size doubly scaled by WINDOW_WIDTH " <<
                "due to scale mode and width expression variable usage\n";
        }
        if (p.spec.fftUsesExprVar[WINDOW_HEIGHT] &&
           (fftScaleMode == HEIGHT_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                " FFT custom size doubly scaled by WINDOW_HEIGH T" <<
                "due to scale mode and height expression variable usage\n";
        }
        if (p.spec.customFFTSizeScalesWithWindow != NO_SCALE &&
            p.spec.fftOutputMode != CUSTOM_SIZE) {
            std::cout << "WARNING: " << p.name <<
                " FFT is not using custom size mode, but has window scaling. " <<
                "Window scaling set to none. \n";
            p.spec.customFFTSizeScalesWithWindow = NO_SCALE;
        }
        const int feedbackScaleMode = p.spec.feedbackBufferScalesWithWindow;
        if (p.spec.feedbackUsesExprVar[WINDOW_WIDTH] &&
           (feedbackScaleMode == WIDTH_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                " Feedback buffer doubly scaled by WINDOW_WIDTH due to " <<
                "scale mode and width expression variable usage\n";
        }
        if (p.spec.feedbackUsesExprVar[WINDOW_HEIGHT] &&
           (feedbackScaleMode == HEIGHT_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << p.name <<
                " Feedback buffer doubly scaled by WINDOW_HEIGHT due to " <<
                "scale mode and height expression variable usage\n";
        }

        p.shader = Shader();
        ret = p.shader.init(vertexSrc, fragSrc.c_str());
        std::string loadedName = p.name;
        p.shaderDir = entry.path().string();
        p.lastFragWrite = std::filesystem::last_write_time(fragPath);
        p.lastSpecWrite = std::filesystem::exists(specPath)
                        ? std::filesystem::last_write_time(specPath)
                        : std::filesystem::file_time_type{};
        if (!ret.empty()) {
            std::cerr << "loadPresets: " << loadedName <<
                         " - shader compile failed - " << ret;
            p.errorMessage = "loadPresets: " + loadedName +
                             " - shader compilation failed. " +
                             "Check log.txt for more details.\n";
            p.hasError = true;
            presets.push_back(std::move(p));
            std::cout << "loadPresets: using ErrorShader in " << loadedName << "\n";
            continue;
        }

        buildTextures(p);
        presets.push_back(std::move(p));
        std::cout << "loadPresets: loaded " << loadedName << "\n";
    }
    return presets;
}

inline void reloadPreset(ShaderPreset* p) {
    auto fragPath = std::filesystem::path(p->shaderDir) / "frag.glsl";
    auto specPath = std::filesystem::path(p->shaderDir) / "spec.cfg";
    std::string fragSrc = loadFile(fragPath);
    if (fragSrc.empty()) {
        p->hasError     = true;
        p->errorMessage = "Hot Reload - " + p->name +
                         " failed to open file - frag.glsl\n";
        std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
        return;
    }

    Spec newSpec{};
    std::string errLog = "";
    if (!specPath.empty() && std::filesystem::exists(specPath)) {
        errLog = parseSpec(specPath, newSpec);
        if (errLog != "") {
            p->hasError     = true;
            p->errorMessage = "Hot Reload - " + p->name +
                             " spec parse failed. Check log.txt for more details.";
            std::cerr << "Hot Reload - " + p->name +
                         " spec parse failed - " + errLog;
            std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
            p->destroyTextures();
            p->spec = newSpec;
            return;
        }
    }

    Shader newShader;
    errLog = newShader.init(vertexSrc, fragSrc.c_str());
    if (!errLog.empty()) {
        p->hasError     = true;
        p->errorMessage = "Hot Reload - " + p->name + " shader error. " +
                          "Check Log.txt for more details\n";
        std::cerr << "Hot Reload - " + p->name + " - " + errLog + "\n";
        std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
        p->destroyTextures();
        p->spec = newSpec;
        return;
    }

    p->shader       = std::move(newShader);
    p->spec         = newSpec;
    p->hasError     = false;
    p->errorMessage = "";
    buildTextures(p);
}

inline void assertUserDefinedBufferSizes(ShaderPreset* p, size_t maxFBSize) {
    std::string ret = "";
    //set limits on buffer sizes and warn about double dependencies here
    if (p->spec.fftOutputMode == CUSTOM_SIZE && (p->spec.customFFTSize > 8192)) {
        ret = p->name + " - customFFTSize outside of bounds: 0 to 8192 (inclusive). " +
              "customFFTSize has been set to 0.\n";
        std::cerr << ret;
        p->errorMessage = ret;
        p->hasError = true;
        p->spec.customFFTSize = 0;
        std::cout << "Using ErrorShader in" << p->name << "\n";
    }
    if (p->spec.feedbackBufferSize > maxFBSize) {
        ret = p->name + " - feedback buffer size (" +
            std::to_string(p->spec.feedbackBufferSize) + " floats) exceeds gpu limit ("
            + std::to_string(maxFBSize) + ". feedbackBufferSize has been set to 0.\n";
        std::cerr << ret;
        p->errorMessage = ret;
        p->hasError = true;
        p->spec.feedbackBufferSize = 0;
        std::cout << "Using ErrorShader in " << p->name << "\n";
    }
}

inline std::string evalSpecExprs(Spec& spec, ExprContext& ctx) {
    std::string ret = evalExpr(spec.customFFTSizeExpr, ctx,
                               spec.customFFTSize, spec.fftUsesExprVar);
    if (!ret.empty()) return ret;
    ret = evalExpr(spec.feedbackBufferSizeExpr, ctx,
                   spec.feedbackBufferSize, spec.feedbackUsesExprVar);
    return ret;
}

