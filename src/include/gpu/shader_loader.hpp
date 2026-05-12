#pragma once

#include "gpu/shader_preset.hpp"
#include "gpu/texture_loader.hpp"
#include "gpu/font_loader.hpp"
#include "config/spec_parser.hpp"
#include "config/globals.hpp"
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

inline std::array<int, 512> formatErrorMessageForPreset(const std::string& msg,
                                                        int& errorLen) {
    errorLen = std::min((int)msg.size(), 512);
    std::array<int, 512> errorChars;
    for (int i = 0; i < errorLen; i++) {
        errorChars[i] = (int)msg[i];
    }
    return errorChars;
}

inline std::vector<ShaderPreset> loadPresets(const std::string& shadersDir) {
    std::vector<ShaderPreset> presets;

    if (!std::filesystem::exists(shadersDir)) {
        std::cerr << "loadPresets: directory not found: " << shadersDir << "\n";
        return presets;
    }

    std::string vtxSrc = getVertexSrc();

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
        std::string loadedName = p.name;
        p.spec = Spec{};

        if (std::filesystem::exists(specPath)) {
            ret = parseSpec(specPath.string(), p.spec);
            if (ret != "") {
                p.shaderDir = entry.path().string();
                p.lastFragWrite = std::filesystem::last_write_time(fragPath);
                p.lastSpecWrite = std::filesystem::exists(specPath)
                                ? std::filesystem::last_write_time(specPath)
                                : std::filesystem::file_time_type{};
                const std::string err = "loadPresets: Error in " + loadedName +
                                        " spec.cfg - " + ret;
                std::cerr << err;
                p.errorMessage = formatErrorMessageForPreset(err, p.errorLen);
                p.hasError = true;
                presets.push_back(std::move(p));
                std::cout << "loadPresets: using ErrorShader in " << p.name << "\n";
                continue;
            }
        }
        const int fftScaleMode = p.spec.customFFTSizeScalesWithWindow;
        if (p.spec.fftUsesExprVar[WINDOW_WIDTH] &&
           (fftScaleMode == WIDTH_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << loadedName <<
                " FFT custom size doubly scaled by WINDOW_WIDTH " <<
                "due to scale mode and width expression variable usage\n";
        }
        if (p.spec.fftUsesExprVar[WINDOW_HEIGHT] &&
           (fftScaleMode == HEIGHT_SCALE || fftScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << loadedName <<
                " FFT custom size doubly scaled by WINDOW_HEIGH T" <<
                "due to scale mode and height expression variable usage\n";
        }
        if (p.spec.customFFTSizeScalesWithWindow != NO_SCALE &&
            p.spec.fftOutputMode != CUSTOM_SIZE) {
            std::cout << "WARNING: " << loadedName <<
                " FFT is not using custom size mode, but has window scaling. " <<
                "Window scaling set to none. \n";
            p.spec.customFFTSizeScalesWithWindow = NO_SCALE;
        }
        const int feedbackScaleMode = p.spec.feedbackBufferScalesWithWindow;
        if (p.spec.feedbackUsesExprVar[WINDOW_WIDTH] &&
           (feedbackScaleMode == WIDTH_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << loadedName <<
                " Feedback buffer doubly scaled by WINDOW_WIDTH due to " <<
                "scale mode and width expression variable usage\n";
        }
        if (p.spec.feedbackUsesExprVar[WINDOW_HEIGHT] &&
           (feedbackScaleMode == HEIGHT_SCALE ||
            feedbackScaleMode == RESOLUTION_SCALE)) {
            std::cout << "WARNING: " << loadedName <<
                " Feedback buffer doubly scaled by WINDOW_HEIGHT due to " <<
                "scale mode and height expression variable usage\n";
        }

        p.shader = Shader();
        ret = p.shader.init(vtxSrc.c_str(), fragSrc.c_str());
        p.shaderDir = entry.path().string();
        p.lastFragWrite = std::filesystem::last_write_time(fragPath);
        p.lastSpecWrite = std::filesystem::exists(specPath)
                        ? std::filesystem::last_write_time(specPath)
                        : std::filesystem::file_time_type{};
        if (!ret.empty()) {
            const std::string err = "loadPresets: " + loadedName +
                                    " - shader compile failed - " + ret;
                        p.errorMessage = formatErrorMessageForPreset(err, p.errorLen);
            std::cerr << err;
            p.hasError = true;
            presets.push_back(std::move(p));
            std::cout << "loadPresets: using ErrorShader in " << loadedName << "\n";
            continue;
        }

        buildTextures(p);
        buildFonts(p);
        presets.push_back(std::move(p));
        std::cout << "loadPresets: loaded " << loadedName << "\n";
    }
    return presets;
}

inline void reloadPreset(ShaderPreset* p) {
    auto fragPath = std::filesystem::path(p->shaderDir) / "frag.glsl";
    auto specPath = std::filesystem::path(p->shaderDir) / "spec.cfg";
    std::string fragSrc = loadFile(fragPath.string());
    if (fragSrc.empty()) {
        p->hasError     = true;
        const std::string err = "Hot Reload - " + p->name +
                                " failed to open file - frag.glsl\n";
        p->errorMessage = formatErrorMessageForPreset(err, p->errorLen);
        std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
        return;
    }

    Spec newSpec{};
    std::string errLog = "";
    if (!specPath.empty() && std::filesystem::exists(specPath)) {
        errLog = parseSpec(specPath.string(), newSpec);
        if (errLog != "") {
            p->hasError     = true;
            const std::string err = "Hot Reload - " + p->name +
                                    " spec parse failed - " + errLog;
            std::cerr << err;
            p->errorMessage = formatErrorMessageForPreset(err, p->errorLen);
            std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
            p->destroyTextures();
            p->destroyFonts();
            p->spec = newSpec;
            return;
        }
    }

    std::string vtxSrc = getVertexSrc();
    Shader newShader;
    errLog = newShader.init(vtxSrc.c_str(), fragSrc.c_str());
    if (!errLog.empty()) {
        p->hasError     = true;
        const std::string err = "Hot Reload - " + p->name + " - " + errLog;
        std::cerr << err;
        p->errorMessage = formatErrorMessageForPreset(err, p->errorLen);
        std::cout << "Hot Reload: using ErrorShader in " << p->name << "\n";
        p->destroyTextures();
        p->destroyFonts();
        p->spec = newSpec;
        return;
    }

    p->shader       = std::move(newShader);
    p->spec         = newSpec;
    p->hasError     = false;
    p->errorMessage = {0};
    buildTextures(p);
    buildFonts(p);
}

inline void assertUserDefinedBufferSizes(ShaderPreset* p, size_t maxFBSize) {
    std::string ret = "";
    //set limits on buffer sizes and warn about double dependencies here
    if (p->spec.fftOutputMode == CUSTOM_SIZE && (p->spec.customFFTSize > 8192)) {
        ret = p->name + " - customFFTSize outside of bounds: 0 to 8192 (inclusive). " +
              "customFFTSize has been set to 0.\n";
        std::cerr << ret;
        p->errorMessage = formatErrorMessageForPreset(ret, p->errorLen);
        p->hasError = true;
        p->spec.customFFTSize = 0;
        std::cout << "Using ErrorShader in" << p->name << "\n";
    }
    if (p->spec.feedbackBufferSize > maxFBSize) {
        ret = p->name + " - feedback buffer size (" +
            std::to_string(p->spec.feedbackBufferSize) + " floats) exceeds gpu limit ("
            + std::to_string(maxFBSize) + ". feedbackBufferSize has been set to 0.\n";
        std::cerr << ret;
        p->errorMessage = formatErrorMessageForPreset(ret, p->errorLen);
        p->hasError = true;
        p->spec.feedbackBufferSize = 0;
        std::cout << "Using ErrorShader in " << p->name << "\n";
    }
}

inline std::string evalSpecExprs(Spec& spec, ExprContext& ctx) {
    std::string ret = evalExpr(spec.customFFTSizeExpr, ctx,
                               spec.customFFTSize, spec.fftUsesExprVar);
    if (!ret.empty()) return ret;
    ctx.fftArrSize = spec.customFFTSize;
    ctx.isFeedbackExpr = true;
    ret = evalExpr(spec.feedbackBufferSizeExpr, ctx,
                   spec.feedbackBufferSize, spec.feedbackUsesExprVar);
    return ret;
}

//if sampleRate > displayHz * hopSize, lower hopAmount until it hits 1,
//if sr > output still, raise fft order by 1 until output > sr
//cout new values, and change spec values to new values
inline void validateFFTRates(Globals& g, ShaderPreset* s) {
    bool valuesChanged = false;
    FFTOrder specOrder = s->spec.fftOrder;
    HopAmount specHops = s->spec.hopAmount;
    while (g.sampleRate > g.displayHz * g.hopSize) {
        if (s->spec.allowDroppedSamples) {
            valuesChanged = true;
            break;
        }
        if (s->spec.hopAmount > 1) {
            s->spec.hopAmount = static_cast<HopAmount>(s->spec.hopAmount / 2);
            g.hopSize = g.fftSize / s->spec.hopAmount;
            valuesChanged = true;
            continue;
        }
        if (s->spec.fftOrder == 13) {
            const std::string err = "Display Hz: " + std::to_string(g.displayHz) +
                                    ". Sample rate: " + std::to_string(g.sampleRate) +
                                    ". All shaders will not work without a lower sample rate or higher display rate.\n";
            s->hasError = true;
            s->errorMessage = formatErrorMessageForPreset(err, s->errorLen);
            return;
        }
        s->spec.fftOrder = static_cast<FFTOrder>(s->spec.fftOrder + 1);
        g.hopSize = 1 << s->spec.fftOrder;
        g.fftSize = g.hopSize;
        valuesChanged = true;
    }
    if (valuesChanged) {
        if (s->spec.allowDroppedSamples) {
            std::cout << "fftOrder: " << specOrder << ", hopAmount: " << specHops
                      << ", and displayHz: " << g.displayHz <<
                      " cannot keep up with sample rate: " << g.sampleRate <<
                      ". allowDroppedSamples is enabled, samples will be dropped.\n";
        }
        else {
            std::cout << "fftOrder: " << specOrder << ", hopAmount: " << specHops
                      << ", and displayHz: " << g.displayHz <<
                      " cannot keep up with sample rate: " << g.sampleRate <<
                      ". To avoid buffer overlap, hopAmount is now " << s->spec.hopAmount
                      << " and fftOrder is now " << s->spec.fftOrder << ".\n";
        }
    }
}

//Selective hot-reload helpers

inline void reloadTextureSlot(ShaderPreset* p, TextureSlot& slot) {
    auto fullPath = std::filesystem::path(p->shaderDir) / slot.filename;
    if (!isTextureFilenameSafe(slot.filename)) {
        std::cerr << "reloadTextureSlot: unsafe filename " << slot.filename << "\n";
        return;
    }
    int newW = 0, newH = 0;
    GLuint newId = uploadTexture(fullPath.string(), newW, newH);
    if (!newId) {
        std::cerr << "reloadTextureSlot: upload failed for " << slot.filename << "\n";
        return;
    }
    if (slot.texId) glDeleteTextures(1, &slot.texId);
    slot.texId = newId;
    slot.w = newW;
    slot.h = newH;
    // Re-bind the sampler uniform in case the program is already in use
    p->shader.use();
    auto it = p->shader.samplerLocations.find(slot.uniformName);
    if (it != p->shader.samplerLocations.end() && it->second != -1)
        glUniform1i(it->second, slot.unit);
    std::cout << "reloadTextureSlot: reloaded " << slot.filename << "\n";
}

inline void reloadFontSlot(ShaderPreset* p, FontSlot& slot) {
    auto fullPath = std::filesystem::path(p->shaderDir) / slot.filename;
    if (!isFontFilenameSafe(slot.filename)) {
        std::cerr << "reloadFontSlot: unsafe filename " << slot.filename << "\n";
        return;
    }
    auto fontData = loadFontBytes(fullPath.string());
    if (fontData.empty()) {
        std::cerr << "reloadFontSlot: could not load " << slot.filename << "\n";
        return;
    }
    SdfBakeResult bake;
    if (!bakeSdfAtlas(fontData, FONT_SDF_SIZE, FONT_SDF_PADDING,
                      FONT_FIRST_CHAR, FONT_NUM_GLYPHS, bake)) {
        std::cerr << "reloadFontSlot: bake failed for " << slot.filename << "\n";
        return;
    }
    GLuint newAtlas   = uploadSdfAtlas(bake);
    GLuint newMetrics = uploadMetricsTexture(bake);
    if (!newAtlas || !newMetrics) {
        if (newAtlas)   glDeleteTextures(1, &newAtlas);
        if (newMetrics) glDeleteTextures(1, &newMetrics);
        std::cerr << "reloadFontSlot: GPU upload failed for " << slot.filename << "\n";
        return;
    }
    if (slot.atlasTexId)   glDeleteTextures(1, &slot.atlasTexId);
    if (slot.metricsTexId) glDeleteTextures(1, &slot.metricsTexId);
    slot.atlasTexId   = newAtlas;
    slot.metricsTexId = newMetrics;
    // Re-bind sampler uniforms
    p->shader.use();
    auto itA = p->shader.samplerLocations.find(slot.uniformName);
    if (itA != p->shader.samplerLocations.end() && itA->second != -1)
        glUniform1i(itA->second, slot.atlasUnit);
    auto itM = p->shader.samplerLocations.find(slot.uniformName + "Metrics");
    if (itM != p->shader.samplerLocations.end() && itM->second != -1)
        glUniform1i(itM->second, slot.metricsUnit);
    std::cout << "reloadFontSlot: reloaded " << slot.filename << "\n";
}

